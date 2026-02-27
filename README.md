# Heavy Metal: FAKK2 -- Static Recompilation

*"One girl. One sword. One hell of a good time."*

A static recompilation of **Heavy Metal: FAKK2** (2000) targeting modern Windows 11 x86-64. Julie Strain's finest hour deserves to run flawlessly on today's hardware.

## What Is This?

FAKK2 was built on **id Tech 3 + Ritual Entertainment's UberTools** -- a heavily modified Quake III engine with skeletal animation, a custom scripting language, dynamic particles, and a cinematic camera system. It shipped August 4, 2000, compiled with MSVC 6.0 for 32-bit Windows.

This project statically recompiles the original `fakk2.exe` (v1.02) into a native 64-bit binary that runs on modern systems, preserving the original gameplay feel while fixing the compatibility issues that plague the 25-year-old executable.

## Why FAKK2 Doesn't Work on Modern Windows

| Issue | Cause | Our Fix |
|-------|-------|---------|
| Crashes on launch | ASLR randomizes load address; original has no relocations, must load at `0x00400000` | 64-bit recomp with full ASLR support |
| Graphics corruption | Legacy OpenGL 1.x calls on modern drivers | Updated to OpenGL 4.x via SDL2 |
| No widescreen | Hardcoded 4:3 resolutions | Native widescreen/ultrawide support |
| Timing issues | `timeGetTime()` precision on modern kernels | High-resolution SDL2 timers |
| CD check | Original requires disc in drive | Removed (you supply your own game files) |
| Registry dependency | Install path/CD key in Windows registry | Config file based |

## Architecture

```
fakk2-recomp (unified 64-bit binary)
    |
    |-- Engine Core ........... Command system, CVars, filesystem (PK3/ZIP)
    |-- TIKI System ........... Skeletal models, animation, bone hierarchy
    |-- Morpheus Scripting .... ~700 commands for entity/camera/AI control
    |-- Ghost Particles ....... Custom particle system (~50 params/emitter)
    |-- Renderer .............. OpenGL 4.x (replacing legacy GL 1.x)
    |-- Sound ................. SDL2 audio (replacing Miles Sound System)
    |-- Client ................ Input, view, HUD, cgame interface
    |-- Server ................ Game simulation, AI, physics, fgame interface
    '-- Platform Layer ........ SDL2 (replacing 195 Win32 API calls)
```

## Original Binary Analysis

| Binary | Size | Compiled | Sections |
|--------|------|----------|----------|
| `fakk2.exe` | 1,318,912 bytes | 2000-08-22, MSVC 6.0 | .text: 866KB code |
| `gamex86.dll` | 1,740,800 bytes | 2000-07-31, MSVC 6.0 | .text: 1.3MB code |
| `cgamex86.dll` | 339,968 bytes | 2000-07-31, MSVC 6.0 | .text: 253KB code |

The engine executable imports from 6 Win32 DLLs (195 unique functions) and dynamically loads OpenGL at runtime. All three binaries export a shared `str` C++ class (50 methods) as the ABI bridge.

## Building

### Prerequisites
- CMake 3.20+
- C17/C++20 compiler (MSVC 2022, GCC 13+, or Clang 17+)
- SDL2 development libraries
- OpenGL headers

### Build
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Running
Place the built `fakk2` executable alongside your original FAKK2 game files:
```
your_install/
    fakk2.exe          <-- our recomp binary
    fakk/
        pak0.pk3       <-- from your original game disc
        pak1.pk3
        pak2.pk3
        autoexec.cfg
        default.cfg
```

## Project Status

**Phase: Foundation** -- Project scaffolding, binary analysis, and subsystem stubs.

- [x] Binary analysis (PE headers, imports, exports)
- [x] Project structure and CMake build system
- [x] Type definitions and shared math
- [x] Engine core stubs (command, cvar, filesystem, network)
- [x] Platform abstraction layer (SDL2)
- [x] TIKI model system header and stubs
- [x] Morpheus scripting stubs
- [x] Ghost particle system stubs
- [x] Renderer stubs
- [x] Sound system stubs (Miles replacement)
- [ ] PK3 filesystem implementation
- [ ] TIKI file parser
- [ ] BSP loader (FAKK v12)
- [ ] Renderer implementation
- [ ] Sound mixing
- [ ] Game module interface (GetGameAPI / GetCGameAPI)
- [ ] Input processing
- [ ] Menu system
- [ ] Save/load system
- [ ] Full gameplay loop

## Related Projects

| Project | Description |
|---------|-------------|
| [fakk2-sdk](https://github.com/a1batross/fakk2-sdk) | Official FAKK2 SDK (game logic source), builds on modern systems |
| [fakk2-rework](https://github.com/Sporesirius/fakk2-rework) | Modernization effort (CMake, Vulkan goals) |
| [ioquake3](https://github.com/ioquake/ioq3) | Community id Tech 3 fork (base engine reference) |
| [sof-recomp](https://github.com/sp00nznet/sof) | Soldier of Fortune recomp (sister project, id Tech 2) |

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
