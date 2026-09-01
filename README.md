# Heavy Metal: FAKK2 -- Static Recompilation

*"One girl. One sword. One hell of a good time."*

A static recompilation of **Heavy Metal: FAKK2** (2000) for modern Windows. The
original binary is lifted back into C and rebuilt with a modern compiler --
Julie Strain's finest hour, running on today's hardware without an emulator.

## What Is This?

FAKK2 was built on **id Tech 3 + Ritual Entertainment's UberTools** -- a heavily modified Quake III engine with skeletal animation, a custom scripting language, dynamic particles, and a cinematic camera system. It shipped August 4, 2000, compiled with MSVC 6.0 for 32-bit Windows.

This project statically recompiles the original `fakk2.exe` (v1.02) into a
native binary that runs on modern systems, preserving the original gameplay feel
while fixing the compatibility issues that plague the 25-year-old executable.

## Why FAKK2 Doesn't Work on Modern Windows

| Issue | Cause | Our Fix |
|-------|-------|---------|
| Crashes on launch | No relocations; must load at `0x00400000`, which the modern loader will not guarantee | We *are* the code -- a launcher reserves the range before the loader can take it |
| Graphics corruption | Legacy OpenGL 1.x calls on modern drivers | Planned: intercept the GL loader and route to a modern backend |
| No widescreen | Hardcoded 4:3 resolutions | Planned: the resolution table is now just C |
| Timing issues | `timeGetTime()` precision on modern kernels | Bridged import; replaceable with a high-resolution timer |
| CD check | Original requires disc in drive | Bridged import; replaceable |
| Registry dependency | Install path/CD key in Windows registry | Forwarded to the real registry today; replaceable with a config file |

The last column is the point of a recompilation: every one of those is now C or
a bridged import, not machine code, so each is a small edit rather than a patch
to a 25-year-old binary.

## How It Works

