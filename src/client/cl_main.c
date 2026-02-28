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
#include "../common/cg_public.h"
#include "../renderer/tr_types.h"
#include "../sound/snd_local.h"
#include "../engine/win32_compat.h"
#include <string.h>
#include <math.h>

/* Forward declarations from renderer */
extern void R_Init(void);
extern void R_Shutdown(void);
extern void R_BeginFrame(void);
extern void R_EndFrame(void);
extern void R_ClearScene(void);
extern void R_RenderScene(const refdef_t *fd);
extern void R_GetGlconfig(glconfig_t *config);
extern void R_SetColor(const float *rgba);
extern void R_DrawFillRect(float x, float y, float w, float h,
                           float r, float g, float b, float a);
extern void R_DrawString(float x, float y, const char *str,
                         float scale, float r, float g, float b, float a);
extern void R_Set2D(void);

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
extern void CL_CGameFrame(int serverTime);
extern void CL_CGameDraw2D(void);
extern clientGameExport_t *CL_GetCGameExport(void);

/* Forward declarations from cl_input.c */
extern void CL_InitInput(void);
extern void CL_ShutdownInput(void);
extern void CL_CreateCmd(usercmd_t *cmd, int serverTime, int mouseDx, int mouseDy);

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

    /* cgame module */
    qboolean        cgameStarted;

    /* View angles (accumulated from mouse and keys) */
    float           viewangles[3];
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

    /* Precache models, sounds, images from server configstrings */
    {
        extern char *SV_GetConfigstring(int index);
        extern qhandle_t R_RegisterModel(const char *name);
        extern sfxHandle_t S_RegisterSound(const char *name);

        #define CS_MODELS_CL   32
        #define CS_SOUNDS_CL   (CS_MODELS_CL + 256)

        /* Precache models */
        for (int i = 1; i < 256; i++) {
            const char *cs = SV_GetConfigstring(CS_MODELS_CL + i);
            if (cs[0]) {
                R_RegisterModel(cs);
                cls.loadingPercent = 0.1f + 0.4f * ((float)i / 256.0f);
            }
        }

        /* Precache sounds */
        for (int i = 1; i < 256; i++) {
            const char *cs = SV_GetConfigstring(CS_SOUNDS_CL + i);
            if (cs[0]) {
                S_RegisterSound(cs);
                cls.loadingPercent = 0.5f + 0.4f * ((float)i / 256.0f);
            }
        }

        cls.loadingPercent = 0.95f;
    }

    R_EndRegistration();

    cls.mapLoading = qfalse;
    cls.loadingPercent = 1.0f;

    /* Initialize client game module if not already running */
    if (!cls.cgameStarted) {
        CL_InitCGame();
        cls.cgameStarted = qtrue;
    }

    cls.state = CA_ACTIVE;
    Com_Printf("Map loaded: %s\n", mapname);
}

float CL_GetLoadingPercent(void) {
    return cls.loadingPercent;
}

/* =========================================================================
 * Console commands
 * ========================================================================= */

static void CL_Disconnect_f(void) {
    if (cls.state == CA_DISCONNECTED) return;

    if (cls.cgameStarted) {
        CL_ShutdownCGame();
        cls.cgameStarted = qfalse;
    }

    cls.viewangles[0] = cls.viewangles[1] = cls.viewangles[2] = 0.0f;
    cls.serverTime = 0;

    Com_Printf("Disconnected from server\n");
    cls.state = CA_DISCONNECTED;
}

/*
 * CL_ForceDisconnect -- called by Com_Error (ERR_DROP / ERR_DISCONNECT)
 * to reset client state without going through normal disconnect flow.
 * Must be safe to call even when client is in a partially broken state.
 */
