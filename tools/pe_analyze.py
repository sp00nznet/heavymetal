#!/usr/bin/env python3
"""
pe_analyze.py -- PE binary analysis tool for FAKK2 recomp

Analyzes the original FAKK2 binaries (fakk2.exe, gamex86.dll, cgamex86.dll)
and outputs detailed information about their structure, imports, exports,
and other characteristics relevant to static recompilation.

Usage:
    python3.13 pe_analyze.py <binary_path>
    python3.13 pe_analyze.py --all <directory>

Requires: pip install pefile
"""

import sys
import os
import struct
import datetime

try:
    import pefile
except ImportError:
    print("Error: pefile module required. Install with: python3.13 -m pip install pefile")
    sys.exit(1)


def analyze_pe(filepath):
    """Comprehensive PE analysis of a single binary."""
    pe = pefile.PE(filepath)
    filename = os.path.basename(filepath)
    filesize = os.path.getsize(filepath)

    print(f"\n{'='*72}")
    print(f"  PE Analysis: {filename}")
    print(f"  Size: {filesize:,} bytes")
    print(f"{'='*72}\n")

    # --- Basic Headers ---
    print("--- PE Header ---")
    machine = pe.FILE_HEADER.Machine
    machine_str = {0x14c: "Intel 386 (32-bit)", 0x8664: "AMD64 (64-bit)"}.get(machine, f"Unknown (0x{machine:x})")
    print(f"  Machine:       {machine_str}")

    timestamp = pe.FILE_HEADER.TimeDateStamp
    dt = datetime.datetime.utcfromtimestamp(timestamp)
    print(f"  Compile time:  {dt.strftime('%Y-%m-%d %H:%M:%S UTC')} (0x{timestamp:08x})")

    linker = f"{pe.OPTIONAL_HEADER.MajorLinkerVersion}.{pe.OPTIONAL_HEADER.MinorLinkerVersion}"
    print(f"  Linker:        {linker}")

    subsys = pe.OPTIONAL_HEADER.Subsystem
    subsys_str = {2: "Windows GUI", 3: "Windows Console"}.get(subsys, f"Unknown ({subsys})")
    print(f"  Subsystem:     {subsys_str}")

    print(f"  Image base:    0x{pe.OPTIONAL_HEADER.ImageBase:08x}")
    print(f"  Entry point:   0x{pe.OPTIONAL_HEADER.ImageBase + pe.OPTIONAL_HEADER.AddressOfEntryPoint:08x}")

    chars = pe.FILE_HEADER.Characteristics
    char_flags = []
    if chars & 0x0001: char_flags.append("RELOCS_STRIPPED")
    if chars & 0x0002: char_flags.append("EXECUTABLE_IMAGE")
    if chars & 0x0100: char_flags.append("32BIT_MACHINE")
    if chars & 0x2000: char_flags.append("DLL")
    print(f"  Characteristics: {' | '.join(char_flags)}")

    dll_chars = pe.OPTIONAL_HEADER.DllCharacteristics
    if dll_chars == 0:
        print(f"  DLL Chars:     0x0000 (NONE - no ASLR, no DEP, no CFG)")
    else:
        dc_flags = []
        if dll_chars & 0x0040: dc_flags.append("DYNAMIC_BASE (ASLR)")
        if dll_chars & 0x0100: dc_flags.append("NX_COMPAT (DEP)")
        if dll_chars & 0x4000: dc_flags.append("GUARD_CF")
        print(f"  DLL Chars:     {' | '.join(dc_flags)}")

    # --- Sections ---
    print("\n--- Sections ---")
    print(f"  {'Name':<10} {'VirtSize':>12} {'RawSize':>12} {'VirtAddr':>12} {'Characteristics'}")
    for section in pe.sections:
        name = section.Name.decode('ascii', errors='replace').rstrip('\x00')
        chars_str = []
        sc = section.Characteristics
        if sc & 0x20: chars_str.append("CODE")
        if sc & 0x40: chars_str.append("INITIALIZED_DATA")
        if sc & 0x80: chars_str.append("UNINITIALIZED_DATA")
        if sc & 0x20000000: chars_str.append("EXECUTE")
        if sc & 0x40000000: chars_str.append("READ")
        if sc & 0x80000000: chars_str.append("WRITE")
        print(f"  {name:<10} {section.Misc_VirtualSize:>12,} {section.SizeOfRawData:>12,} 0x{section.VirtualAddress:08x} {', '.join(chars_str)}")

    # --- Imports ---
    print("\n--- Imports ---")
    if hasattr(pe, 'DIRECTORY_ENTRY_IMPORT'):
        total_imports = 0
        for entry in pe.DIRECTORY_ENTRY_IMPORT:
            dll_name = entry.dll.decode('ascii', errors='replace')
            funcs = [imp for imp in entry.imports]
            total_imports += len(funcs)
            print(f"\n  {dll_name} ({len(funcs)} functions):")
            for imp in sorted(funcs, key=lambda x: (x.name or b'').decode('ascii', errors='replace')):
                if imp.name:
                    name = imp.name.decode('ascii', errors='replace')
                    print(f"    {name}")
                else:
                    print(f"    Ordinal {imp.ordinal}")
        print(f"\n  Total imports: {total_imports}")
    else:
        print("  No imports found")

    # --- Exports ---
    print("\n--- Exports ---")
    if hasattr(pe, 'DIRECTORY_ENTRY_EXPORT'):
        exports = pe.DIRECTORY_ENTRY_EXPORT.symbols
        print(f"  {len(exports)} exported symbols:")
        for exp in exports:
            name = exp.name.decode('ascii', errors='replace') if exp.name else f"Ordinal_{exp.ordinal}"
            print(f"    [{exp.ordinal:>3}] 0x{exp.address:08x}  {name}")
    else:
        print("  No exports found")

    # --- Resources ---
    if hasattr(pe, 'DIRECTORY_ENTRY_RESOURCE'):
        print("\n--- Resources ---")
        def print_resources(entries, indent=0):
            for entry in entries:
                name = entry.name.string.decode('utf-8') if entry.name else f"ID={entry.id}"
                prefix = "  " * (indent + 1)
                if entry.struct.DataIsDirectory:
                    rtype = {1: "CURSOR", 2: "BITMAP", 3: "ICON", 4: "MENU", 5: "DIALOG",
                             6: "STRING", 9: "ACCELERATOR", 14: "GROUP_ICON",
                             16: "VERSION"}.get(entry.id, name) if indent == 0 else name
                    print(f"{prefix}[{rtype}]")
                    print_resources(entry.directory.entries, indent + 1)
                else:
                    size = entry.data.struct.Size
                    print(f"{prefix}{name}: {size:,} bytes")
        print_resources(pe.DIRECTORY_ENTRY_RESOURCE.entries)

    # --- Version Info ---
    if hasattr(pe, 'FileInfo'):
        print("\n--- Version Info ---")
        for fileinfo in pe.FileInfo:
            for entry in fileinfo:
                if hasattr(entry, 'StringTable'):
                    for st in entry.StringTable:
                        for key, value in st.entries.items():
                            k = key.decode('utf-8', errors='replace')
                            v = value.decode('utf-8', errors='replace')
                            print(f"  {k}: {v}")

    pe.close()


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <binary_path>")
        print(f"       {sys.argv[0]} --all <directory>")
        sys.exit(1)

    if sys.argv[1] == '--all':
        directory = sys.argv[2] if len(sys.argv) > 2 else '.'
        for ext in ('*.exe', '*.dll'):
            import glob
            for filepath in sorted(glob.glob(os.path.join(directory, ext))):
                analyze_pe(filepath)
    else:
        analyze_pe(sys.argv[1])


if __name__ == '__main__':
    main()
