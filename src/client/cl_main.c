/*
 * cl_main.c -- Client main loop
 *
 * The client handles rendering, input, sound, and communication
 * with the server. In FAKK2, the client is always running (no
 * dedicated server mode was common for this single-player focused game).
 *
 * Client state machine:
 *   CA_DISCONNECTED -> CA_CONNECTING -> CA_CONNECTED -> CA_LOADING -> CA_ACTIVE
 *
 * The client's frame consists of:
 *   1. Process window events (SDL2 pump)
 *   2. Process input (mouse/keyboard)
 *   3. Send user commands to server
 *   4. Receive and process snapshots from server
 *   5. Call cgame to render the scene
 *   6. Update sound listener position
 *   7. Swap buffers
 */

#include "../common/qcommon.h"
#include "../renderer/tr_types.h"
#include "../sound/snd_local.h"
#include "../engine/win32_compat.h"

/* Forward declarations from renderer */
extern void R_Init(void);
extern void R_Shutdown(void);
extern void R_BeginFrame(void);
extern void R_EndFrame(void);
extern void R_ClearScene(void);
extern void R_RenderScene(const refdef_t *fd);
extern void R_GetGlconfig(glconfig_t *config);

/* Forward declarations from keys.c */
extern void Key_Init(void);
extern void Key_Shutdown(void);
extern void Key_KeyEvent(int key, qboolean down, unsigned int time);
extern void Key_CharEvent(int ch);

/* Forward declarations from console.c */
extern void Con_Init(void);
extern void Con_Shutdown(void);
extern void Con_DrawConsole(void);

/* Forward declarations from cl_cgame.c */
extern void CL_InitCGame(void);
extern void CL_ShutdownCGame(void);

/* Forward declarations from cl_input.c */
extern void CL_InitInput(void);
extern void CL_ShutdownInput(void);

/* =========================================================================
 * Client state
 * ========================================================================= */

typedef enum {
    CA_DISCONNECTED,
    CA_CONNECTING,
    CA_CONNECTED,
    CA_LOADING,
    CA_PRIMED,
    CA_ACTIVE
} connstate_t;

typedef struct {
    connstate_t     state;
    int             clientNum;
    int             serverTime;
    int             oldServerTime;
    int             framecount;

    /* Rendering */
    glconfig_t      glconfig;
    int             vidWidth;
    int             vidHeight;

    /* Timing */
    int             realtime;       /* ignores pause/timescale */
    float           frametime;      /* seconds */
    int             realFrametime;  /* msec */

    /* Map loading */
    char            mapname[MAX_QPATH];
    qboolean        mapLoading;
    float           loadingPercent;

    /* Mouse accumulation */
    int             mouseDx;
    int             mouseDy;
} clientState_t;

static clientState_t cls;

connstate_t CL_GetConnectionState(void) {
    return cls.state;
}

/* =========================================================================
 * Input callbacks -- wired to Win_ProcessEvents via sys_sdl.c
 * ========================================================================= */

extern void IN_SetKeyCallback(void (*callback)(int key, qboolean down, unsigned int time));
extern void IN_SetCharCallback(void (*callback)(int ch));
extern void IN_SetMouseMoveCallback(void (*callback)(int dx, int dy));

static void CL_KeyEvent(int key, qboolean down, unsigned int time) {
    Key_KeyEvent(key, down, time);
}

static void CL_CharEvent(int ch) {
    Key_CharEvent(ch);
}

static void CL_MouseMove(int dx, int dy) {
    cls.mouseDx += dx;
    cls.mouseDy += dy;
}

/* =========================================================================
 * Map loading
 * ========================================================================= */

