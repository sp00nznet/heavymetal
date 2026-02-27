/*
 * sv_main.c -- Server main loop
 *
 * FAKK2's server runs the game simulation, entity logic, AI,
 * physics, and Morpheus script execution. Even in single-player,
 * the local server runs and communicates with the client via
 * loopback messages.
 *
 * Server state machine:
 *   SS_DEAD -> SS_LOADING -> SS_GAME
 *
 * Frame flow:
 *   1. Read client messages (user commands)
 *   2. Advance server time
 *   3. Call game module RunFrame
 *   4. Call game module PrepFrame
 *   5. Build and send snapshots to clients
 */

#include "../common/qcommon.h"
#include "../common/g_public.h"
#include "../collision/cm_local.h"

/* =========================================================================
 * Server state
 * ========================================================================= */

typedef enum {
    SS_DEAD,        /* no map loaded */
    SS_LOADING,     /* loading map */
    SS_GAME         /* actively running */
} serverState_t;

#define MAX_SV_CLIENTS  64

typedef struct {
    qboolean        active;
    int             clientNum;
    char            userinfo[MAX_INFO_STRING];
    usercmd_t       lastUsercmd;
    int             lastMessageTime;
    qboolean        gentitySet;
    gentity_t       *gentity;
    playerState_t   *playerState;
} svClient_t;

typedef struct {
    serverState_t   state;
    int             time;           /* server time in msec */
    int             timeResidual;   /* fractional frame time */
    int             frameTime;      /* msec per server frame */

    char            mapname[MAX_QPATH];
    char            mapfile[MAX_QPATH];

    int             numClients;
    svClient_t      clients[MAX_SV_CLIENTS];

    /* Snapshot counter */
    int             snapCount;

    /* Server cvars */
    cvar_t          *sv_fps;
    cvar_t          *sv_maxclients;
    cvar_t          *sv_mapname;
    cvar_t          *sv_running;
    cvar_t          *sv_cheats;

    qboolean        initialized;
} server_t;

static server_t sv;

/* Accessors from sv_game.c */
extern void SV_InitGameProgs(void);
extern void SV_ShutdownGameProgs(void);
extern game_export_t *SV_GetGameExport(void);

/* =========================================================================
 * Initialization
 * ========================================================================= */

void SV_Init(void) {
    Com_Printf("--- SV_Init ---\n");

    memset(&sv, 0, sizeof(sv));
    sv.state = SS_DEAD;
    sv.frameTime = 50;  /* 20 fps default server frame rate */

    /* Register server cvars */
    sv.sv_fps = Cvar_Get("sv_fps", "20", CVAR_ARCHIVE);
    sv.sv_maxclients = Cvar_Get("sv_maxclients", "1", CVAR_LATCH);
    sv.sv_mapname = Cvar_Get("mapname", "", CVAR_SERVERINFO);
    sv.sv_running = Cvar_Get("sv_running", "0", CVAR_ROM);
    sv.sv_cheats = Cvar_Get("sv_cheats", "1", CVAR_LATCH);

    /* Register commands */
    Cmd_AddCommand("map", SV_Map_f);
    Cmd_AddCommand("killserver", SV_KillServer_f);

    sv.initialized = qtrue;
    Com_Printf("Server initialized\n");
}

void SV_Shutdown(const char *finalmsg) {
    if (!sv.initialized) return;

    if (finalmsg && finalmsg[0]) {
        Com_Printf("Server shutdown: %s\n", finalmsg);
    }

    /* Shut down game module */
    if (sv.state != SS_DEAD) {
        SV_ShutdownGameProgs();
    }

    sv.state = SS_DEAD;
    Cvar_Set("sv_running", "0");
    Cvar_Set("mapname", "");

    Cmd_RemoveCommand("map");
    Cmd_RemoveCommand("killserver");

    sv.initialized = qfalse;
}

/* =========================================================================
 * Map loading -- SV_SpawnServer
 *
 * This is the critical path that brings up a new level:
 *   1. Load BSP into collision model
 *   2. Initialize game module
 *   3. Spawn entities from BSP entity string
 *   4. Connect local client
 * ========================================================================= */

