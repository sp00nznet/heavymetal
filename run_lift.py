#!/usr/bin/env python3
"""
Heavy Metal: FAKK2 Static Recompilation - lift driver.

Runs the pcrecomp linear-lift pipeline (cf. crimsonskies/run_pipeline.py) on
fakk2.exe. FAKK2 is a stock MSVC 6.0 build with no packing or DRM on the
executable, so function discovery is the plain raw-byte scan: direct call
targets + `push ebp; mov ebp,esp` prologues + `push imm32` function pointers.
No IDA/Ghidra catalog needed.

The same driver lifts the two game DLLs (gamex86.dll, cgamex86.dll) -- pass
their path and a distinct output dir.

Usage: py -3 run_lift.py [_extracted/fakk2.exe] [src/recomp/gen]
"""

import sys
import os

# Set up import paths for pcrecomp modules
pcrecomp_root = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'tools', 'pcrecomp', 'tools')
sys.path.insert(0, os.path.join(pcrecomp_root, 'pe'))
sys.path.insert(0, os.path.join(pcrecomp_root, 'lift'))

# Import the modules we need directly
from pe_analyze import analyze_pe, build_iat_map
from lift32 import Lifter

from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from capstone.x86 import X86_OP_IMM
import json
import time
import re
import struct


# ---- Inlined from generate.py (can't import due to path issues) ----

COND_JUMPS = {
    'je', 'jne', 'jz', 'jnz', 'ja', 'jae', 'jb', 'jbe',
    'jg', 'jge', 'jl', 'jle', 'js', 'jns', 'jo', 'jno',
    'jp', 'jnp', 'jcxz', 'jecxz',
}


class LinearInstruction:
    __slots__ = ['address', 'size', 'mnemonic', 'op_str', 'bytes', 'operands',
                 'is_call', 'is_ret', 'is_cond_jump', 'is_uncond_jump', 'is_jump']

    def __init__(self, insn):
        self.address = insn.address
        self.size = insn.size
        self.mnemonic = insn.mnemonic
        self.op_str = insn.op_str
        self.bytes = bytes(insn.bytes)
        self.operands = list(insn.operands) if insn.operands else []
        self.is_call = insn.mnemonic == 'call'
        self.is_ret = insn.mnemonic in ('ret', 'retn', 'retf')
        self.is_cond_jump = insn.mnemonic in COND_JUMPS
        self.is_uncond_jump = insn.mnemonic == 'jmp'
        self.is_jump = self.is_cond_jump or self.is_uncond_jump

    @property
    def end_address(self):
        return self.address + self.size

    def get_branch_target(self):
        if self.operands:
            op = self.operands[0]
            if op.type == X86_OP_IMM:
                return op.imm & 0xFFFFFFFF
        return None

    def __repr__(self):
        return f"0x{self.address:08X}: {self.mnemonic} {self.op_str}"


def find_entries(code_data, code_start, code_end):
    call_targets = set()
    for i in range(len(code_data) - 5):
        if code_data[i] == 0xE8:
            rel = struct.unpack_from('<i', code_data, i + 1)[0]
            target = (code_start + i + 5 + rel) & 0xFFFFFFFF
            if code_start <= target < code_end:
                call_targets.add(target)
    prologues = set()
    for i in range(len(code_data) - 3):
        if code_data[i:i+3] == b'\x55\x8B\xEC':
            prologues.add(code_start + i)
        if i > 0 and code_data[i-1] in (0xCC, 0x90, 0xC3):
            if code_data[i] == 0x83 and code_data[i+1] == 0xEC:
                prologues.add(code_start + i)

    # Function pointers passed as immediates: `push imm32` where imm32 is a
    # .text address (constructor/callback pointers handed to iterators like the
    # MSVC array-construct helper). These are never `call`ed directly, so the
    # call-graph + prologue scans miss them — the same blind spot as vtables.
    # Guard against mid-function false positives by requiring the target to be
    # preceded by a function terminator / alignment padding.
    TERMINATORS = (0xC3, 0xC2, 0xCB, 0xCC, 0x90, 0xE9)
    push_imm_targets = set()
    for i in range(len(code_data) - 5):
        if code_data[i] == 0x68:  # push imm32
            target = struct.unpack_from('<I', code_data, i + 1)[0]
            if code_start <= target < code_end:
                off = target - code_start
                if 0 < off < len(code_data) and code_data[off - 1] in TERMINATORS:
                    push_imm_targets.add(target)
    return sorted(call_targets | prologues | push_imm_targets)


JUMP_TABLE = None  # set in main(): (image_bytes, image_base) for table reads


