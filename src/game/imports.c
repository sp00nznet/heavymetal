/*
 * FAKK2 Recompilation - Win32 import bridge. See imports.h.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "imports.h"

/* The value written into each IAT slot. It has to be something the lifted code
 * will never mistake for a real address, and it has to survive being loaded
 * into a register and called: RECOMP_ICALL dispatches on the value, not on the
 * slot it came from, which is why the index is encoded in the sentinel. */
#define IMPORT_SENTINEL_BASE 0xFE000000u

static uint32_t call_native(void* fn, const uint32_t* args, int n, int is_cdecl);

static FARPROC g_resolved[1024];
static char    g_called[1024];   /* logged-once flags, one per import */
int            g_trace_strings;  /* FAKK2_TRACE_STR (main.c) */

/* Addresses handed out by GetProcAddress, so a later call through one of them
 * can be named (and eventually forwarded). See recomp_native_call. */
#define MAX_DYNAMIC 256
static struct { uint32_t addr; char name[64]; } g_dynamic[MAX_DYNAMIC];
static unsigned g_dynamic_count;

static void remember_dynamic(uint32_t addr, uint32_t name_va) {
    if (!addr || g_dynamic_count >= MAX_DYNAMIC) return;
    for (unsigned i = 0; i < g_dynamic_count; i++)
        if (g_dynamic[i].addr == addr) return;
    const char* nm = (const char*)(uintptr_t)name_va;
    if (name_va < 0x10000) return;            /* ordinal, not a name */
    g_dynamic[g_dynamic_count].addr = addr;
    strncpy(g_dynamic[g_dynamic_count].name, nm, 63);
    g_dynamic[g_dynamic_count].name[63] = 0;
    fprintf(stderr, "[dynamic] GetProcAddress %s -> 0x%08X\n", nm, addr);
    g_dynamic_count++;
}

static int argc_for(const char* name) {
    int lo = 0, hi = (int)g_fakk_argc_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2, c = strcmp(g_fakk_argc[mid].name, name);
        if (c == 0) return g_fakk_argc[mid].nargs;
        if (c < 0) lo = mid + 1; else hi = mid - 1;
    }
    return -1;
}

/*
 * Call an address the lifted code got at runtime rather than from the IAT --
 * in practice everything the game pulls out of opengl32, plus the odd kernel32
 * feature probe. RECOMP_ICALL has pushed the dummy return address and pops it
 * itself on this path, so the bridge cleans up the ARGUMENTS only.
 */
int recomp_native_call(uint32_t va) {
    for (unsigned i = 0; i < g_dynamic_count; i++) {
        if (g_dynamic[i].addr != va) continue;

        int n = argc_for(g_dynamic[i].name);
        if (n < 0) {
            static int warned = 0;
            if (warned++ < 20)
                fprintf(stderr, "[dynamic] %s (0x%08X): no stdcall signature, "
                                "cannot bridge\n", g_dynamic[i].name, va);
            return 0;
        }
        if (n > 16) n = 16;

        uint32_t args[16];
        for (int a = 0; a < n; a++) args[a] = MEM32(g_esp + 4 + a * 4);
        g_eax = call_native((void*)(uintptr_t)va, args, n, 0);
        g_esp += (uint32_t)n * 4;   /* stdcall; the macro pops the return address */
        return 1;
    }
    return 0;
}

void recomp_register_native(uint32_t addr, const char* name, int nargs) {
    (void)addr; (void)name; (void)nargs;
}

unsigned fakk_imports_init(void) {
    unsigned ok = 0, n = g_fakk_import_count;
    if (n > 1024) n = 1024;

    for (unsigned i = 0; i < n; i++) {
        HMODULE h = LoadLibraryA(g_fakk_imports[i].dll);
        g_resolved[i] = h ? GetProcAddress(h, g_fakk_imports[i].name) : NULL;
        if (g_resolved[i]) ok++;
        else fprintf(stderr, "[import] unresolved %s!%s\n",
                     g_fakk_imports[i].dll, g_fakk_imports[i].name);
        MEM32(g_fakk_imports[i].iat_va) = IMPORT_SENTINEL_BASE + i;
    }
    return ok;
}

/*
 * Forward a call to the real Win32 function: push n argument slots, call, let
 * the callee clean up (stdcall) or clean up ourselves (cdecl).
 *
 * ponytail: 32-bit MSVC inline asm. It is the whole reason the host build is
 * x86 -- a 64-bit host would need a per-arity thunk table or libffi instead.
 */
