# Technical Overview

## Engine Lineage

Heavy Metal: FAKK2 runs on **id Tech 3** (Quake III Arena engine), heavily modified by Ritual Entertainment with their **UberTools** framework. Ritual obtained a limited Quake 3 license with an early code snapshot in February 1999, before Q3A shipped publicly.

```
id Tech 3 (id Software, 1999)
    |
    +-- Ritual's UberTools modifications
    |       |
    |       +-- TIKI model system (from SiN's DEF system)
    |       +-- Morpheus scripting (~700 commands, from SiN's ~500)
    |       +-- Ghost particle system (written from scratch)
    |       +-- Babble dialogue/lip-sync
    |       +-- Dynamic music system
    |       +-- Cinematic camera system
    |       +-- Enhanced shaders, lens flares, sky portals
    |       +-- Miles Sound System integration
    |       +-- Third-person gameplay systems
    |       +-- Data-driven content (TIKI text files)
    |
    +-- Heavy Metal: FAKK2 (August 2000)
    +-- American McGee's Alice (December 2000, EA licensed FAKK2 engine)
    +-- Medal of Honor: Allied Assault (January 2002, 2015 Inc.)
    +-- Star Trek: Elite Force II (June 2003, Ritual)
```

## Why the Original Doesn't Run on Windows 10/11

### 1. Address Space Layout Randomization (ASLR)
The original `fakk2.exe` has **relocations stripped** (`IMAGE_FILE_RELOCS_STRIPPED` flag) and must load at its preferred base address `0x00400000`. Modern Windows with ASLR may assign a different base address, causing immediate crashes. The recomp produces a fully relocatable 64-bit binary.

### 2. Legacy OpenGL
The engine uses OpenGL 1.x immediate-mode rendering calls. Modern GPU drivers have dropped or poorly support this legacy path. Our renderer targets OpenGL 4.x core profile.

### 3. Timing Precision
The original uses `timeGetTime()` from WINMM.DLL with `timeBeginPeriod(1)`. Windows 10/11 changed timer resolution behavior, causing frame timing issues and physics instability. SDL2's high-resolution timer solves this.

### 4. No DEP/NX Support
The original has no NX-compatible exception handling (`IMAGE_DLLCHARACTERISTICS_NX_COMPAT` not set). Modern Windows enforces DEP by default. The recomp is fully DEP-compatible.

### 5. CD-ROM Check
The original validates that the game disc is inserted. This is incompatible with modern systems that lack optical drives.

### 6. Registry Dependency
Install path and CD key are stored in the Windows registry. The recomp uses config files instead.

### 7. DirectInput
Mouse/keyboard input uses DirectInput, which is deprecated. SDL2 provides the replacement.

### 8. Miles Sound System
The commercial audio middleware may have compatibility issues with modern audio drivers. SDL2 audio replaces it.

## Binary Architecture

### fakk2.exe (Engine)
- **Type**: 32-bit PE32 executable, Windows GUI subsystem
- **Compiler**: Microsoft Visual C++ 6.0 (Linker 6.12, build 8168)
- **Size**: 1,318,912 bytes
- **Code section**: 866 KB (.text)
- **Data**: 8.4 MB virtual (136 KB initialized) -- large static game state
- **Resources**: 260 KB (splash bitmap, icon, version info)
- **Entry point**: `0x004C4C73`
- **Base address**: `0x00400000` (fixed, relocations stripped)
- **Imports**: 6 DLLs, 195 unique functions

### gamex86.dll (Server Game Logic)
- **Type**: 32-bit PE32 DLL
- **Size**: 1,740,800 bytes
- **Code**: 1.3 MB (largest code section)
- **BSS**: 1.4 MB uninitialized data (entity tables, AI state)
- **Base**: `0x10000000` (relocatable, 47,436 relocations)
- **Export**: `GetGameAPI` (+ 50 `str` class methods)
- **SDK source available**: ~131,000 lines across 167 files

### cgamex86.dll (Client Game Logic)
- **Type**: 32-bit PE32 DLL
- **Size**: 339,968 bytes
- **Code**: 253 KB
- **BSS**: 3.1 MB uninitialized data
- **Base**: `0x30000000` (relocatable, 8,126 relocations)
- **Export**: `GetCGameAPI` (+ 50 `str` class methods)
- **SDK source available**: ~21,500 lines across 31 files

## Import Analysis

### KERNEL32.DLL (97 functions)
Core OS: memory management, file I/O, threading, process management. The DLLs use only CRT runtime support functions (~61 each); the EXE adds file operations, threading, and directory management.

### USER32.DLL (50 functions)
Window management, message pump, input, clipboard, display mode switching. Replaced by SDL2.

### GDI32.DLL (14 functions)
OpenGL pixel format setup (`DescribePixelFormat`, `SetPixelFormat`, `SwapBuffers`), gamma ramp control, font rendering, splash screen. Replaced by SDL2 + modern GL.

### WINMM.DLL (11 functions)
Joystick input (`joyGetDevCapsA`, `joyGetPosEx`), MIDI input (5 functions for custom controllers), timing (`timeGetTime`, `timeBeginPeriod`). Joystick via SDL2, MIDI input via SDL2 or removed, timing via SDL2.

### WSOCK32.DLL (17 functions)
Full UDP/TCP networking: socket, bind, connect, send/recv, DNS. Uses native Winsock2/BSD sockets in recomp.

### ADVAPI32.DLL (6 functions)
Windows registry (5 functions) and `GetUserNameA`. Registry replaced with config files.

### OpenGL (loaded dynamically)
Not in import table -- loaded via `LoadLibraryA`/`GetProcAddress` at runtime. Standard for Q3-engine games. The recomp uses SDL2's GL loader.

## Module Communication

```
             GetGameAPI()                GetCGameAPI()
fakk2.exe  ===============>  gamex86.dll  |  cgamex86.dll
           <===============               |
           game_import_t     game_export_t | clientGameImport_t
           (~60+ functions)  (~20 funcs)  | (~90+ functions)
                                          | clientGameExport_t
                                          | (6 functions)
```

All three binaries share a common `str` C++ class (50 exported symbols), which serves as the primary ABI contract.

## Key UberTools Subsystems

### TIKI Model System
- Text-based model definitions (.tik) with binary cache (.cik)
- Skeletal animation (up to 128 bones, 16 concurrent animation channels)
- Frame-synchronized events (sounds, particles, scripted actions)
- LOD support
- Surface material system for gameplay effects

### Morpheus Scripting
- ~700 commands for entity manipulation, AI, camera, cinematics
- Embedded in TIKI files, BSP entities, and standalone scripts
- Variable/conditional logic, wait/delay, concurrent threads
- Full control over any entity in the game world

### Ghost Particle System
- ~50 parameters per emitter
- Composite effects (multiple emitters per visual effect)
- Physics-driven particles (gravity, wind, collision)
- Tag-based attachment to model bones
- Sprite, model, beam, and decal particles

### Miles Sound System (replaced in recomp)
- 3D positional audio
- Dynamic music with mood/intensity control
- Ambient sound sets
- Babble dialogue lip-sync integration
- Streaming music playback
