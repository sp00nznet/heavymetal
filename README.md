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
src/recomp/gen/           3,885 functions, ~430K lines of C, 0 lift errors
    +
src/game/main.c           x86 machine state, image mapping, VA-reserving launcher
src/game/imports.c        Win32 import bridge -- forwards to the REAL Win32 API
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
startup and gets ~27,000 calls into the engine before faulting.

| Phase | Status | Result |
|-------|--------|--------|
| **0** -- binary analysis, asset extraction | Complete | Unpacked MSVC 6 PE, no DRM; 194 imports, 866 KB of code |
| **1** -- function discovery | Complete | **3,885 functions** (call graph + prologues + `push imm32` + vtable/data pointers + runtime-discovered splits) |
| **2** -- x86-to-C lift | Complete | **~430K lines of C, 0 lift errors** |
| **3** -- compile and link | Complete | 0 errors, 32-bit host at base `0x70000000` |
| **4** -- runtime bringup | **In progress** | Image maps at its real VAs, all **194 imports resolve and forward to the real Win32 API**, CRT startup runs, ~27K calls into engine init |
| 5 -- engine init | Pending | Follow the fault past CRT/engine setup; a real->lifted trampoline for Win32 callbacks (`WndProc`) |
| 6 -- renderer | Pending | The game loads OpenGL through `LoadLibrary`/`GetProcAddress`; that path needs `recomp_native_call` |
| 7 -- assets, gameplay | Pending | PK3 filesystem, BSP (FAKK v12), TIKI models, Morpheus scripts, Ghost particles |

The two game DLLs lift cleanly as well and are not yet linked in:
`gamex86.dll` at **8,303 functions** / 650K lines, `cgamex86.dll` at **1,239
functions** / 128K lines -- 13,427 functions and 1.2M lines of C across the
three binaries, all at 0 lift errors.

### Building

```bash
git clone --recursive https://github.com/sp00nznet/heavymetal
cd heavymetal

# Put your own fakk2.exe / gamex86.dll / cgamex86.dll in _extracted/
py -3 run_lift.py                      # lift fakk2.exe   -> src/recomp/gen
py -3 gen_imports.py                   # import table     -> src/game/imports_gen.c
cmake -B build -A Win32                # 32-bit host, required
cmake --build build --config Release

build/bin/Release/fakk2.exe _extracted/fakk2.exe
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