static uint32_t call_native(void* fn, const uint32_t* args, int n, int is_cdecl) {
    uint32_t ret;
    __asm {
        push esi
        mov  esi, args
        mov  ecx, n
        lea  esi, [esi + ecx*4]     /* one past the last argument */
    push_next:
        test ecx, ecx
        jz   pushed
        sub  esi, 4
        push dword ptr [esi]
        dec  ecx
        jmp  push_next
    pushed:
        mov  eax, fn
        call eax                    /* stdcall: the callee pops its arguments */
        mov  ret, eax
        cmp  is_cdecl, 0
        je   done
        mov  ecx, n
        lea  esp, [esp + ecx*4]     /* cdecl: we pop them */
    done:
        pop  esi
    }
    return ret;
}

/* Which import the pending dispatch is for.
 * ponytail: a single global, set by the lookup immediately before the thunk
 * runs. Fine while the lifted code is single-threaded; FAKK2 does create
 * threads (the sound mixer), so this becomes __declspec(thread) the moment a
 * second thread runs lifted code. */
static unsigned g_pending;

static void import_thunk(void) {
    const fakk_import_t* imp = &g_fakk_imports[g_pending];
    int n = imp->nargs < 0 ? 0 : imp->nargs;

    uint32_t args[16];
    if (n > 16) n = 16;
    for (int i = 0; i < n; i++)
        args[i] = MEM32(g_esp + 4 + i * 4);   /* skip the dummy return address */

    if (!g_called[g_pending]) {
        g_called[g_pending] = 1;
        fprintf(stderr, "[import] %s!%s\n", imp->dll, imp->name);
    }

    /* String arguments worth seeing during bring-up: which files the game looks
     * for, and the text it puts in its console window -- which is where an
     * engine error message ends up. FAKK2_TRACE_STR=1 to enable. */
    if (g_trace_strings) {
        static const struct { const char* fn; int arg; } watch[] = {
            { "CreateFileA", 0 }, { "FindFirstFileA", 0 }, { "DeleteFileA", 0 },
            { "SetWindowTextA", 1 }, { "OutputDebugStringA", 0 },
            { "LoadLibraryA", 0 }, { "RegOpenKeyA", 1 }, { "RegQueryValueExA", 1 },
            { "GetPrivateProfileStringA", 0 }, { "MessageBoxA", 1 },
        };
        for (unsigned w = 0; w < sizeof watch / sizeof watch[0]; w++)
            if (n > watch[w].arg && strcmp(imp->name, watch[w].fn) == 0 &&
                args[watch[w].arg] > 0x10000)
                fprintf(stderr, "[str] %s(\"%.200s\")\n", imp->name,
                        (const char*)(uintptr_t)args[watch[w].arg]);
    }

    /* Callbacks: an argument that is the address of one of the game's own
     * functions cannot be passed to Windows as-is. Swap in a trampoline. */
    if (n >= 1) {
        if (strcmp(imp->name, "RegisterClassA") == 0 ||
            strcmp(imp->name, "RegisterClassW") == 0)
            fakk_hook_wndclass(args[0], 4);            /* WNDCLASS.lpfnWndProc */
        else if (strcmp(imp->name, "RegisterClassExA") == 0 ||
                 strcmp(imp->name, "RegisterClassExW") == 0)
            fakk_hook_wndclass(args[0], 8);            /* WNDCLASSEX.lpfnWndProc */
        else if (n >= 3 && (int32_t)args[1] == -4 &&        /* GWL_WNDPROC */
                 (strcmp(imp->name, "SetWindowLongA") == 0 ||
                  strcmp(imp->name, "SetWindowLongW") == 0))
            args[2] = fakk_wndproc_trampoline(args[2]);
        else if (n >= 5 && strcmp(imp->name, "CreateThread") == 0)
            args[2] = fakk_hook_threadproc(args[2], args[3], &args[3]);
    }

    if (!g_resolved[g_pending]) {
        static int warned = 0;
        if (warned++ < 20)
            fprintf(stderr, "[import] %s!%s is unresolved, returning 0\n",
                    imp->dll, imp->name);
        g_eax = 0;
    } else {
        g_eax = call_native((void*)g_resolved[g_pending], args, n, imp->nargs < 0);
        if (strcmp(imp->name, "GetProcAddress") == 0) remember_dynamic(g_eax, args[1]);
    }

    /* Pop the dummy return address, plus the arguments for stdcall. */
    g_esp += 4 + (imp->nargs > 0 ? (uint32_t)imp->nargs * 4 : 0);
}

recomp_func_t recomp_lookup_import(uint32_t va) {
    uint32_t idx = va - IMPORT_SENTINEL_BASE;
    if (idx >= g_fakk_import_count) return NULL;
    g_pending = idx;
    return import_thunk;
}
