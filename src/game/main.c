/*
 * Heavy Metal: FAKK2 Static Recompilation - runtime entry point.
 *
 * Defines the global x86 machine state the lifted code in src/recomp/gen/
 * refers to, maps the original fakk2.exe data sections at their real VAs, and
 * calls the recompiled entry point.
 *
 * The host binary is linked at 0x70000000 (see CMakeLists.txt) so FAKK2's own
 * VA range 0x00400000-0x00D40000 is free and the mapping is 1:1 -- g_mem_base
 * stays 0 and MEM32(va) is a plain dereference.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

#include "recomp_types.h"
#include "recomp_funcs.h"
#include "image_loader.h"
#include "imports.h"

/*========================================================================
 * Global machine state (declared extern by recomp_types.h)
 *========================================================================*/

uint32_t g_eax = 0, g_ecx = 0, g_edx = 0, g_esp = 0;
uint32_t g_ebx = 0, g_esi = 0, g_edi = 0, g_ebp = 0;
uint32_t g_flag_a = 0, g_flag_b = 0;
int      g_flag_k = 0;
uint32_t g_cf = 0;
int      g_df = 1;          /* forward; the CRT never clears it at startup */
int      g_fpu_cmp = 0;
uint16_t g_seg_cs = 0, g_seg_ds = 0, g_seg_es = 0;
uint16_t g_seg_fs = 0, g_seg_gs = 0, g_seg_ss = 0;
ptrdiff_t g_mem_base = 0;

/* Simulated TIB. The lifter emits fs:/gs: as MEM32(FS_BASE + off). */
uint32_t g_fs_seg[256] = {0};
uint32_t g_fs_base = 0;
uint32_t g_gs_base = 0;

/* x87 stack / MMX file are global: __ftol and friends take their argument on
 * the FPU stack from the caller, and the control word persists across calls. */
double   g_st[8] = {0};
int      g_fp_top = 0;
uint16_t g_fpu_cw = 0x027F;
uint64_t g_mm[8] = {0};

uint32_t g_cur_func = 0;
unsigned char g_cov[COV_BYTES];

uint32_t g_icall_trace[ICALL_TRACE_SIZE] = {0};
uint32_t g_icall_trace_idx = 0;
uint32_t g_icall_count = 0;
uint32_t g_call_depth = 0, g_call_depth_max = 0;
uint32_t g_total_calls = 0, g_total_icalls = 0;
int      g_heap_check_enabled = 0;
uint32_t g_heap_check_every = 0;
uint32_t g_heap_check_last_ok_call = 0, g_heap_check_last_ok_va = 0;
char     g_trace_ring[TRACE_RING_SIZE][TRACE_ENTRY_SIZE];
uint32_t g_trace_ring_idx = 0;

#ifdef RECOMP_TRACE
uint32_t g_enter_trace[RECOMP_ENTER_SIZE] = {0};
uint32_t g_enter_idx = 0;
void recomp_trace_enter(uint32_t va) {
    g_enter_trace[g_enter_idx++ & (RECOMP_ENTER_SIZE - 1)] = va;
}
#endif

void recomp_dump_trace(const char* why) {
    fprintf(stderr, "\n=== trace ring (%s) ===\n", why ? why : "?");
    uint32_t show = TRACE_RING_SIZE;
    if (show > g_trace_ring_idx) show = g_trace_ring_idx;
    for (uint32_t i = show; i > 0; i--) {
        uint32_t idx = (g_trace_ring_idx - i) & (TRACE_RING_SIZE - 1);
        if (g_trace_ring[idx][0]) fprintf(stderr, "  %s", g_trace_ring[idx]);
    }
}

