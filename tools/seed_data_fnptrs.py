#!/usr/bin/env python3
"""Discover functions that only ever appear as a pointer in .rdata/.data.

C++ engines keep handlers in vtables, RTTI and callback tables and CRuntimeClass structures, not only in
vtables. Those are single pointers surrounded by counts and string addresses,
so the "run of >= 3 consecutive function pointers" rule that finds vtables
never sees them, and neither does the call graph -- nothing CALLs them
directly, MFC does.

sub_004448E0 was one: MFC dispatched it, the address had no lifted function
behind it, and the process took an execute fault at an original VA.

A raw "any dword pointing into .text" scan produces far too many false hits
over a 2 MB range, so require the target to start with something that actually
looks like a function prologue. That is a heuristic, and a wrong entry splits a
real function -- which is why the accepted bytes are limited to the openings
MSVC 6 actually emits.

Run alongside seed_split_entries.py; re-run the pipeline while it adds entries.
"""
import json
import os
import struct
import sys

import pefile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# One binary per invocation: fakk2.exe, gamex86.dll, cgamex86.dll.
EXE = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, '_extracted', 'fakk2.exe')
_STEM = os.path.splitext(os.path.basename(EXE))[0].lower()
FUNCS = os.path.join(ROOT, 'config', f'functions_{_STEM}.json')
MANUAL = os.path.join(ROOT, 'config', f'manual_{_STEM}.json')

# Function openings MSVC 6 emits. Ordered longest-first where they overlap.
PROLOGUES = (
    b'\x55\x8b\xec',          # push ebp; mov ebp, esp
    b'\x8b\xff\x55\x8b\xec',  # mov edi,edi; push ebp; mov ebp,esp  (hotpatch)
    b'\x8b\x44\x24',          # mov eax, [esp+N]
    b'\x8b\x4c\x24',          # mov ecx, [esp+N]
    b'\x8b\x54\x24',          # mov edx, [esp+N]
    b'\x83\xec',              # sub esp, imm8
    b'\x81\xec',              # sub esp, imm32
    b'\x8b\x01',              # mov eax, [ecx]
    b'\x8b\x41',              # mov eax, [ecx+N]
    b'\x8b\xc1',              # mov eax, ecx
    b'\x33\xc0',              # xor eax, eax
    b'\x53',                  # push ebx
    b'\x56',                  # push esi
    b'\x57',                  # push edi
    b'\x51',                  # push ecx
    b'\x6a',                  # push imm8
    b'\xa1',                  # mov eax, [abs]
    b'\xb8',                  # mov eax, imm32
    b'\xe9',                  # jmp rel32  (thunk)
    b'\xe8',                  # call rel32 (forwarder) -- target checked below
    b'\xc3',                  # ret        (empty virtual)
    b'\xc2',                  # ret imm16
)


def main():
    pe = pefile.PE(EXE)
    image = pe.get_memory_mapped_image()
    base = pe.OPTIONAL_HEADER.ImageBase
    text = [s for s in pe.sections if s.Name.rstrip(b'\0') == b'.text'][0]
    lo = base + text.VirtualAddress
    hi = lo + max(text.Misc_VirtualSize, text.SizeOfRawData)

    known = {f['address_int'] for f in json.load(open(FUNCS))}
    manual = json.load(open(MANUAL)) if os.path.exists(MANUAL) else []
    have = {m['address_int'] for m in manual}

    candidates, rejected = set(), 0
    for sec in pe.sections:
        name = sec.Name.rstrip(b'\0').decode(errors='replace')
        if name not in ('.rdata', '.data'):
            continue
        start = base + sec.VirtualAddress
        size = min(sec.Misc_VirtualSize, sec.SizeOfRawData)
        for off in range(0, size - 4, 4):
            va = struct.unpack_from('<I', image, start - base + off)[0]
            if not (lo <= va < hi) or va in known or va in have:
                continue
            head = image[va - base: va - base + 8]
            if not any(head.startswith(p) for p in PROLOGUES):
                rejected += 1
                continue
            # E8/E9 are single bytes and appear constantly in data. Only
            # believe them when the relative target lands inside .text.
            if head[:1] in (b'\xe8', b'\xe9') and len(head) >= 5:
                rel = struct.unpack_from('<i', head, 1)[0]
                if not (lo <= va + 5 + rel < hi):
                    rejected += 1
                    continue
            candidates.add(va)

    new = sorted(candidates)
    for addr in new:
        manual.append({
            'address': f'0x{addr:08X}',
            'address_int': addr,
            'name': f'sub_{addr:08X}',
            'source': 'data function pointer (message map / RTTI / callback)',
        })
    if new:
        with open(MANUAL, 'w') as f:
            json.dump(manual, f, indent=2)

    print(f'data code-pointers rejected (no prologue): {rejected}')
    print(f'added as entries   : {len(new)}')
    if new:
        print('  ' + ', '.join(f'0x{a:08X}' for a in new[:8])
              + (' ...' if len(new) > 8 else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main())
