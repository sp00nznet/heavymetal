/*
 * FAKK2 Recompilation - Win32 import bridge. See imports.h.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include "imports.h"

/* The value written into each IAT slot. It has to be something the lifted code
 * will never mistake for a real address, and it has to survive being loaded
 * into a register and called: RECOMP_ICALL dispatches on the value, not on the
 * slot it came from, which is why the index is encoded in the sentinel. */
#define IMPORT_SENTINEL_BASE 0xFE000000u

static FARPROC g_resolved[1024];

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

    if (!g_resolved[g_pending]) {
        static int warned = 0;
        if (warned++ < 20)
            fprintf(stderr, "[import] %s!%s is unresolved, returning 0\n",
                    imp->dll, imp->name);
        g_eax = 0;
    } else {
        g_eax = call_native((void*)g_resolved[g_pending], args, n, imp->nargs < 0);
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
