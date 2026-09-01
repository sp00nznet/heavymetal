#!/usr/bin/env python3
"""
Generate the FAKK2 import bridge table.

The lifted code calls every Win32 import as RECOMP_ICALL(MEM32(iat_slot)). We
patch each IAT slot at startup with a sentinel, so the dispatcher can map it
back to an entry in the table this script emits, and forward the call to the
REAL Win32 function -- FAKK2 is a Win32 program and we are a Win32 process, so
almost nothing needs emulating. Only the calling convention has to be bridged:
arguments come off the simulated stack.

Stdcall argument sizes are not in the PE (kernel32 exports undecorated names),
but they ARE in the Windows SDK import libraries, which keep the decorated
`_CreateFileA@28` symbol. So the arg counts are read out of the SDK rather than
maintained by hand.

Output: src/game/imports_gen.c  (committed; regenerate with this script)
Usage:  py -3 gen_imports.py [_extracted/fakk2.exe] [src/game/imports_gen.c]
"""
import glob
import os
import re
import sys

_here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(_here, 'tools', 'pcrecomp', 'tools', 'pe'))
from pe_analyze import analyze_pe, build_iat_map

SDK_LIBS = r'C:\Program Files (x86)\Windows Kits\10\Lib\*\um\x86\*.lib'

# Cdecl imports: the CALLER cleans the stack, so the bridge must not pop the
# arguments. Everything else in a Win32 import table is stdcall.
CDECL = {'wsprintfA', 'wsprintfW'}


def stdcall_arg_bytes():
    """{name: bytes of arguments} from the decorated symbols in the SDK libs."""
    out = {}
    versions = sorted(glob.glob(SDK_LIBS))
    if not versions:
        print('WARNING: no Windows SDK x86 import libraries found; '
              'every arg count falls back to 0', file=sys.stderr)
    for lib in versions:
        for name, n in re.findall(rb'_([A-Za-z_][A-Za-z0-9_]*)@(\d+)', open(lib, 'rb').read()):
            out[name.decode()] = int(n)
    return out


def main():
    exe = sys.argv[1] if len(sys.argv) > 1 else os.path.join(_here, '_extracted', 'fakk2.exe')
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(_here, 'src', 'game', 'imports_gen.c')

    rows = sorted(build_iat_map(analyze_pe(exe)).items())   # (va, (dll, name))
    argb = stdcall_arg_bytes()

    unknown = sorted({n for _, (_, n) in rows if n not in argb and n not in CDECL})

    L = ['/* FAKK2 import bridge table - AUTO-GENERATED, DO NOT EDIT */',
         f'/* Regenerate: py -3 gen_imports.py {os.path.basename(exe)} */',
         '#include "imports.h"', '',
         f'/* {len(rows)} imports from '
         f'{len({d for _, (d, _) in rows})} DLLs in {os.path.basename(exe)} */',
         'const fakk_import_t g_fakk_imports[] = {']
    for va, (dll, name) in rows:
        nargs = -1 if name in CDECL else argb.get(name, 0) // 4
        note = '' if name in argb or name in CDECL else '  /* arg count unknown */'
        L.append(f'    {{ 0x{va:08X}u, "{dll}", "{name}", {nargs} }},{note}')
    L += ['};', f'const unsigned g_fakk_import_count = {len(rows)};', '']

    os.makedirs(os.path.dirname(out), exist_ok=True)
    open(out, 'w').write('\n'.join(L))
    print(f'wrote {out}: {len(rows)} imports')
    if unknown:
        print(f'{len(unknown)} without a decorated SDK symbol (arg count 0): '
              + ', '.join(unknown))


if __name__ == '__main__':
    main()
