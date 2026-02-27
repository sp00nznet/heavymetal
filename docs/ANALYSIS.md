# Binary Analysis

## PE Header Summary

### fakk2.exe
```
Machine:            Intel 386 (32-bit)
Compile timestamp:  2000-08-22 16:14:06 UTC
Linker version:     6.0 (MSVC 6.0)
Subsystem:          Windows GUI
Image base:         0x00400000
Entry point:        0x004C4C73
Characteristics:    RELOCS_STRIPPED, EXECUTABLE_IMAGE, 32BIT_MACHINE

DLL Characteristics: 0x0000 (NONE)
  - No ASLR (DYNAMIC_BASE not set)
  - No DEP/NX (NX_COMPAT not set)
  - No SafeSEH
  - No CFG

Rich Header:
  MSVC 6.0 C++ Compiler (build 7299)
  Linker 6.12 (build 8168)

Version Info:
  CompanyName:      Ritual Entertainment
  ProductName:      Heavy Metal : Fakk 2
  FileVersion:      1, 0, 0, 1
  LegalCopyright:   Copyright (C) 2000
```

### Section Table

#### fakk2.exe
| Section | Virtual Size | Raw Size | Characteristics |
|---------|-------------|----------|-----------------|
| .text   | 886,816     | 886,784  | CODE, EXECUTE, READ |
| .rdata  | 34,988      | 35,328   | INITIALIZED_DATA, READ |
| .data   | 8,848,928   | 139,264  | INITIALIZED_DATA, READ, WRITE |
| .rsrc   | 266,624     | 266,752  | INITIALIZED_DATA, READ |

#### gamex86.dll
| Section | Virtual Size | Raw Size | Characteristics |
|---------|-------------|----------|-----------------|
| .text   | 1,363,984   | 1,363,968 | CODE, EXECUTE, READ |
| .rdata  | 89,564      | 89,600   | INITIALIZED_DATA, READ |
| .data   | 1,448,224   | 147,456  | INITIALIZED_DATA, READ, WRITE |
| .reloc  | 113,684     | 113,664  | INITIALIZED_DATA, DISCARDABLE, READ |

#### cgamex86.dll
| Section | Virtual Size | Raw Size | Characteristics |
|---------|-------------|----------|-----------------|
| .text   | 259,716     | 259,584  | CODE, EXECUTE, READ |
| .rdata  | 11,828      | 12,288   | INITIALIZED_DATA, READ |
| .data   | 3,244,032   | 33,280   | INITIALIZED_DATA, READ, WRITE |
| .reloc  | 27,756      | 27,648   | INITIALIZED_DATA, DISCARDABLE, READ |

## Full Import Table

### KERNEL32.DLL (97 unique functions)

**Used by all 3 binaries (CRT runtime):**
CloseHandle, CompareStringA, CompareStringW, CreateFileA, CreateMutexA,
DeleteCriticalSection, EnterCriticalSection, ExitProcess, FreeLibrary,
GetACP, GetCPInfo, GetCommandLineA, GetCurrentProcess, GetCurrentProcessId,
GetCurrentThread, GetCurrentThreadId, GetEnvironmentVariableA, GetFileType,
GetLastError, GetLocaleInfoA, GetModuleFileNameA, GetModuleHandleA, GetOEMCP,
GetProcAddress, GetStartupInfoA, GetStdHandle, GetStringTypeA, GetStringTypeW,
GetSystemTimeAsFileTime, GetTickCount, GetVersion, HeapAlloc, HeapFree,
HeapReAlloc, HeapSize, InitializeCriticalSection, InterlockedDecrement,
InterlockedIncrement, IsBadReadPtr, IsBadWritePtr, LCMapStringA, LCMapStringW,
LeaveCriticalSection, LoadLibraryA, MultiByteToWideChar, OutputDebugStringA,
QueryPerformanceCounter, RaiseException, ReadFile, RtlUnwind, SetFilePointer,
SetHandleCount, SetLastError, SetStdHandle, Sleep, TerminateProcess,
TlsAlloc, TlsFree, TlsGetValue, TlsSetValue, UnhandledExceptionFilter,
VirtualAlloc, VirtualFree, WideCharToMultiByte, WriteFile, lstrlenA