def read_jump_table(insn, func_start, func_end):
    """Arms of `jmp dword ptr [reg*4 + imm32]`, as far as they stay in range."""
    if JUMP_TABLE is None or insn.mnemonic != 'jmp':
        return []
    m = re.match(r'^dword ptr \[\w+\*4 \+ (0x[0-9a-fA-F]+)\]$', insn.op_str)
    if not m:
        return []
    image, base = JUMP_TABLE
    table = int(m.group(1), 16)
    out = []
    for i in range(256):
        off = table - base + i * 4
        if off < 0 or off + 4 > len(image):
            break
        tgt = struct.unpack_from('<I', image, off)[0]
        if not (func_start <= tgt < func_end):
            break
        out.append(tgt)
    return out


MAX_SPLIT_GAP = 16   # bytes; a larger gap means data, not a split instruction


def decode_reaches(md, code_data, code_start, addr, stop):
    """Does a linear decode of [addr, stop) end exactly on stop?

    Returns False only for genuinely continuous code that stops a few bytes
    short -- those bytes are the tail of an instruction that `stop` splits.
    A computed jump means a jump table follows, and everything decoded after it
    is data pretending to be instructions, so that case reports True.
    """
    off = addr - code_start
    if off < 0 or off + (stop - addr) > len(code_data) or stop <= addr:
        return True

    last_end = addr
    for insn in md.disasm(code_data[off:off + (stop - addr)], addr):
        if insn.mnemonic == 'int3':
            return True
        if insn.mnemonic == 'jmp' and '[' in insn.op_str and '*4' in insn.op_str:
            return True
        last_end = insn.address + insn.size

    gap = stop - last_end
    return not (0 < gap < MAX_SPLIT_GAP)


def resolve_func_end(md, code_data, code_start, code_end, entries, idx):
    """The end of the function at entries[idx], skipping split boundaries."""
    addr = entries[idx]
    j = idx + 1
    for _ in range(8):
        cand = entries[j] if j < len(entries) else code_end
        cand = min(cand, addr + 65536)
        if cand <= addr:
            break
        if decode_reaches(md, code_data, code_start, addr, cand):
            return cand, (entries[j] if j < len(entries) else None)
        j += 1
    cand = entries[j] if j < len(entries) else code_end
    return min(cand, addr + 65536), (entries[j] if j < len(entries) else None)


def linear_disassemble_function(md, code_data, code_start, func_start, func_end):
    offset = func_start - code_start
    size = func_end - func_start
    if offset < 0 or offset + size > len(code_data):
        return [], set()
    raw = code_data[offset:offset + size]
    instructions = []
    leaders = {func_start}
    for insn in md.disasm(raw, func_start):
        li = LinearInstruction(insn)
        instructions.append(li)
        if li.is_cond_jump:
            target = li.get_branch_target()
            if target and func_start <= target < func_end:
                leaders.add(target)
            leaders.add(li.end_address)
        elif li.is_uncond_jump:
            target = li.get_branch_target()
            if target and func_start <= target < func_end:
                leaders.add(target)
            leaders.add(li.end_address)
            for arm in read_jump_table(li, func_start, func_end):
                leaders.add(arm)
        else:
            for arm in read_jump_table(li, func_start, func_end):
                leaders.add(arm)
        if li.mnemonic == 'int3':
            break
    return instructions, leaders


def lift_function_linear(lifter, name, instructions, leaders, func_start,
                         fallthrough=None):
    lines = []
    lines.append(f'void {name}(void) {{')
    # ebp is a global register (g_ebp via alias) — see recomp_types.h. It must
    # persist across calls for frameless helpers, so it is NOT a per-function
    # local. (No 'uint32_t ebp = 0;' emitted here anymore.)
    # The FPU stack and the flags are globals now (recomp_types.h aliases them
    # to g_*); declaring them here would shadow the shared state, and flags in
    # particular have to survive a branch from another lifted function.

    # Record the VA on entry so a crash / unresolved dispatch can name the
    # function it happened in (g_cur_func; ring tracer under -DRECOMP_TRACE).
    lines.append(f'    RECOMP_ENTER(0x{func_start:08X}u);')
    lines.append(f'')
    lifter._flag_state = None
    # The set of block starts that actually get a label in this function. lift32
    # needs it to tell an intra-function branch (goto) from one that leaves the
    # function (tail-dispatch); without it, a jump past the entry we trimmed at
    # emits `goto L_<addr>` with no such label -- error C2094 at compile time.
    lifter._labels = leaders
    for insn in instructions:
        if insn.address in leaders:
            lines.append(f'L_{insn.address:08X}:')
        lifted = lifter.lift_instruction(insn)
        for line in lifted:
            lines.append(f'    {line}')
    last = instructions[-1] if instructions else None
    if last is not None and not last.is_ret and not last.is_uncond_jump:
        # Trimmed at the next entry while still falling through: on the original
        # hardware execution ran straight on into it, so tail-call rather than
        # return (which would silently drop the rest of the flow).
        if fallthrough is not None:
            lines.append(f'    RECOMP_ITAIL(0x{fallthrough:08X}u); '
                         f'return; /* fell through to next function */')
        else:
            lines.append('    return; /* end of function */')
    lines.append('}')
    return '\n'.join(lines)


