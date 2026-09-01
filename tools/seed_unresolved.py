#!/usr/bin/env python3
"""Turn the runtime's "unresolved VA" complaints into function entries.

A jump into the middle of what the raw scan thought was one function has no
label to land on, so the lifter tail-dispatches it -- and at runtime there is
no function at that VA to dispatch to. Every such VA is a function boundary the
scan missed. Adding it to config/manual_<stem>.json and re-lifting splits the
function there, which is what the original code does anyway.

Usage: py -3 tools/seed_unresolved.py run.log [fakk2]
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    log = sys.argv[1]
    stem = sys.argv[2] if len(sys.argv) > 2 else 'fakk2'
    funcs = os.path.join(ROOT, 'config', f'functions_{stem}.json')
    manual = os.path.join(ROOT, 'config', f'manual_{stem}.json')

    known = {f['address_int'] for f in json.load(open(funcs))}
    entries = json.load(open(manual)) if os.path.exists(manual) else []
    have = {m['address_int'] for m in entries}

    # Only VAs in the code range: an unresolved import sentinel or a garbage
    # pointer is not a missing function.
    lo, hi = 0x00401000, 0x004D4905
    found = {int(m, 16) for m in re.findall(r'unresolved VA (0x[0-9A-Fa-f]{8})', open(log).read())}
    new = sorted(va for va in found if lo <= va < hi and va not in known and va not in have)

    for va in new:
        entries.append({'address': f'0x{va:08X}', 'address_int': va,
                        'name': f'sub_{va:08X}',
                        'source': 'unresolved dispatch at runtime (split function)'})
    if new:
        json.dump(entries, open(manual, 'w'), indent=2)
    print(f'{len(found)} unresolved VAs in the log, {len(new)} added as entries')
    return 0


if __name__ == '__main__':
    sys.exit(main())
