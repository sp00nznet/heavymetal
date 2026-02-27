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

/* Key event callback -- set by the key binding system */
static void (*key_event_callback)(int key, qboolean down, unsigned int time) = NULL;
static void (*char_event_callback)(int ch) = NULL;
static void (*mouse_move_callback)(int dx, int dy) = NULL;

void IN_SetKeyCallback(void (*callback)(int key, qboolean down, unsigned int time)) {
    key_event_callback = callback;
}

void IN_SetCharCallback(void (*callback)(int ch)) {
    char_event_callback = callback;
}

void IN_SetMouseMoveCallback(void (*callback)(int dx, int dy)) {
    mouse_move_callback = callback;
}

void IN_Init(void) {
    Com_Printf("Input: SDL2 input initialized\n");
#ifdef USE_SDL2
    SDL_SetRelativeMouseMode(SDL_TRUE);
#endif
}

void IN_Shutdown(void) {
#ifdef USE_SDL2
    SDL_SetRelativeMouseMode(SDL_FALSE);
#endif
}

void IN_Frame(void) {
    /* Input events are processed in Win_ProcessEvents */
}

void IN_Activate(qboolean active) {
#ifdef USE_SDL2
    SDL_SetRelativeMouseMode(active ? SDL_TRUE : SDL_FALSE);
#endif
    (void)active;
}

/* =========================================================================
 * SDL2 scancode to engine keyNum_t translation
 * ========================================================================= */

#ifdef USE_SDL2
static int SDL_ScancodeToKey(SDL_Scancode sc) {
    switch (sc) {
        case SDL_SCANCODE_TAB:          return K_TAB;
        case SDL_SCANCODE_RETURN:       return K_ENTER;
        case SDL_SCANCODE_ESCAPE:       return K_ESCAPE;
        case SDL_SCANCODE_SPACE:        return K_SPACE;
        case SDL_SCANCODE_BACKSPACE:    return K_BACKSPACE;
        case SDL_SCANCODE_UP:           return K_UPARROW;
        case SDL_SCANCODE_DOWN:         return K_DOWNARROW;
        case SDL_SCANCODE_LEFT:         return K_LEFTARROW;
        case SDL_SCANCODE_RIGHT:        return K_RIGHTARROW;
        case SDL_SCANCODE_LALT:
        case SDL_SCANCODE_RALT:         return K_ALT;
        case SDL_SCANCODE_LCTRL:
        case SDL_SCANCODE_RCTRL:        return K_CTRL;
        case SDL_SCANCODE_LSHIFT:
        case SDL_SCANCODE_RSHIFT:       return K_SHIFT;
        case SDL_SCANCODE_F1:           return K_F1;
        case SDL_SCANCODE_F2:           return K_F2;
        case SDL_SCANCODE_F3:           return K_F3;
        case SDL_SCANCODE_F4:           return K_F4;
        case SDL_SCANCODE_F5:           return K_F5;
        case SDL_SCANCODE_F6:           return K_F6;
        case SDL_SCANCODE_F7:           return K_F7;
        case SDL_SCANCODE_F8:           return K_F8;
        case SDL_SCANCODE_F9:           return K_F9;
        case SDL_SCANCODE_F10:          return K_F10;
        case SDL_SCANCODE_F11:          return K_F11;
        case SDL_SCANCODE_F12:          return K_F12;
        case SDL_SCANCODE_INSERT:       return K_INS;
        case SDL_SCANCODE_DELETE:       return K_DEL;
        case SDL_SCANCODE_PAGEDOWN:     return K_PGDN;
        case SDL_SCANCODE_PAGEUP:       return K_PGUP;
        case SDL_SCANCODE_HOME:         return K_HOME;
        case SDL_SCANCODE_END:          return K_END;
        case SDL_SCANCODE_PAUSE:        return K_PAUSE;
        case SDL_SCANCODE_GRAVE:        return '`';
        default:
            /* ASCII range keys */
            if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z)
                return 'a' + (sc - SDL_SCANCODE_A);
            if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9)
                return '1' + (sc - SDL_SCANCODE_1);
            if (sc == SDL_SCANCODE_0)
                return '0';
            if (sc == SDL_SCANCODE_MINUS)    return '-';
            if (sc == SDL_SCANCODE_EQUALS)   return '=';
            if (sc == SDL_SCANCODE_LEFTBRACKET)  return '[';
            if (sc == SDL_SCANCODE_RIGHTBRACKET) return ']';
            if (sc == SDL_SCANCODE_BACKSLASH)    return '\\';
            if (sc == SDL_SCANCODE_SEMICOLON)    return ';';
            if (sc == SDL_SCANCODE_APOSTROPHE)   return '\'';
            if (sc == SDL_SCANCODE_COMMA)        return ',';
            if (sc == SDL_SCANCODE_PERIOD)       return '.';
            if (sc == SDL_SCANCODE_SLASH)        return '/';
            return 0;
    }
}

