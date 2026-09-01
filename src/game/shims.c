/*
 * FAKK2 Recompilation - hand-written replacements for lifted CRT functions.
 *
 * These stand in for functions run_lift.py deliberately does not lift (its
 * HOST_SHIM set). The allocator family is here because MSVC 6's malloc is its
 * small-block heap -- __sbh_alloc_block walks a linked structure of page
 * descriptors and faults once the game starts allocating for real. The host CRT
 * does the same job correctly. They move as a set: a block allocated by one
 * allocator must not be freed by the other.
 *
 * Calling convention: RECOMP_CALL pushed a dummy return address, so from ESP
 * the stack reads [ret][arg0][arg1]... These are all cdecl, so the shim pops
 * only the return address -- the caller cleans up its own arguments.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <malloc.h>
#include "recomp_types.h"

#define SHIM_ARG(n) MEM32(g_esp + 4 + (n) * 4)
#define SHIM_RET()  do { g_esp += 4; } while (0)

/* The host is 32-bit, so a host pointer is a VA the lifted code can hold. */
#define TO_VA(p)   ((uint32_t)(uintptr_t)(p))
#define FROM_VA(v) ((void*)(uintptr_t)(v))

/* Walk the host heap on every allocation, to pin corruption on the window of
 * lifted calls that caused it rather than the later malloc that trips over it.
 * Off unless FAKK2_HEAPCHECK is set (main.c). */
static void heap_watch(void) {
    static int broken = 0;
    if (!g_heap_check_enabled || broken) return;
    if (HeapValidate(GetProcessHeap(), 0, NULL)) {
        g_heap_check_last_ok_call = g_total_calls;
        g_heap_check_last_ok_va = g_cur_func;
        return;
    }
    broken = 1;
    fprintf(stderr, "[heap] corrupt at call #%u (in sub_%08X); "
                    "last clean at call #%u (in sub_%08X)\n",
            g_total_calls, g_cur_func,
            g_heap_check_last_ok_call, g_heap_check_last_ok_va);
}

/* malloc(size) at 0x004C2090 */
void sub_004C2090(void) {
    uint32_t size = SHIM_ARG(0);
    heap_watch();
    g_eax = TO_VA(malloc(size ? size : 1));
    SHIM_RET();
}

/* operator new(size) at 0x004C3A16 -- MSVC's new is malloc plus a new-handler
 * loop we do not need: the host malloc either returns memory or we are dead. */
void sub_004C3A16(void) { sub_004C2090(); }

/* free(p) at 0x004C377E.
 *
 * The pointer is checked before it is freed: a block the host allocator never
 * handed out means some other allocation path is still live in the lifted code,
 * and passing it to free() corrupts the host heap in a way that only surfaces
 * hundreds of calls later inside an unrelated malloc. */
void sub_004C377E(void) {
    void* p = FROM_VA(SHIM_ARG(0));
    if (p && !HeapValidate(GetProcessHeap(), 0, p)) {
        static int warned = 0;
        if (warned++ < 20)
            fprintf(stderr, "[heap] free(%p) from sub_%08X: not a host block, ignored\n",
                    p, g_cur_func);
    } else {
        free(p);
    }
    SHIM_RET();
}

/* _msize(p) at 0x004C5058 */
void sub_004C5058(void) {
    void* p = FROM_VA(SHIM_ARG(0));
    g_eax = p ? (uint32_t)_msize(p) : 0;
    SHIM_RET();
}

/* realloc(p, size) at 0x004C4F20 */
void sub_004C4F20(void) {
    g_eax = TO_VA(realloc(FROM_VA(SHIM_ARG(0)), SHIM_ARG(1)));
    SHIM_RET();
}

/*
 * The printf family.
 *
 * On 32-bit x86 a va_list is just a pointer into the pushed argument block, and
 * the simulated stack is real host memory at the same addresses, so the lifted
 * caller's arguments can be handed straight to the host vsprintf. Pointer
 * arguments (%s) are VAs, which are host pointers here too.
 */

/* vsprintf(buf, fmt, ap) at 0x004C2A6E */
void sub_004C2A6E(void) {
    g_eax = (uint32_t)vsprintf((char*)FROM_VA(SHIM_ARG(0)),
                               (const char*)FROM_VA(SHIM_ARG(1)),
                               (va_list)(uintptr_t)SHIM_ARG(2));
    SHIM_RET();
}

/* sprintf(buf, fmt, ...) at 0x004C20EE -- the varargs start at argument 2. */
void sub_004C20EE(void) {
    g_eax = (uint32_t)vsprintf((char*)FROM_VA(SHIM_ARG(0)),
                               (const char*)FROM_VA(SHIM_ARG(1)),
                               (va_list)(uintptr_t)(g_esp + 4 + 2 * 4));
    SHIM_RET();
}

/* _tzset(void) at 0x004C87FC -- deliberately does nothing. See run_lift.py. */
void sub_004C87FC(void) { SHIM_RET(); }
