/*
 * sys_sdl.c -- Platform layer using SDL2
 *
 * Replaces the original Win32 platform code (WinMain, window creation,
 * input handling, timing). The original imported 195 functions from
 * 6 Win32 DLLs. SDL2 replaces most of them with a single cross-platform API.
 */

#include "qcommon.h"
#include "win32_compat.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

/* =========================================================================
 * Global window state
 * ========================================================================= */

fakk_window_t fakk_window;

/* =========================================================================
 * System initialization
 * ========================================================================= */

void Sys_Init(void) {
#ifdef USE_SDL2
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        exit(1);
    }

    /* Request OpenGL 4.x context */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
#endif

    Time_Init();

    Com_Printf("Platform: SDL2 initialized\n");
}

/* =========================================================================
 * Timing
 *
 * Original used:
 *   timeGetTime()          -- millisecond timer (WINMM.DLL)
 *   QueryPerformanceCounter/Frequency -- high-res timer (KERNEL32.DLL)
 *   timeBeginPeriod(1)     -- set timer resolution
 * ========================================================================= */

static qword time_base = 0;

void Time_Init(void) {
#ifdef USE_SDL2
    time_base = SDL_GetPerformanceCounter();
#endif
}

int Time_Milliseconds(void) {
#ifdef USE_SDL2
    return (int)SDL_GetTicks();
#else
    return (int)(clock() * 1000 / CLOCKS_PER_SEC);
#endif
}

qword Time_Microseconds(void) {
#ifdef USE_SDL2
    qword now = SDL_GetPerformanceCounter();
    qword freq = SDL_GetPerformanceFrequency();
    return ((now - time_base) * 1000000) / freq;
#else
    return (qword)Time_Milliseconds() * 1000;
#endif
}

int Sys_Milliseconds(void) {
    return Time_Milliseconds();
}

/* =========================================================================
 * Window creation
 *
 * Original used:
 *   RegisterClassA, CreateWindowExA (USER32.DLL)
 *   DescribePixelFormat, SetPixelFormat (GDI32.DLL)
 *   wglCreateContext, wglMakeCurrent (OPENGL32.DLL via GetProcAddress)
 * ========================================================================= */

qboolean Win_Create(int width, int height, qboolean fullscreen) {
#ifdef USE_SDL2
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN;
    if (fullscreen) flags |= SDL_WINDOW_FULLSCREEN;

    fakk_window.window = SDL_CreateWindow(
        "Heavy Metal: FAKK2",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height, flags
    );

    if (!fakk_window.window) {
        Com_Printf("Win_Create: SDL_CreateWindow failed: %s\n", SDL_GetError());
        return qfalse;
    }

    fakk_window.gl_context = SDL_GL_CreateContext(fakk_window.window);
    if (!fakk_window.gl_context) {
        Com_Printf("Win_Create: SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(fakk_window.window);
        return qfalse;
    }

    /* VSync -- original used wglSwapIntervalEXT */
    SDL_GL_SetSwapInterval(1);

    fakk_window.width = width;
    fakk_window.height = height;
    fakk_window.fullscreen = fullscreen;
    fakk_window.active = qtrue;
#endif

    Com_Printf("Window created: %dx%d %s\n", width, height,
               fullscreen ? "fullscreen" : "windowed");
    return qtrue;
}

void Win_Destroy(void) {
#ifdef USE_SDL2
    if (fakk_window.gl_context) {
        SDL_GL_DeleteContext(fakk_window.gl_context);
        fakk_window.gl_context = NULL;
    }
    if (fakk_window.window) {
        SDL_DestroyWindow(fakk_window.window);
        fakk_window.window = NULL;
    }
#endif
    fakk_window.active = qfalse;
}

void Win_SwapBuffers(void) {
#ifdef USE_SDL2
    if (fakk_window.window) {
        SDL_GL_SwapWindow(fakk_window.window);
    }
#endif
}

void Win_SetTitle(const char *title) {
#ifdef USE_SDL2
    if (fakk_window.window) {
        SDL_SetWindowTitle(fakk_window.window, title);
    }
#endif
}

/* =========================================================================
 * System functions
 * ========================================================================= */

void Sys_Print(const char *msg) {
    fputs(msg, stdout);
    fflush(stdout);
}

void Sys_Quit(void) {
#ifdef USE_SDL2
    SDL_Quit();
#endif
    exit(0);
}

void Sys_Error(const char *error, ...) {
    va_list args;
    char    msg[4096];

    va_start(args, error);
    vsnprintf(msg, sizeof(msg), error, args);
    va_end(args);

    fprintf(stderr, "FATAL ERROR: %s\n", msg);

#ifdef USE_SDL2
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "FAKK2 Fatal Error", msg, NULL);
    SDL_Quit();
#endif

    exit(1);
}