static int SDL_MouseButtonToKey(int button) {
    switch (button) {
        case SDL_BUTTON_LEFT:   return K_MOUSE1;
        case SDL_BUTTON_RIGHT:  return K_MOUSE2;
        case SDL_BUTTON_MIDDLE: return K_MOUSE3;
        case SDL_BUTTON_X1:     return K_MOUSE4;
        case SDL_BUTTON_X2:     return K_MOUSE5;
        default:                return 0;
    }
}
#endif /* USE_SDL2 */

/* =========================================================================
 * Window event processing
 *
 * Replaces the Win32 message pump (PeekMessageA/DispatchMessageA).
 * Translates SDL2 events into engine key/mouse events.
 * Returns qfalse if the application should quit.
 * ========================================================================= */

qboolean Win_ProcessEvents(void) {
#ifdef USE_SDL2
    SDL_Event event;
    unsigned int time = (unsigned int)Sys_Milliseconds();

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                return qfalse;

            case SDL_WINDOWEVENT:
                switch (event.window.event) {
                    case SDL_WINDOWEVENT_MINIMIZED:
                        fakk_window.minimized = qtrue;
                        fakk_window.active = qfalse;
                        break;
                    case SDL_WINDOWEVENT_RESTORED:
                        fakk_window.minimized = qfalse;
                        fakk_window.active = qtrue;
                        break;
                    case SDL_WINDOWEVENT_FOCUS_GAINED:
                        fakk_window.active = qtrue;
                        IN_Activate(qtrue);
                        break;
                    case SDL_WINDOWEVENT_FOCUS_LOST:
                        fakk_window.active = qfalse;
                        IN_Activate(qfalse);
                        break;
                    case SDL_WINDOWEVENT_SIZE_CHANGED:
                        fakk_window.width = event.window.data1;
                        fakk_window.height = event.window.data2;
                        break;
                }
                break;

            case SDL_KEYDOWN:
            case SDL_KEYUP: {
                int key = SDL_ScancodeToKey(event.key.keysym.scancode);
                qboolean down = (event.type == SDL_KEYDOWN) ? qtrue : qfalse;
                if (key && key_event_callback)
                    key_event_callback(key, down, time);
                break;
            }

            case SDL_TEXTINPUT: {
                /* Feed printable characters for console input */
                if (char_event_callback) {
                    for (int i = 0; event.text.text[i]; i++) {
                        unsigned char ch = (unsigned char)event.text.text[i];
                        if (ch >= 32 && ch < 127)
                            char_event_callback(ch);
                    }
                }
                break;
            }

            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP: {
                int key = SDL_MouseButtonToKey(event.button.button);
                qboolean down = (event.type == SDL_MOUSEBUTTONDOWN) ? qtrue : qfalse;
                if (key && key_event_callback)
                    key_event_callback(key, down, time);
                break;
            }

            case SDL_MOUSEWHEEL:
                if (key_event_callback) {
                    if (event.wheel.y > 0) {
                        key_event_callback(K_MWHEELUP, qtrue, time);
                        key_event_callback(K_MWHEELUP, qfalse, time);
                    } else if (event.wheel.y < 0) {
                        key_event_callback(K_MWHEELDOWN, qtrue, time);
                        key_event_callback(K_MWHEELDOWN, qfalse, time);
                    }
                }
                break;

            case SDL_MOUSEMOTION:
                if (mouse_move_callback && fakk_window.active)
                    mouse_move_callback(event.motion.xrel, event.motion.yrel);
                break;
        }
    }
#endif
    return qtrue;
}

/* =========================================================================
 * Gamma control
 *
 * Original used SetDeviceGammaRamp/GetDeviceGammaRamp (GDI32.DLL).
 * SDL2 provides SDL_SetWindowBrightness as a simple alternative.
 * ========================================================================= */

void Win_SetGamma(float gamma) {
#ifdef USE_SDL2
    if (fakk_window.window) {
        SDL_SetWindowBrightness(fakk_window.window, gamma);
    }
#else
    (void)gamma;
#endif
}

void Win_GetGamma(float *gamma) {
#ifdef USE_SDL2
    if (fakk_window.window && gamma) {
        *gamma = SDL_GetWindowBrightness(fakk_window.window);
        return;
    }
#endif
    if (gamma) *gamma = 1.0f;
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