void recomp_report_coverage(const char* why) {
    uint32_t hit = 0;
    for (uint32_t i = 0; i < recomp_dispatch_count; i++) {
        uint32_t va = recomp_dispatch_table[i].address - COV_LO;
        if (va < (COV_HI - COV_LO) && (g_cov[va >> 3] & (1u << (va & 7u)))) hit++;
    }
    fprintf(stderr, "[cov] %s: %u of %u functions executed (%.1f%%)\n",
            why ? why : "?", hit, recomp_dispatch_count,
            recomp_dispatch_count ? 100.0 * hit / recomp_dispatch_count : 0.0);
}

/*========================================================================
 * Dispatch
 *========================================================================*/

/* The generated table is emitted sorted by VA, so a binary search is valid. */
recomp_func_t recomp_lookup(uint32_t va) {
    int lo = 0, hi = (int)recomp_dispatch_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint32_t mva = recomp_dispatch_table[mid].address;
        if (mva == va) return recomp_dispatch_table[mid].func;
        if (mva < va) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}

/* Hand-written replacements for lifted functions, keyed by original VA.
 * ponytail: empty until a lifted function actually needs shimming. */
recomp_func_t recomp_lookup_manual(uint32_t va) { (void)va; return NULL; }

/* recomp_lookup_import lives in imports.c. */

/* Calls to code resolved at runtime (LoadLibrary/GetProcAddress -- FAKK2 does
 * this for opengl32). Nothing registered yet, so every such call is unresolved.
 * ponytail: the GL loader needs this before anything draws. */
int recomp_native_call(uint32_t va) { (void)va; return 0; }
void recomp_register_native(uint32_t addr, const char* name, int nargs) {
    (void)addr; (void)name; (void)nargs;
}

/* Only meaningful in a hybrid build, where a routed vtable slot holds a thunk
 * address instead of a function VA. This build is fully lifted -- no thunks. */
int recomp_thunk_ova(uint32_t addr, uint32_t* out_ova) {
    (void)addr; (void)out_ova; return 0;
}

/* A lifted callee must pop at least the dummy return address RECOMP_CALL
 * pushed, so esp on return is never below where it started. Anything else
 * means the caller's view of its own frame is now wrong. */
void recomp_esp_check(const char* name, uint32_t before, uint32_t after) {
    if (after >= before && after - before <= 0x200) return;
    static int reported = 0;
    if (reported >= 20) return;
    reported++;
    fprintf(stderr, "[esp] %s returned esp=0x%08X, expected >= 0x%08X (delta %d)"
            " at call #%u depth %u, caller sub_%08X\n",
            name, after, before, (int)(after - before),
            g_total_calls, g_call_depth, g_cur_func);
}

/*========================================================================
 * Memory + entry
 *========================================================================*/

#define FAKK_IMAGE_BASE  0x00400000u
#define FAKK_IMAGE_END   0x00E00000u   /* past .rsrc (image span ~0x981218) */
/* The simulated stack goes just above the image, not below it: the child's PEB
 * and process parameters already own part of 0x00100000 and reserving there
 * comes back ERROR_INVALID_ADDRESS. The VA is arbitrary -- nothing in FAKK2
 * hard-codes a stack address. */
#define FAKK_STACK_BASE  0x00E00000u
#define FAKK_STACK_SIZE  0x00100000u   /* 1 MB simulated stack */
#define FAKK_VA_END      (FAKK_STACK_BASE + FAKK_STACK_SIZE)

/*------------------------------------------------------------------------
 * Self-relaunching launcher.
 *
 * The Windows loader maps NLS/locale data and creates the process heap before
 * any of our code runs -- TLS callbacks included -- and those allocations land
 * inside 0x00100000-0x00E00000, which is where FAKK2's own image and simulated
 * stack have to live (its VAs are absolute; there is nowhere else to put them).
 *
 * So the first launch is a launcher: it starts a copy of itself suspended,
 * reserves the range in the child with VirtualAllocEx, and resumes it. The
 * child's loader then finds the range taken and puts its heap elsewhere. The
 * child knows it is the child from an environment variable.
 *----------------------------------------------------------------------*/