void Sys_Mkdir(const char *path) {
#ifdef PLATFORM_WINDOWS
    CreateDirectoryA(path, NULL);
#else
    mkdir(path, 0777);
#endif
}

char *Sys_Cwd(void) {
    static char cwd[MAX_OSPATH];
#ifdef PLATFORM_WINDOWS
    GetCurrentDirectoryA(sizeof(cwd), cwd);
#else
    getcwd(cwd, sizeof(cwd));
#endif
    return cwd;
}

char *Sys_DefaultBasePath(void) {
    return Sys_Cwd();
}

char *Sys_GetClipboardData(void) {
#ifdef USE_SDL2
    return SDL_GetClipboardText();
#else
    return NULL;
#endif
}

void Sys_SetClipboardData(const char *data) {
#ifdef USE_SDL2
    SDL_SetClipboardText(data);
#endif
}

/* =========================================================================
 * Input handling
 *
 * Original used:
 *   PeekMessageA, DispatchMessageA (USER32.DLL) for Windows messages
 *   DirectInput for mouse/keyboard (loaded dynamically)
 *   joyGetPosEx (WINMM.DLL) for joystick
 *   midiIn* (WINMM.DLL) for MIDI input (used for custom controllers)
 * ========================================================================= */

void IN_Init(void) {
    Com_Printf("Input: SDL2 input initialized\n");
    /* TODO: SDL2 relative mouse, keyboard state */
}

void IN_Shutdown(void) {
    /* Nothing to clean up with SDL2 input */
}

void IN_Frame(void) {
    /* TODO: Process SDL2 input events and feed to engine */
}

void IN_Activate(qboolean active) {
#ifdef USE_SDL2
    SDL_SetRelativeMouseMode(active ? SDL_TRUE : SDL_FALSE);
#endif
    (void)active;
}

/* =========================================================================
 * OpenGL loading
 *
 * Original loaded opengl32.dll dynamically via LoadLibraryA/GetProcAddress.
 * The recomp uses SDL2's GL loader.
 * ========================================================================= */

qboolean GLimp_Init(void) {
    /* GL context is created in Win_Create via SDL2 */
    Com_Printf("GLimp: OpenGL context ready\n");
    return qtrue;
}

void GLimp_Shutdown(void) {
    Win_Destroy();
}

void *GLimp_GetProcAddress(const char *name) {
#ifdef USE_SDL2
    return SDL_GL_GetProcAddress(name);
#else
    return NULL;
#endif
}

/* =========================================================================
 * Registry replacement
 *
 * Original used ADVAPI32.DLL:
 *   RegCreateKeyA, RegOpenKeyA, RegCloseKey
 *   RegQueryValueExA, RegSetValueExA
 *   GetUserNameA
 *
 * In the recomp, we don't need registry. Install path is CWD,
 * CD key is not needed, user name comes from OS.
 * ========================================================================= */

const char *Reg_GetInstallPath(void) {
    return Sys_Cwd();
}

const char *Reg_GetCDKey(void) {
    return "";  /* CD key validation not needed in 2025 */
}

const char *Reg_GetUserName(void) {
    static char username[256] = "";
    if (!username[0]) {
#ifdef PLATFORM_WINDOWS
        DWORD size = sizeof(username);
        GetUserNameA(username, &size);
#else
        const char *env = getenv("USER");
        if (env) strncpy(username, env, sizeof(username) - 1);
        else strcpy(username, "Player");
#endif
    }
    return username;
}

/* =========================================================================
 * DLL loading (for game modules)
 *
 * Original loaded gamex86.dll and cgamex86.dll via LoadLibraryA.
 * In the unified recomp binary, we may link statically or keep DLL support.
 * ========================================================================= */

void *Sys_LoadDll(const char *name) {
#ifdef PLATFORM_WINDOWS
    return (void *)LoadLibraryA(name);
#elif defined(USE_SDL2)
    return SDL_LoadObject(name);
#else
    return NULL;
#endif
}

void *Sys_GetProcAddress(void *handle, const char *name) {
#ifdef PLATFORM_WINDOWS
    return (void *)GetProcAddress((HMODULE)handle, name);
#elif defined(USE_SDL2)
    return SDL_LoadFunction(handle, name);
#else
    return NULL;
#endif
}

void Sys_UnloadDll(void *handle) {
    if (!handle) return;
#ifdef PLATFORM_WINDOWS
    FreeLibrary((HMODULE)handle);
#elif defined(USE_SDL2)
    SDL_UnloadObject(handle);
#endif
}