def write_chunk(output_dir, file_idx, funcs):
    filename = f'recomp_{file_idx:04d}.c'
    filepath = os.path.join(output_dir, filename)
    with open(filepath, 'w') as f:
        f.write('/* FAKK2 Recompilation - Auto-generated - DO NOT EDIT */\n')
        f.write(f'/* File {file_idx}: {len(funcs)} functions */\n\n')
        f.write('#define RECOMP_GENERATED_CODE\n')
        f.write('#include "recomp_types.h"\n')
        f.write('#include "recomp_funcs.h"\n')
        f.write('#include <math.h>\n')
        f.write('#include <string.h>\n\n')
        for code, addr, name in funcs:
            f.write(code)
            f.write('\n\n')


# ---- End inlined code ----


def main():
    exe_path = sys.argv[1] if len(sys.argv) > 1 else '_extracted/fakk2.exe'
    output_dir = sys.argv[2] if len(sys.argv) > 2 else 'src/recomp/gen'
    split_size = int(sys.argv[3]) if len(sys.argv) > 3 else 500

    os.makedirs(output_dir, exist_ok=True)

    # Clear stale chunks. A run that emits fewer files than the last one leaves
    # the extras behind, and CMake globs them: every function in the orphan is
    # a duplicate definition at link time.
    import glob as _glob
    for _stale in _glob.glob(os.path.join(output_dir, 'recomp_*.c')):
        os.remove(_stale)
    os.makedirs('config', exist_ok=True)

    print(f'=== FAKK2 Static Recompilation - lift ===', flush=True)
    print(f'[*] Binary: {exe_path}', flush=True)
    print(f'[*] Output: {output_dir}', flush=True)
    print(flush=True)

    # Step 1: PE Analysis
    print(f'[*] Step 1: PE Analysis...', flush=True)
    info = analyze_pe(exe_path)
    iat_map = build_iat_map(info)
    print(f'[*]   Image base: 0x{info.image_base:08X}', flush=True)
    print(f'[*]   Code: 0x{info.code_start:08X} - 0x{info.code_end:08X}', flush=True)
    print(f'[*]   IAT entries: {len(iat_map)}', flush=True)

    with open(exe_path, 'rb') as f:
        pe_data = f.read()

    global JUMP_TABLE
    import pefile as _pefile
    _pe = _pefile.PE(exe_path)
    JUMP_TABLE = (_pe.get_memory_mapped_image(), _pe.OPTIONAL_HEADER.ImageBase)

    text_sect = [s for s in info.sections if s.name == '.text'][0]
    offset = text_sect.raw_offset
    # The loader maps SizeOfRawData, and MSVC leaves real code in the slack
    # past VirtualSize. Taking the min drops it, and a `jmp` into it then has
    # no target to dispatch.
    size = max(text_sect.virtual_size, text_sect.raw_size)
    size = min(size, len(pe_data) - offset)
    code_data = pe_data[offset:offset + size]
    code_start = info.code_start
    code_end = code_start + size
    print(f'[*]   Code size: {len(code_data):,} bytes', flush=True)

    # Step 2: Function Discovery
    print(f'\n[*] Step 2: Function Discovery...', flush=True)
    entries = find_entries(code_data, code_start, code_end)
    print(f'[*]   Found {len(entries)} function entries', flush=True)

    md_probe = Cs(CS_ARCH_X86, CS_MODE_32)

    # Merge manually-added functions from config/functions.json
    stem = os.path.splitext(os.path.basename(exe_path))[0].lower()
    manual_funcs_file = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                     'config', f'manual_{stem}.json')
    if os.path.exists(manual_funcs_file):
        with open(manual_funcs_file) as mf:
            manual = json.load(mf)
        manual_addrs = set(e['address_int'] for e in manual)
        entry_set = set(entries)
        added = 0
        for addr in manual_addrs:
            if addr not in entry_set and code_start <= addr < code_end:
                entries.append(addr)
                added += 1
        entries.sort()
        print(f'[*]   Added {added} manual entries (total: {len(entries)})', flush=True)

    # Step 3: Disassembly + Lifting
    print(f'\n[*] Step 3: Disassembly + Code Generation...', flush=True)
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    lifter = Lifter(iat_map=iat_map)

    all_entries = []
    func_stats = []
    error_count = 0
    file_idx = 0
    chunk_funcs = []
    start = time.time()

    for idx, addr in enumerate(entries):
        func_end, next_real = resolve_func_end(md_probe, code_data, code_start,
                                               code_end, entries, idx)
        if func_end <= addr:
            continue

        if func_end - addr < 2:
            continue

        name = f'sub_{addr:08X}'

        try:
            instructions, leaders = linear_disassemble_function(
                md, code_data, code_start, addr, func_end)

            if not instructions:
                continue

            trimmed = []
            seen_ret = False
            for insn in instructions:
                if insn.mnemonic == 'int3':
                    break
                if seen_ret:
                    if insn.address not in leaders:
                        continue
                    seen_ret = False
                trimmed.append(insn)
                if insn.is_ret:
                    seen_ret = True
            if not trimmed:
                continue

            # A leader with no instruction behind it emits no label, so a
            # `goto L_<addr>` to it would dangle (error C2094). That happens for
            # a target landing mid-instruction (misaligned entry seeded from
            # data) and for one landing on the int3 padding trimmed above.
            # Dropping it makes lift32 tail-dispatch the branch instead.
            leaders &= {i.address for i in trimmed}

            code = lift_function_linear(lifter, name, trimmed, leaders, addr,
                                        fallthrough=next_real)
            chunk_funcs.append((code, addr, name))
            all_entries.append((addr, name))

            func_stats.append({
                'address': f'0x{addr:08X}',
                'address_int': addr,
                'name': name,
                'num_instructions': len(trimmed),
            })

        except Exception as e:
            stub = f'/* ERROR: {name} at 0x{addr:08X}: {e} */\nvoid {name}(void) {{ /* error */ }}\n'
            chunk_funcs.append((stub, addr, name))
            all_entries.append((addr, name))
            error_count += 1

        if len(chunk_funcs) >= split_size:
            write_chunk(output_dir, file_idx, chunk_funcs)
            elapsed = time.time() - start
            rate = len(all_entries) / elapsed if elapsed > 0 else 0
            print(f'[*]   {len(all_entries)}/{len(entries)} functions '
                  f'({file_idx + 1} files, {rate:.0f}/s, {error_count} err)', flush=True)
            file_idx += 1
            chunk_funcs = []

    if chunk_funcs:
        write_chunk(output_dir, file_idx, chunk_funcs)
        file_idx += 1

    # Step 4: Header + Dispatch Table
    print(f'\n[*] Step 4: Header + Dispatch Table...', flush=True)

    header_path = os.path.join(output_dir, 'recomp_funcs.h')
    with open(header_path, 'w') as f:
        f.write('/* FAKK2 Recompilation - Auto-generated - DO NOT EDIT */\n')
        f.write('#pragma once\n#include <stdint.h>\n\n')
        f.write(f'/* {len(all_entries)} recompiled functions */\n\n')
        for addr, name in all_entries:
            f.write(f'void {name}(void);  /* 0x{addr:08X} */\n')

    dispatch_path = os.path.join(output_dir, 'recomp_dispatch.c')
    with open(dispatch_path, 'w') as f:
        f.write('/* FAKK2 Recompilation - Auto-generated - DO NOT EDIT */\n\n')
        f.write('#include "recomp_types.h"\n')
        f.write('#include "recomp_funcs.h"\n\n')
        f.write('const recomp_dispatch_entry_t recomp_dispatch_table[] = {\n')
        for addr, name in sorted(all_entries, key=lambda x: x[0]):
            f.write(f'    {{ 0x{addr:08X}u, {name} }},\n')
        f.write('};\n\n')
        f.write(f'const uint32_t recomp_dispatch_count = {len(all_entries)};\n')

    with open(f'config/functions_{stem}.json', 'w') as f:
        json.dump(func_stats, f, indent=2)

    # Summary
    elapsed = time.time() - start
    total_lines = 0
    total_bytes = 0
    for fn in os.listdir(output_dir):
        fp = os.path.join(output_dir, fn)
        if os.path.isfile(fp):
            total_bytes += os.path.getsize(fp)
            with open(fp, 'r') as rf:
                total_lines += sum(1 for _ in rf)

    print(f'\n{"="*60}', flush=True)
    print(f'  FAKK2 LIFT COMPLETE', flush=True)
    print(f'{"="*60}', flush=True)
    print(f'  Functions:      {len(all_entries):,}', flush=True)
    print(f'  Source files:   {file_idx}', flush=True)
    print(f'  Errors:         {error_count}', flush=True)
    print(f'  Lines of C:     {total_lines:,}', flush=True)
    print(f'  Generated size: {total_bytes / 1048576:.1f} MB', flush=True)
    print(f'  Time:           {elapsed:.1f}s', flush=True)
    print(f'{"="*60}', flush=True)


if __name__ == '__main__':
    main()
