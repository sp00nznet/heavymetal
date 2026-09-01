/*
 * FAKK2 Recompilation - the real -> lifted boundary.
 *
 * Some pointers the game hands to Windows are addresses of its OWN functions:
 * the window procedure it registers, the entry point of a thread it creates.
 * Windows then calls back to them, and nothing about that call goes through the
 * recomp dispatcher -- the CPU simply jumps to the address.
 *
 * That address is mapped (we map the original image so the lifted code can read
 * its data), so the call lands in the ORIGINAL 2000-vintage machine code, which
 * runs until its first `call dword ptr [IAT]` and dies executing an import
 * sentinel. What Windows needs instead is a real function of ours: one that
 * moves the arguments onto the simulated stack and dispatches to the lifted
 * body. That is what this file provides, and the import bridge swaps our
 * trampoline in as each callback is registered.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include "imports.h"

uint32_t recomp_call_lifted(uint32_t va, const uint32_t* args, int n) {
    recomp_func_t fn = recomp_lookup(va);
    if (!fn) {
        fprintf(stderr, "[callback] no lifted function at 0x%08X\n", va);
        return 0;
    }
    uint32_t saved_esp = g_esp;
    uint32_t saved_cur = g_cur_func;

    for (int i = n - 1; i >= 0; i--) PUSH32(g_esp, args[i]);
    PUSH32(g_esp, RECOMP_RETADDR);   /* the lifted `ret` pops this */
    fn();

    g_esp = saved_esp;               /* stdcall or cdecl, we own the frame */
    g_cur_func = saved_cur;
    return g_eax;
}

/*
 * Window procedures.
 *
 * The game registers one for its main window and then subclasses the console's
 * input line with SetWindowLong, so more than one lifted procedure has to look
 * like a real WNDPROC at the same time. Each gets a slot: a distinct real
 * function Windows can call, bound to the lifted VA behind it.
 *
 * Only addresses that ARE lifted functions get replaced. The old procedure
 * SetWindowLong hands back belongs to Windows and must pass through untouched,
 * or the CallWindowProc that chains to it would call into nothing.
 */
#define WNDPROC_SLOTS 8
static uint32_t g_wp_va[WNDPROC_SLOTS];
static unsigned g_wp_used;

static LRESULT wp_dispatch(unsigned slot, HWND hwnd, UINT msg,
                           WPARAM wparam, LPARAM lparam) {
    uint32_t args[4] = { (uint32_t)(uintptr_t)hwnd, (uint32_t)msg,
                         (uint32_t)wparam, (uint32_t)lparam };
    return (LRESULT)recomp_call_lifted(g_wp_va[slot], args, 4);
}

#define WP_SLOT(n)     static LRESULT CALLBACK wp##n(HWND h, UINT m, WPARAM w, LPARAM l) {         return wp_dispatch(n, h, m, w, l);     }
WP_SLOT(0) WP_SLOT(1) WP_SLOT(2) WP_SLOT(3)
WP_SLOT(4) WP_SLOT(5) WP_SLOT(6) WP_SLOT(7)

static const WNDPROC g_wp[WNDPROC_SLOTS] = { wp0, wp1, wp2, wp3,
                                             wp4, wp5, wp6, wp7 };

/* A real WNDPROC standing in for the lifted function at `va`, or `va` itself
 * when it is not one of ours. */
uint32_t fakk_wndproc_trampoline(uint32_t va) {
    if (!va || !recomp_lookup(va)) return va;
    for (unsigned i = 0; i < g_wp_used; i++)
        if (g_wp_va[i] == va) return (uint32_t)(uintptr_t)g_wp[i];
    if (g_wp_used >= WNDPROC_SLOTS) {
        fprintf(stderr, "[callback] out of WndProc slots for sub_%08X\n", va);
        return va;
    }
    g_wp_va[g_wp_used] = va;
    fprintf(stderr, "[callback] WndProc sub_%08X -> slot %u (%p)\n",
            va, g_wp_used, (void*)g_wp[g_wp_used]);
    return (uint32_t)(uintptr_t)g_wp[g_wp_used++];
}

/* Swap the lpfnWndProc of a WNDCLASS(EX) the lifted code is about to register.
 * `off` is the field offset: 4 in WNDCLASSA, 8 in WNDCLASSEXA. */
void fakk_hook_wndclass(uint32_t wc_va, unsigned off) {
    if (!wc_va) return;
    uint32_t* slot = (uint32_t*)(uintptr_t)(wc_va + off);
    *slot = fakk_wndproc_trampoline(*slot);
}

/*
 * Thread entry points. Same problem, and the thread would run original machine
 * code on a real stack, so it has to come back through the dispatcher too.
 * ponytail: the lifted code is one machine state -- registers, flags and the
 * simulated stack are globals -- so a second thread running lifted code races
 * the first. FAKK2 uses threads for sound; when one actually starts doing work,
 * this needs per-thread state (__declspec(thread) in recomp_types.h) rather
 * than another shim here.
 */
typedef struct { uint32_t va, param; } thread_start_t;

static DWORD WINAPI thread_trampoline(LPVOID p) {
    thread_start_t start = *(thread_start_t*)p;
    free(p);
    uint32_t arg = start.param;
    return (DWORD)recomp_call_lifted(start.va, &arg, 1);
}

/* Returns a real thread entry to hand CreateThread in place of the lifted VA. */
uint32_t fakk_hook_threadproc(uint32_t va, uint32_t param, uint32_t* out_param) {
    thread_start_t* s = (thread_start_t*)malloc(sizeof *s);
    if (!s) return va;
    s->va = va;
    s->param = param;
    *out_param = (uint32_t)(uintptr_t)s;
    fprintf(stderr, "[callback] thread entry sub_%08X -> trampoline\n", va);
    return (uint32_t)(uintptr_t)thread_trampoline;
}