void CL_LoadMap(const char *mapname) {
    Com_Printf("--- CL_LoadMap: %s ---\n", mapname);

    cls.state = CA_LOADING;
    cls.mapLoading = qtrue;
    cls.loadingPercent = 0.0f;
    Q_strncpyz(cls.mapname, mapname, sizeof(cls.mapname));

    /* Tell renderer to load BSP */
    R_BeginRegistration();
    R_LoadWorldMap(mapname);

    /* TODO: Precache models, sounds, images from configstrings */

    R_EndRegistration();

    cls.mapLoading = qfalse;
    cls.loadingPercent = 1.0f;
    cls.state = CA_PRIMED;

    Com_Printf("Map loaded: %s\n", mapname);
}

/* =========================================================================
 * Console commands
 * ========================================================================= */

static void CL_Disconnect_f(void) {
    if (cls.state == CA_DISCONNECTED) return;
    Com_Printf("Disconnected from server\n");
    cls.state = CA_DISCONNECTED;
}

static void CL_Quit_f(void) {
    Com_Shutdown();
    Sys_Quit();
}

/* Note: "map" command is registered by SV_Init in sv_main.c.
 * The server handles BSP loading, then calls CL_LoadMap via
 * the client-server loopback to load the renderer's copy. */

/* =========================================================================
 * Initialization / Shutdown
 * ========================================================================= */

void CL_Init(void) {
    Com_Printf("--- CL_Init ---\n");

    memset(&cls, 0, sizeof(cls));
    cls.state = CA_DISCONNECTED;

    /* Create window first -- renderer needs it */
    /* TODO: Read r_mode, r_fullscreen cvars */
    Win_Create(1024, 768, qfalse);

    /* Initialize renderer */
    R_Init();

    /* Sound is initialized by Com_Init() -- no double init */

    /* Key binding system and console */
    Key_Init();
    Con_Init();

    /* Initialize +/- movement commands */
    CL_InitInput();

    /* Initialize input and wire callbacks */
    IN_Init();
    IN_SetKeyCallback(CL_KeyEvent);
    IN_SetCharCallback(CL_CharEvent);
    IN_SetMouseMoveCallback(CL_MouseMove);

    /* Get GL config */
    R_GetGlconfig(&cls.glconfig);
    cls.vidWidth = cls.glconfig.vidWidth;
    cls.vidHeight = cls.glconfig.vidHeight;

    /* Register commands */
    Cmd_AddCommand("disconnect", CL_Disconnect_f);
    Cmd_AddCommand("quit", CL_Quit_f);

    Com_Printf("Client initialized\n");
}

void CL_Shutdown(void) {
    Com_Printf("--- CL_Shutdown ---\n");

    Cmd_RemoveCommand("disconnect");
    Cmd_RemoveCommand("quit");

    CL_ShutdownInput();
    Con_Shutdown();
    Key_Shutdown();
    IN_Shutdown();
    R_Shutdown();
    Win_Destroy();
}

/* =========================================================================
 * Client frame
 * ========================================================================= */

void CL_Frame(int msec) {
    cls.realtime += msec;
    cls.realFrametime = msec;
    cls.frametime = msec / 1000.0f;
    cls.framecount++;

    /* Process window events -- returns qfalse on quit */
    if (!Win_ProcessEvents()) {
        Cbuf_AddText("quit\n");
        return;
    }

    /* Process input */
    IN_Frame();

    /* Update sound */
    S_Update();

    /* --- Render --- */
    R_BeginFrame();

    switch (cls.state) {
        case CA_DISCONNECTED:
            /* Draw menu/title screen */
            /* TODO: UI system */
            break;

        case CA_LOADING:
            /* Draw loading screen */
            /* TODO: Loading progress bar */
            break;

        case CA_ACTIVE:
        case CA_PRIMED: {
            /* Build and render scene via cgame */
            R_ClearScene();
            /* TODO: Call cgame CG_DrawActiveFrame */
            /* TODO: R_RenderScene(&rd) */
            break;
        }

        default:
            break;
    }

    /* Draw console on top of everything */
    Con_DrawConsole();

    R_EndFrame();

    /* Reset per-frame mouse accumulation */
    cls.mouseDx = 0;
    cls.mouseDy = 0;
}
