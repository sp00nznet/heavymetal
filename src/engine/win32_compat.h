/*
 * win32_compat.h -- Windows API compatibility layer for FAKK2 recomp
 *
 * The original fakk2.exe imports from 6 DLLs:
 *   KERNEL32.DLL  (97 functions)  -- Core OS services
 *   USER32.DLL    (50 functions)  -- Window management, input
 *   GDI32.DLL     (14 functions)  -- Graphics device, pixel formats
 *   WINMM.DLL    (11 functions)  -- Joystick, MIDI, timers
 *   WSOCK32.DLL   (17 functions)  -- UDP/TCP networking
 *   ADVAPI32.DLL  (6 functions)   -- Registry, user info
 *
 * In the recomp, we replace most of these with SDL2 equivalents.
 * This header provides the mapping layer.
 *
 * Total: 195 unique Win32 API functions to handle.
 */

#ifndef WIN32_COMPAT_H
#define WIN32_COMPAT_H

#include "../common/fakk_types.h"

#ifdef PLATFORM_WINDOWS
    /* On Windows, we can use native APIs directly where beneficial */
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #pragma comment(lib, "winmm.lib")
#else
    /* On non-Windows, provide compatibility typedefs */
    typedef void *HWND;
    typedef void *HDC;
    typedef void *HINSTANCE;
    typedef void *HGLRC;
    typedef unsigned long DWORD;
    typedef unsigned short WORD;
    typedef int BOOL;
    #define TRUE  1
    #define FALSE 0
#endif

/* =========================================================================
 * SDL2 platform abstraction
 *
 * These functions replace the Win32 API calls. The original imports
 * are documented here for reference during reverse engineering.
 * ========================================================================= */

#ifdef USE_SDL2
#include <SDL2/SDL.h>
#endif

/* =========================================================================
 * Window management (replaces USER32.DLL)
 *
 * Original imports:
 *   CreateWindowExA, RegisterClassA, DefWindowProcA, DestroyWindow
 *   ShowWindow, UpdateWindow, SetForegroundWindow, SetFocus
 *   PeekMessageA, GetMessageA, DispatchMessageA, TranslateMessage
 *   ChangeDisplaySettingsA, EnumDisplaySettingsA
 *   GetSystemMetrics, AdjustWindowRectEx
 *   SetCapture, ReleaseCapture, ClipCursor, SetCursorPos
 *   GetCursorPos, ShowCursor, SetCursor, LoadCursorA
 *   RegisterHotKey
 * ========================================================================= */

typedef struct {
#ifdef USE_SDL2
    SDL_Window      *window;
    SDL_GLContext    gl_context;
#endif
#ifdef PLATFORM_WINDOWS
    HWND            hWnd;
    HDC             hDC;
    HGLRC           hGLRC;
    HINSTANCE       hInstance;
#endif
    int             width;
    int             height;
    int             fullscreen;
    int             hz;             /* refresh rate */
    qboolean        active;
    qboolean        minimized;
} fakk_window_t;

extern fakk_window_t fakk_window;

qboolean    Win_Create(int width, int height, qboolean fullscreen);
void        Win_Destroy(void);
void        Win_SetTitle(const char *title);
void        Win_SwapBuffers(void);
void        Win_SetGamma(float gamma);
void        Win_GetGamma(float *gamma);
qboolean    Win_ProcessEvents(void);

/* =========================================================================
 * OpenGL context (replaces GDI32.DLL pixel format functions)
 *
 * Original imports:
 *   DescribePixelFormat, SetPixelFormat, SwapBuffers, ChoosePixelFormat
 *   GetDeviceGammaRamp, SetDeviceGammaRamp
 * ========================================================================= */

qboolean    GLimp_Init(void);
void        GLimp_Shutdown(void);
void        *GLimp_GetProcAddress(const char *name);

/* =========================================================================
 * Input (replaces USER32.DLL input + WINMM.DLL joystick)
 *
 * USER32 originals:
 *   GetAsyncKeyState, MapVirtualKeyA, GetKeyState
 *
 * WINMM originals:
 *   joyGetDevCapsA, joyGetNumDevs, joyGetPosEx
 *   midiInOpen, midiInStart, midiInClose, midiInGetDevCapsA, midiInGetNumDevs
 * ========================================================================= */

void        IN_Init(void);
void        IN_Shutdown(void);
void        IN_Frame(void);
void        IN_Activate(qboolean active);

/* Input callbacks -- set by client to receive events from Win_ProcessEvents */
void        IN_SetKeyCallback(void (*callback)(int key, qboolean down, unsigned int time));
void        IN_SetCharCallback(void (*callback)(int ch));
void        IN_SetMouseMoveCallback(void (*callback)(int dx, int dy));

/* =========================================================================
 * Timing (replaces WINMM.DLL timers + KERNEL32 timing)
 *
 * Original imports:
 *   timeBeginPeriod, timeEndPeriod, timeGetTime
 *   QueryPerformanceCounter, QueryPerformanceFrequency
 *   GetTickCount, Sleep
 * ========================================================================= */

void        Time_Init(void);
int         Time_Milliseconds(void);
qword       Time_Microseconds(void);

/* =========================================================================
 * Networking (replaces WSOCK32.DLL)
 *
 * Original imports (17 functions):
 *   WSAStartup, WSAGetLastError
 *   socket, bind, connect, closesocket
 *   send, sendto, recv, recvfrom
 *   setsockopt, ioctlsocket
 *   htons, ntohl, ntohs
 *   gethostbyname, gethostname, inet_addr
 * ========================================================================= */

/* Network implementation uses native sockets on all platforms */
/* See src/engine/net.c */

/* =========================================================================
 * Registry (replaces ADVAPI32.DLL)
 *
 * Original imports:
 *   RegCreateKeyA, RegOpenKeyA, RegCloseKey
 *   RegQueryValueExA, RegSetValueExA
 *   GetUserNameA
 *
 * In the recomp, registry access is replaced with config file storage.
 * The original used registry for CD key and install path.
 * ========================================================================= */

const char  *Reg_GetInstallPath(void);
const char  *Reg_GetCDKey(void);
const char  *Reg_GetUserName(void);

/* =========================================================================
 * File system helpers (replaces some KERNEL32.DLL file ops)
 *
 * Original imports:
 *   CreateFileA, ReadFile, WriteFile, CloseHandle
 *   FindFirstFileA, FindNextFileA, FindClose
 *   GetFileAttributesA, SetFileAttributesA
 *   DeleteFileA, MoveFileA
 *   CreateDirectoryA, RemoveDirectoryA
 *   GetCurrentDirectoryA, SetCurrentDirectoryA
 *   GetFullPathNameA, GetTempPathA
 * ========================================================================= */

/* These are handled through the FS_ functions in qcommon.h */
/* The platform layer in sys_sdl.c provides OS-specific implementations */

#endif /* WIN32_COMPAT_H */