void CL_ForceDisconnect(void) {
    if (cls.cgameStarted) {
        CL_ShutdownCGame();
        cls.cgameStarted = qfalse;
    }

    cls.viewangles[0] = cls.viewangles[1] = cls.viewangles[2] = 0.0f;
    cls.serverTime = 0;
    cls.mapname[0] = '\0';
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

    /* Read video mode cvars for window setup */
    cvar_t *r_mode = Cvar_Get("r_mode", "4", CVAR_ARCHIVE);
    cvar_t *r_fullscreen = Cvar_Get("r_fullscreen", "0", CVAR_ARCHIVE);
    cvar_t *r_customwidth = Cvar_Get("r_customwidth", "1280", CVAR_ARCHIVE);
    cvar_t *r_customheight = Cvar_Get("r_customheight", "720", CVAR_ARCHIVE);

    /* Standard mode table matching Q3/FAKK2 r_mode values */
    int vidWidth = 1024, vidHeight = 768;
    switch (r_mode->integer) {
        case 0: vidWidth = 320;  vidHeight = 240;  break;
        case 1: vidWidth = 400;  vidHeight = 300;  break;
        case 2: vidWidth = 512;  vidHeight = 384;  break;
        case 3: vidWidth = 640;  vidHeight = 480;  break;
        case 4: vidWidth = 800;  vidHeight = 600;  break;
        case 5: vidWidth = 960;  vidHeight = 720;  break;
        case 6: vidWidth = 1024; vidHeight = 768;  break;
        case 7: vidWidth = 1152; vidHeight = 864;  break;
        case 8: vidWidth = 1280; vidHeight = 1024; break;
        case 9: vidWidth = 1600; vidHeight = 1200; break;
        case 10: vidWidth = 1920; vidHeight = 1080; break;
        case 11: vidWidth = 2560; vidHeight = 1440; break;
        case -1: /* Custom mode */
            vidWidth = r_customwidth->integer;
            vidHeight = r_customheight->integer;
            break;
    }
    if (vidWidth < 320) vidWidth = 320;
    if (vidHeight < 240) vidHeight = 240;

    qboolean fullscreen = r_fullscreen->integer ? qtrue : qfalse;
    Com_Printf("Video: %dx%d %s (r_mode %d)\n",
               vidWidth, vidHeight, fullscreen ? "fullscreen" : "windowed",
               r_mode->integer);
    Win_Create(vidWidth, vidHeight, fullscreen);

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

    if (cls.cgameStarted) {
        CL_ShutdownCGame();
        cls.cgameStarted = qfalse;
    }

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

/* =========================================================================
 * Screen drawing helpers for non-game states
 * ========================================================================= */

static void CL_DrawDisconnectedScreen(void) {
    R_Set2D();

    /* Dark background */
    R_DrawFillRect(0, 0, (float)cls.vidWidth, (float)cls.vidHeight,
                   0.1f, 0.05f, 0.0f, 1.0f);

    /* Title */
    float cx = (float)cls.vidWidth * 0.5f - 200.0f;
    float cy = (float)cls.vidHeight * 0.3f;
    R_DrawString(cx, cy, "Heavy Metal: FAKK2", 2.0f, 1.0f, 0.6f, 0.0f, 1.0f);

    /* Subtitle */
    R_DrawString(cx + 16.0f, cy + 40.0f, "Static Recompilation", 1.0f,
                 0.7f, 0.7f, 0.7f, 1.0f);

    /* Instructions */
    R_DrawString(cx + 16.0f, cy + 80.0f, "Press ~ for console, type: map <name>",
                 1.0f, 0.5f, 0.5f, 0.5f, 1.0f);
}

static void CL_DrawLoadingScreen(void) {
    R_Set2D();

    /* Dark background */
    R_DrawFillRect(0, 0, (float)cls.vidWidth, (float)cls.vidHeight,
                   0.0f, 0.0f, 0.0f, 1.0f);

    /* Loading text */
    float cx = (float)cls.vidWidth * 0.5f - 80.0f;
    float cy = (float)cls.vidHeight * 0.45f;
    R_DrawString(cx, cy, "Loading...", 1.5f, 1.0f, 0.7f, 0.0f, 1.0f);

    /* Map name */
    if (cls.mapname[0]) {
        R_DrawString(cx, cy + 30.0f, cls.mapname, 1.0f, 0.6f, 0.6f, 0.6f, 1.0f);
    }

    /* Progress bar */
    float barX = (float)cls.vidWidth * 0.25f;
    float barY = cy + 60.0f;
    float barW = (float)cls.vidWidth * 0.5f;
    float barH = 8.0f;

    /* Background */
    R_DrawFillRect(barX, barY, barW, barH, 0.2f, 0.2f, 0.2f, 1.0f);
    /* Fill */
    R_DrawFillRect(barX, barY, barW * cls.loadingPercent, barH,
                   0.8f, 0.4f, 0.0f, 1.0f);
}

/* =========================================================================
 * Build refdef for engine-side fallback rendering (no cgame DLL)
 *
 * When the cgame DLL is not loaded, the engine renders a basic scene
 * from the current snapshot's playerState.
 * ========================================================================= */

static void CL_BuildRefdef(refdef_t *rd) {
    extern void CL_SetSnapshot(int serverTime, int snapNum,
                               const playerState_t *ps,
                               const entityState_t *entities, int numEntities);

    memset(rd, 0, sizeof(*rd));

    /* Get current snapshot data */
    extern int cl_currentServerTime;
    extern snapshot_t cl_snapshot;

    playerState_t *ps = &cl_snapshot.ps;

    /* Viewport fills the screen */
    rd->x = 0;
    rd->y = 0;
    rd->width = cls.vidWidth;
    rd->height = cls.vidHeight;

    /* Field of view */
    rd->fov_x = (ps->fov > 0.0f) ? ps->fov : 90.0f;
    float x = rd->fov_x / 360.0f * 3.14159265f;
    float aspect = (float)rd->width / (float)rd->height;
    rd->fov_y = 2.0f * atanf(tanf(x) / aspect) * 360.0f / 3.14159265f;

    /* View origin from player state */
    VectorCopy(ps->origin, rd->vieworg);
    rd->vieworg[2] += (float)ps->viewheight;

    /* View angles -- combine playerState viewangles with client-side mouse look */
    float angles[3];
    angles[PITCH] = ps->viewangles[PITCH] + cls.viewangles[PITCH];
    angles[YAW]   = ps->viewangles[YAW]   + cls.viewangles[YAW];
    angles[ROLL]  = ps->viewangles[ROLL];

    /* Build view axis from angles */
    AngleVectors(angles, rd->viewaxis[0], rd->viewaxis[1], rd->viewaxis[2]);

    /* Time for shader effects */
    rd->time = cls.realtime;

    /* Screen blend from player state (damage flash, underwater, etc.) */
    rd->blend[0] = ps->blend[0];
    rd->blend[1] = ps->blend[1];
    rd->blend[2] = ps->blend[2];
    rd->blend[3] = ps->blend[3];

    /* Sky */
    rd->sky_alpha = 1.0f;
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

    /* Build and send user command to server when in-game */
    if (cls.state == CA_ACTIVE) {
        usercmd_t cmd;
        CL_CreateCmd(&cmd, SV_GetServerTime(), cls.mouseDx, cls.mouseDy);

        /* Update view angles from mouse input */
        {
            float sens = Cvar_VariableValue("sensitivity");
            float yawScale = Cvar_VariableValue("m_yaw");
            float pitchScale = Cvar_VariableValue("m_pitch");
            if (yawScale == 0.0f) yawScale = 0.022f;
            if (pitchScale == 0.0f) pitchScale = 0.022f;

            cls.viewangles[YAW]   -= cls.mouseDx * sens * yawScale;
            cls.viewangles[PITCH] += cls.mouseDy * sens * pitchScale;

            /* Clamp pitch */
            if (cls.viewangles[PITCH] > 89.0f) cls.viewangles[PITCH] = 89.0f;
            if (cls.viewangles[PITCH] < -89.0f) cls.viewangles[PITCH] = -89.0f;
        }

        /* Send usercmd to server (loopback fast path -- directly write to client struct) */
        cmd.angles[PITCH] = (int)(cls.viewangles[PITCH] * 65536.0f / 360.0f);
        cmd.angles[YAW]   = (int)(cls.viewangles[YAW]   * 65536.0f / 360.0f);
        cmd.angles[ROLL]  = 0;

        extern void SV_ClientUsercmd(int clientNum, const usercmd_t *cmd);
        SV_ClientUsercmd(0, &cmd);
    }

    /* Update sound */
    S_Update();

    /* --- Render --- */
    R_BeginFrame();

    switch (cls.state) {
        case CA_DISCONNECTED:
            CL_DrawDisconnectedScreen();
            break;

        case CA_CONNECTING:
        case CA_CONNECTED:
        case CA_LOADING:
            CL_DrawLoadingScreen();
            break;

        case CA_PRIMED:
        case CA_ACTIVE: {
            clientGameExport_t *cge = CL_GetCGameExport();
            if (cge) {
                /* cgame DLL handles scene building and rendering */
                CL_CGameFrame(cls.serverTime > 0 ? cls.serverTime : cls.realtime);
                CL_CGameDraw2D();
            } else {
                /* No cgame DLL -- engine-side fallback rendering */
                R_ClearScene();

                /* Add entities from current snapshot to scene */
                extern snapshot_t cl_snapshot;
                for (int i = 0; i < cl_snapshot.numEntities; i++) {
                    refEntity_t rent;
                    memset(&rent, 0, sizeof(rent));
                    rent.hModel = cl_snapshot.entities[i].modelindex;
                    VectorCopy(cl_snapshot.entities[i].origin, rent.origin);
                    rent.scale = cl_snapshot.entities[i].scale;
                    if (rent.scale <= 0.0f) rent.scale = 1.0f;

                    /* Identity axis */
                    rent.axis[0][0] = 1.0f;
                    rent.axis[1][1] = 1.0f;
                    rent.axis[2][2] = 1.0f;

                    R_AddRefEntityToScene(&rent);
                }

                /* Build and submit refdef */
                refdef_t rd;
                CL_BuildRefdef(&rd);
                R_RenderScene(&rd);
            }
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