**EXE-only additions (file/directory/threading):**
CopyFileA, CreateDirectoryA, CreateEventA, CreateThread,
DeleteFileA, FindClose, FindFirstFileA, FindNextFileA,
FormatMessageA, GetCurrentDirectoryA, GetDiskFreeSpaceA,
GetDriveTypeA, GetFileAttributesA, GetFullPathNameA,
GetLogicalDriveStrings, GetPrivateProfileStringA, GetSystemDirectoryA,
GetTempPathA, GetWindowsDirectoryA, MoveFileA, QueryPerformanceFrequency,
RemoveDirectoryA, ResetEvent, ResumeThread, SearchPathA,
SetCurrentDirectoryA, SetEndOfFile, SetErrorMode, SetEvent,
WaitForSingleObject, _hread, _lclose, _lopen, _lread, lstrcpyA

### USER32.DLL (50 functions, fakk2.exe only)
AdjustWindowRectEx, BeginPaint, CallWindowProcA, ChangeDisplaySettingsA,
CharLowerA, CharUpperA, ClientToScreen, ClipCursor, CreateWindowExA,
DefWindowProcA, DestroyWindow, DispatchMessageA, EnableWindow,
EndPaint, EnumDisplaySettingsA, GetActiveWindow, GetAsyncKeyState,
GetCapture, GetClientRect, GetCursorPos, GetDC, GetForegroundWindow,
GetKeyState, GetMessageA, GetSystemMetrics, GetWindowRect, InvalidateRect,
IsIconic, KillTimer, LoadCursorA, LoadIconA, MapVirtualKeyA,
MessageBoxA, MoveWindow, PeekMessageA, PostMessageA, PostQuitMessage,
RegisterClassA, RegisterHotKey, ReleaseCapture, ReleaseDC,
ScreenToClient, SendMessageA, SetCapture, SetCursor, SetCursorPos,
SetFocus, SetForegroundWindow, SetTimer, ShowCursor, ShowWindow,
TranslateMessage, UnregisterClassA, UpdateWindow

### GDI32.DLL (14 functions, fakk2.exe only)
CreateCompatibleDC, CreateFontA, CreateSolidBrush, DeleteDC,
DeleteObject, DescribePixelFormat, GetDeviceGammaRamp,
GetStockObject, SelectObject, SetBkColor, SetPixelFormat,
SetTextColor, StretchBlt, SwapBuffers

### WINMM.DLL (11 functions, fakk2.exe only)
joyGetDevCapsA, joyGetNumDevs, joyGetPosEx,
midiInClose, midiInGetDevCapsA, midiInGetNumDevs, midiInOpen, midiInStart,
timeBeginPeriod, timeEndPeriod, timeGetTime

### WSOCK32.DLL (17 functions, fakk2.exe only)
WSAGetLastError, WSAStartup,
bind, closesocket, connect, gethostbyname, gethostname,
htons, inet_addr, ioctlsocket, ntohl, ntohs,
recv, recvfrom, send, sendto, setsockopt, socket

### ADVAPI32.DLL (6 functions, fakk2.exe only)
GetUserNameA, RegCloseKey, RegCreateKeyA,
RegOpenKeyA, RegQueryValueExA, RegSetValueExA

## Export Table

### All binaries export the `str` class (50 symbols):
C++ mangled names for: constructor, destructor, operator=, operator+,
operator+=, operator[], operator==, operator!=, append, c_str, length,
cmp, cmpn, icmp, icmpn, tolower, toupper, snprintf, isNumeric,
capLength, strip, and related overloads.

### gamex86.dll additional export:
- **`GetGameAPI`** (ordinal 29, RVA 0x000837D0) -- game module entry point

### cgamex86.dll additional export:
- **`GetCGameAPI`** (ordinal 51, RVA 0x00022390) -- client game entry point

## Key Addresses (for reverse engineering reference)

| Symbol | Binary | Address | Notes |
|--------|--------|---------|-------|
| WinMain (entry) | fakk2.exe | 0x004C4C73 | Application entry point |
| GetGameAPI | gamex86.dll | 0x100837D0 | Game module init |
| GetCGameAPI | cgamex86.dll | 0x30022390 | Client game init |

## Observations

1. **No debug info** -- All binaries stripped, no PDB references
2. **No OpenGL static imports** -- GL loaded dynamically (standard for Q3)
3. **Massive BSS** -- gamex86.dll 1.4MB, cgamex86.dll 3.1MB uninitialized
4. **MSVC 6.0 CRT** -- All three link against the same MSVC 6.0 runtime
5. **Shared str class** -- All three export identical str class, this is the ABI bridge