#define FAKK_CHILD_ENV "FAKK2_RECOMP_CHILD"

static int is_recomp_child(void) {
    char buf[8];
    DWORD n = GetEnvironmentVariableA(FAKK_CHILD_ENV, buf, sizeof(buf));
    return n > 0 && buf[0] == '1';
}

static int launcher_main(void) {
    char exe[MAX_PATH];
    GetModuleFileNameA(NULL, exe, MAX_PATH);

    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    SetEnvironmentVariableA(FAKK_CHILD_ENV, "1");

    if (!CreateProcessA(exe, GetCommandLineA(), NULL, NULL, TRUE,
                        CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "FATAL: CreateProcess failed (%lu)\n", GetLastError());
        return 1;
    }
    /* One span: image + simulated stack, both above 0x00400000 so the child's
     * PEB and process parameters are not in the way. */
    if (!VirtualAllocEx(pi.hProcess, (void*)FAKK_IMAGE_BASE,
                        FAKK_VA_END - FAKK_IMAGE_BASE, MEM_RESERVE, PAGE_NOACCESS)) {
        fprintf(stderr, "FATAL: could not reserve 0x%08X-0x%08X in the child (%lu)\n",
                FAKK_IMAGE_BASE, FAKK_VA_END, GetLastError());
        TerminateProcess(pi.hProcess, 1);
        return 1;
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    return (int)code;
}


static LONG WINAPI veh_handler(EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
        return EXCEPTION_CONTINUE_SEARCH;
    fprintf(stderr, "\n=== access violation in sub_%08X ===\n", g_cur_func);
    fprintf(stderr, "  eax=%08X ecx=%08X edx=%08X ebx=%08X\n", g_eax, g_ecx, g_edx, g_ebx);
    fprintf(stderr, "  esp=%08X ebp=%08X esi=%08X edi=%08X\n", g_esp, g_ebp, g_esi, g_edi);
    fprintf(stderr, "  calls=%u depth=%u\n", g_total_calls, g_call_depth);
    recomp_dump_trace("crash");
    recomp_report_coverage("crash");
    return EXCEPTION_CONTINUE_SEARCH;
}

int main(int argc, char* argv[]) {
    const char* exe = argc > 1 ? argv[1] : "_extracted/fakk2.exe";

    if (!is_recomp_child()) return launcher_main();

    printf("=== Heavy Metal: FAKK2 - static recompilation ===\n");
    printf("[*] %u lifted functions\n", recomp_dispatch_count);

    AddVectoredExceptionHandler(1, veh_handler);

    if (!recomp_load_image(exe, FAKK_IMAGE_BASE)) {
        fprintf(stderr, "FATAL: could not map %s at 0x%08X\n", exe, FAKK_IMAGE_BASE);
        return 1;
    }
    printf("[*] mapped %s at 0x%08X\n", exe, FAKK_IMAGE_BASE);

    /* The launcher already reserved this range, so commit into the existing
     * reservation; MEM_RESERVE over it would fail. */
    if (!VirtualAlloc((void*)(uintptr_t)FAKK_STACK_BASE, FAKK_STACK_SIZE,
                      MEM_COMMIT, PAGE_READWRITE)) {
        fprintf(stderr, "FATAL: could not commit the simulated stack at 0x%08X (%lu)\n",
                FAKK_STACK_BASE, GetLastError());
        return 1;
    }
    g_esp = FAKK_STACK_BASE + FAKK_STACK_SIZE - 16;
    g_fs_base = (uint32_t)(uintptr_t)g_fs_seg;

    printf("[*] %u of %u imports resolved\n", fakk_imports_init(),
           g_fakk_import_count);

    printf("[*] esp=0x%08X, entry=sub_004C4C73\n", g_esp);
    sub_004C4C73();   /* PE entry point: the MSVC 6 CRT startup */

    recomp_report_coverage("exit");
    return 0;
}
