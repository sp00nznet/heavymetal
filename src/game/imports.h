/*
 * FAKK2 Recompilation - Win32 import bridge.
 *
 * FAKK2 is a Win32 program and this is a Win32 process, so its imports are not
 * emulated: each one is forwarded to the real function in the real DLL. Only
 * the calling convention is bridged. When the lifted code calls an import,
 * RECOMP_ICALL has already pushed a dummy return address, so from ESP the
 * simulated stack reads: [ret][arg0][arg1]...  -- one 32-bit slot per argument.
 */
#ifndef FAKK_IMPORTS_H
#define FAKK_IMPORTS_H

#include "recomp_types.h"

typedef struct {
    uint32_t    iat_va;   /* the IAT slot the lifted code calls through */
    const char* dll;
    const char* name;
    int         nargs;    /* 32-bit argument slots; -1 = cdecl (caller cleans) */
} fakk_import_t;

/* imports_gen.c -- regenerate with gen_imports.py */
extern const fakk_import_t g_fakk_imports[];
extern const unsigned      g_fakk_import_count;

/* Resolve every import and patch its IAT slot with a dispatch sentinel.
 * Returns the number resolved. Call after the image is mapped. */
unsigned fakk_imports_init(void);

#endif /* FAKK_IMPORTS_H */