void SV_SpawnServer(const char *mapname) {
    game_export_t *ge;

    Com_Printf("--- SV_SpawnServer: %s ---\n", mapname);

    /* Kill existing server */
    if (sv.state != SS_DEAD) {
        SV_ShutdownGameProgs();
    }

    sv.state = SS_LOADING;
    sv.time = 0;
    sv.timeResidual = 0;
    sv.snapCount = 0;

    /* Update cvars */
    Q_strncpyz(sv.mapname, mapname, sizeof(sv.mapname));
    snprintf(sv.mapfile, sizeof(sv.mapfile), "maps/%s.bsp", mapname);
    Cvar_Set("mapname", mapname);
    Cvar_Set("sv_running", "1");

    /* Server frame rate from cvar */
    if (sv.sv_fps->integer > 0) {
        sv.frameTime = 1000 / sv.sv_fps->integer;
    }

    /* Load collision model (BSP) */
    Com_Printf("Loading collision model: %s\n", sv.mapfile);
    CM_LoadMap(sv.mapfile);

    /* Initialize game module */
    SV_InitGameProgs();
    ge = SV_GetGameExport();

    if (ge) {
        /* Initialize game logic */
        ge->Init(sv.time, Sys_Milliseconds());

        /* Spawn entities from BSP entity string */
        const char *entityString = CM_EntityString();
        if (entityString) {
            Com_Printf("Spawning entities from BSP...\n");
            ge->SpawnEntities(mapname, entityString, sv.time);
        }

        /* Connect local client (single-player) */
        int maxClients = sv.sv_maxclients->integer;
        if (maxClients < 1) maxClients = 1;
        if (maxClients > MAX_SV_CLIENTS) maxClients = MAX_SV_CLIENTS;
        sv.numClients = maxClients;

        /* Clear client slots */
        memset(sv.clients, 0, sizeof(sv.clients));

        /* Connect player 0 (local client) */
        char *rejectReason = ge->ClientConnect(0, qtrue);
        if (rejectReason) {
            Com_Printf("WARNING: Local client rejected: %s\n", rejectReason);
        } else {
            sv.clients[0].active = qtrue;
            sv.clients[0].clientNum = 0;

            /* Begin client session */
            if (ge->gentities) {
                usercmd_t cmd;
                memset(&cmd, 0, sizeof(cmd));
                ge->ClientBegin(ge->gentities, &cmd);
                sv.clients[0].gentity = ge->gentities;
                sv.clients[0].gentitySet = qtrue;
            }
        }

        Com_Printf("Game module initialized, %d entities spawned\n",
                    ge->num_entities);
    } else {
        Com_Printf("WARNING: No game module -- server running without game logic\n");
    }

    sv.state = SS_GAME;

    /* Tell the client to load the map (renderer BSP, precaching) */
    extern void CL_LoadMap(const char *mapname);
    CL_LoadMap(sv.mapfile);

    Com_Printf("--- Server running: %s ---\n", mapname);
}

/* =========================================================================
 * Console commands
 * ========================================================================= */

void SV_Map_f(void) {
    if (Cmd_Argc() < 2) {
        Com_Printf("Usage: map <mapname>\n");
        return;
    }

    const char *mapname = Cmd_Argv(1);

    /* Check if BSP exists */
    char testpath[MAX_QPATH];
    snprintf(testpath, sizeof(testpath), "maps/%s.bsp", mapname);
    fileHandle_t f;
    int len = FS_FOpenFileRead(testpath, &f, qtrue);
    if (len <= 0) {
        Com_Printf("Can't find map: %s\n", testpath);
        return;
    }
    FS_FCloseFile(f);

    SV_SpawnServer(mapname);
}

void SV_KillServer_f(void) {
    if (sv.state == SS_DEAD) {
        Com_Printf("Server is not running\n");
        return;
    }

    SV_Shutdown("Server killed by user");
}

/* =========================================================================
 * Client command processing
 * ========================================================================= */

static void SV_ExecuteClientCommand(svClient_t *cl, const char *s) {
    game_export_t *ge = SV_GetGameExport();

    Cmd_TokenizeString(s);
    if (!Cmd_Argc()) return;

    /* Some commands are handled by the server directly */
    const char *cmd = Cmd_Argv(0);

    if (!Q_stricmp(cmd, "userinfo")) {
        Q_strncpyz(cl->userinfo, Cmd_Argv(1), sizeof(cl->userinfo));
        if (ge && cl->gentity) {
            ge->ClientUserinfoChanged(cl->gentity, cl->userinfo);
        }
        return;
    }

    if (!Q_stricmp(cmd, "disconnect")) {
        if (ge && cl->gentity) {
            ge->ClientDisconnect(cl->gentity);
        }
        cl->active = qfalse;
        return;
    }

    /* Pass unhandled commands to game module */
    if (ge && cl->gentity) {
        ge->ClientCommand(cl->gentity);
    }
}

/* =========================================================================
 * Server frame
 *
 * Called every Com_Frame(). Advances server time in fixed timesteps
 * and calls the game module for entity updates.
 * ========================================================================= */

void SV_Frame(int msec) {
    if (sv.state == SS_DEAD) return;

    game_export_t *ge = SV_GetGameExport();
    if (!ge) return;

    /* Accumulate time */
    sv.timeResidual += msec;

    /* Run fixed-timestep frames */
    while (sv.timeResidual >= sv.frameTime) {
        sv.timeResidual -= sv.frameTime;
        sv.time += sv.frameTime;

        /* Process client user commands */
        for (int i = 0; i < sv.numClients; i++) {
            if (!sv.clients[i].active) continue;

            /* Feed last known usercmd to game */
            if (sv.clients[i].gentity) {
                ge->ClientThink(sv.clients[i].gentity, &sv.clients[i].lastUsercmd);
            }
        }

        /* Run game logic frame */
        ge->RunFrame(sv.time, sv.frameTime);
    }

    /* Prepare frame for snapshot building */
    ge->PrepFrame();

    /* Build and send snapshots to clients */
    sv.snapCount++;
    /* TODO: SV_SendClientSnapshots() -- for loopback, copy game state
     * directly to client's snapshot buffer */
}

/* =========================================================================
 * Utility -- server time query
 * ========================================================================= */

int SV_GetServerTime(void) {
    return sv.time;
}

qboolean SV_IsRunning(void) {
    return sv.state == SS_GAME;
}

/* =========================================================================
 * Collision model accessors (used by sv_world.c)
 * ========================================================================= */

extern const char *CM_EntityString(void);