This is a **static recompilation**, not a reimplementation: `fakk2.exe` is
disassembled and its machine code is lifted back into compilable C, function by
function, using the shared [pcrecomp](https://github.com/sp00nznet/pcrecomp)
toolchain (`tools/pcrecomp`, a submodule). The result is the original game's own
logic, in C, built with a modern compiler.

```
fakk2.exe (2000, MSVC 6)
    |
    |  run_lift.py        PE analysis -> function discovery -> x86-to-C
    v
src/recomp/gen/           4,118 functions, ~430K lines of C, 0 lift errors
    +
src/game/main.c           x86 machine state, image mapping, VA-reserving launcher
src/game/imports.c        Win32 import bridge -- forwards to the REAL Win32 API
src/game/callbacks.c      the real -> lifted boundary (WndProc, thread entries)
src/game/shims.c          the handful of CRT functions the host does better
    |
    v
build/bin/Release/fakk2.exe
```

Three pieces make it run:

- **The image is mapped at its original VAs.** FAKK2's addresses are absolute,
  so `0x00400000`-`0x00E00000` has to be free. The Windows loader claims part of
  that range before any of our code runs, so the first launch is a launcher: it
  starts a suspended copy of itself, reserves the range with `VirtualAllocEx`,
  and resumes it.
- **Imports are forwarded, not emulated.** FAKK2 is a Win32 program and the host
  is a Win32 process, so all 194 imports go to the real `kernel32`/`user32`/
  `gdi32`/`winmm`/`advapi32`/`ole32`. Only the calling convention is bridged.
  The stdcall argument counts come out of the Windows SDK import libraries,
  which keep the decorated `_CreateFileA@28` symbols -- so no hand-maintained
  table (`gen_imports.py`).
- **The host build is 32-bit x86.** The lifted code keeps machine state in
  32-bit globals and stores host pointers in them, so the original VAs and real
  heap pointers have to be the same width.

### Function discovery

The raw call-graph scan finds 3,192 functions. FAKK2 is C++ (UberTools), so
another 632 exist only as vtable/RTTI/callback pointers in `.rdata`/`.data` --
nothing ever `call`s them directly. `tools/seed_data_fnptrs.py` finds those, and
`tools/seed_unresolved.py` turns the runtime's "unresolved VA" complaints into
the function boundaries the scan split wrongly. Both feed
`config/manual_fakk2.json`, which `run_lift.py` merges on the next pass.

## Original Binary Analysis

| Binary | Size | Compiled | Sections |
|--------|------|----------|----------|
| `fakk2.exe` | 1,318,912 bytes | 2000-08-22, MSVC 6.0 | .text: 866KB code |
| `gamex86.dll` | 1,740,800 bytes | 2000-07-31, MSVC 6.0 | .text: 1.3MB code |
| `cgamex86.dll` | 339,968 bytes | 2000-07-31, MSVC 6.0 | .text: 253KB code |

The engine executable imports from 6 Win32 DLLs (195 unique functions) and dynamically loads OpenGL at runtime. All three binaries export a shared `str` C++ class (50 methods) as the ABI bridge.

## Project Status

**Phase: Runtime bringup.** The recompiled binary runs the original MSVC 6 CRT
startup, brings up the engine, creates its window with a working message loop,
reads the registry, and opens `pak0.pk3` -- **1.44 million calls and a quarter of
the lifted code** before it faults.

| Phase | Status | Result |
|-------|--------|--------|
| **0** -- binary analysis, asset extraction | Complete | Unpacked MSVC 6 PE, no DRM; 194 imports, 866 KB of code |
| **1** -- function discovery | Complete | **4,118 functions** (call graph + prologues + `push imm32` + vtable/data pointers + runtime-discovered splits) |
| **2** -- x86-to-C lift | Complete | **~430K lines of C, 0 lift errors** |
| **3** -- compile and link | Complete | 0 errors, 32-bit host at base `0x70000000` |
| **4** -- runtime bringup | Complete | Image at its real VAs, **all 194 imports forwarded to the real Win32 API**, CRT startup runs |
| **5** -- engine init | **In progress** | **1,443,616 calls, 1,039 of 4,118 functions executed (25%)**: window created, WndProc callbacks land in lifted code, message loop pumping, registry read, `fakk/pak0.pk3` open |
| 6 -- renderer | Pending | The GL entry points already bridge (see below); nothing draws yet |
| 7 -- assets, gameplay | Pending | PK3 filesystem, BSP (FAKK v12), TIKI models, Morpheus scripts, Ghost particles |

The two game DLLs lift cleanly as well and are not yet linked in:
`gamex86.dll` at **8,303 functions** / 650K lines, `cgamex86.dll` at **1,239
functions** / 128K lines -- over 13,000 functions and 1.2M lines of C across the
three binaries, all at 0 lift errors.

### The three things that made it run

**Imports are forwarded, not stubbed.** Each IAT slot is patched with a
sentinel; the dispatcher maps the sentinel back to the real `kernel32` /
`user32` / `gdi32` / `winmm` / `advapi32` / `ole32` function and moves the
arguments from the simulated stack to the real one. Stdcall argument counts come
out of the Windows SDK import libraries, which keep the decorated
`_CreateFileA@28` symbol -- so no table is maintained by hand. The same trick
covers everything the game resolves at runtime through `GetProcAddress`
(11,503 signatures, opengl32 included), so the renderer's entry points are
already callable.

**Callbacks come back through a trampoline.** The window procedure the game
registers is the address of its own code. We map the original image, so Windows
calling it would run the 2000-vintage machine code -- which dies on its first
`call [IAT]`. `src/game/callbacks.c` hands Windows a real function instead, one
per lifted procedure, that moves the arguments onto the simulated stack and
dispatches. Thread entry points work the same way.

**A few CRT functions are host shims, not lifted** (`run_lift.py`'s `HOST_SHIM`).
MSVC 6's allocator is its own small-block heap, and the lifted `_msize` reads a
block header that is not there -- silently corrupting the host heap hundreds of
calls before a later `malloc` trips over it. `malloc`/`free`/`realloc`/`_msize`/
`operator new` go to the host CRT as a set. So do `sprintf`/`vsprintf`: on 32-bit
x86 a `va_list` is just a pointer into the argument block, which is exactly what
the simulated stack holds, so the hand-off is direct.

### Bring-up tooling

```bash
py -3 tools/seed_data_fnptrs.py _extracted/fakk2.exe   # vtable / callback pointers
py -3 tools/seed_unresolved.py run.log                 # boundaries the scan split wrong
py -3 run_lift.py                                      # re-lift with the new entries
```

`seed_unresolved.py` closes the loop: run the binary, and every "unresolved VA"
or unlifted callback in the log is a function boundary the static scan missed.
Feed the log back in and re-lift until it converges -- that alone took coverage
from 4.8% to 23%.

Runtime switches, all optional: `FAKK2_TRACE_STR=1` logs the file names and
window text the game passes to Win32 (this is how you find out *why* it stopped);
`FAKK2_HEAPCHECK=<n>` validates the host heap.

### Building

```bash
git clone --recursive https://github.com/sp00nznet/heavymetal
cd heavymetal

# Put your own fakk2.exe / gamex86.dll / cgamex86.dll in _extracted/
py -3 run_lift.py                      # lift fakk2.exe   -> src/recomp/gen
py -3 gen_imports.py                   # import tables    -> src/game/
cmake -B build -A Win32                # 32-bit host, required
cmake --build build --config Release
```

Run it from a directory that has the game's `fakk/` folder beside it, and give it
the original binary to map:

```bash
cd _extracted && ../build/bin/Release/fakk2.exe fakk2.exe
```

The generated C is not committed -- it is a derivative of the original binary,
so it carries the original's licence. Produce it from your own copy.

## Related Projects

| Project | Description |
|---------|-------------|
| [fakk2-sdk](https://github.com/a1batross/fakk2-sdk) | Official FAKK2 SDK (game logic source), builds on modern systems |
| [fakk2-rework](https://github.com/Sporesirius/fakk2-rework) | Modernization effort (CMake, Vulkan goals) |
| [ioquake3](https://github.com/ioquake/ioq3) | Community id Tech 3 fork (base engine reference) |
| [sof](https://github.com/sp00nznet/sof) | Soldier of Fortune recomp (sister project, id Tech 2 + GHOUL) |
| [pcrecomp](https://github.com/sp00nznet/pcrecomp) | The shared toolchain this project lifts with -- and the ~18 other PC games it was forged on |

## Game Maps

From `autoexec.cfg`, the full campaign map order:
```
intro -> fakkhouse -> training -> homes1 -> landersroost -> creeperpens
-> homes2good -> towncenter_good -> under -> over -> shield -> homes2evil
-> otto -> towncenter_evil -> cliff1 -> cliff2 -> swamp1 -> swamp2
-> swamp3 -> gruff -> cemetery -> fog -> water -> blood -> oracleway -> oracle
```

## Legal

This project contains no copyrighted game assets or decompiled code. It is a clean-room static recompilation. You must supply your own legally obtained copy of Heavy Metal: FAKK2.

The original game was developed by **Ritual Entertainment** and published by **Gathering of Developers** (2000). Heavy Metal is a trademark of Heavy Metal Magazine/Metal Mammoth, Inc. Julie is Julie.

## License

MIT -- see [LICENSE](LICENSE)

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Julie needs warriors.
