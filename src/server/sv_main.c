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

/* Forward declarations for console commands */
void SV_Map_f(void);
void SV_KillServer_f(void);
void SV_SaveGame_f(void);
void SV_LoadGame_f(void);
void SV_Autosave(void);

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
    Cmd_AddCommand("save", SV_SaveGame_f);
    Cmd_AddCommand("load", SV_LoadGame_f);
    Cmd_AddCommand("savegame", SV_SaveGame_f);
    Cmd_AddCommand("loadgame", SV_LoadGame_f);

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
    Cmd_RemoveCommand("save");
    Cmd_RemoveCommand("load");
    Cmd_RemoveCommand("savegame");
    Cmd_RemoveCommand("loadgame");

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

    /* Autosave after every successful map load */
    SV_Autosave();

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
 * Save / Load
 *
 * FAKK2 save flow:
 *   Save:  WritePersistant -> WriteLevel
 *   Load:  ReadPersistant -> SpawnServer(map) -> ReadLevel
 *
 * Save files live under "save/<name>". The game DLL handles its own
 * archive format via the Archiver class; the engine just provides the
 * filename and orchestrates the calls.
 * ========================================================================= */

#define SAVE_DIR "save"

/* Build the save-file base path: "save/<name>" */
static void SV_BuildSavePath(char *out, int outsize, const char *name) {
    snprintf(out, outsize, "%s/%s", SAVE_DIR, name);
}

void SV_SaveGame_f(void) {
    if (sv.state != SS_GAME) {
        Com_Printf("Not playing a game\n");
        return;
    }

    game_export_t *ge = SV_GetGameExport();
    if (!ge) {
        Com_Printf("No game module loaded\n");
        return;
    }

    const char *saveName = "quick";
    if (Cmd_Argc() >= 2) {
        saveName = Cmd_Argv(1);
    }

    char path[MAX_QPATH];
    SV_BuildSavePath(path, sizeof(path), saveName);

    Com_Printf("Saving game to '%s'...\n", path);

    /* Persistent state first (player inventory, health, etc.) */
    if (ge->WritePersistant) {
        ge->WritePersistant(path);
    }

    /* Level state (entities, scripts, triggers, etc.) */
    if (ge->WriteLevel) {
        ge->WriteLevel(path, qfalse);
    }

    /* Store the current map name alongside the save so we know which
     * level to load when restoring.  The game DLL's archive may already
     * contain this, but we keep a small engine-side sidecar for quick
     * validation without needing the game DLL to parse its own format. */
    char infopath[MAX_QPATH];
    snprintf(infopath, sizeof(infopath), "%s/%s.ssv", SAVE_DIR, saveName);
    char info[256];
    snprintf(info, sizeof(info), "%s\n%d\n", sv.mapname, sv.time);
    FS_WriteFile(infopath, info, (int)strlen(info));

    Com_Printf("Game saved.\n");
}

void SV_LoadGame_f(void) {
    game_export_t *ge;

    const char *saveName = "quick";
    if (Cmd_Argc() >= 2) {
        saveName = Cmd_Argv(1);
    }

    /* Read the engine-side sidecar to find the map name */
    char infopath[MAX_QPATH];
    snprintf(infopath, sizeof(infopath), "%s/%s.ssv", SAVE_DIR, saveName);
    void *infobuf = NULL;
    long infolen = FS_ReadFile(infopath, &infobuf);
    if (infolen <= 0 || !infobuf) {
        Com_Printf("Save file not found: %s\n", saveName);
        return;
    }

    /* Parse map name from first line */
    char mapname[MAX_QPATH];
    memset(mapname, 0, sizeof(mapname));
    const char *p = (const char *)infobuf;
    int mi = 0;
    while (*p && *p != '\n' && *p != '\r' && mi < MAX_QPATH - 1) {
        mapname[mi++] = *p++;
    }
    mapname[mi] = '\0';
    FS_FreeFile(infobuf);

    if (!mapname[0]) {
        Com_Printf("Invalid save file: %s\n", saveName);
        return;
    }

    char path[MAX_QPATH];
    SV_BuildSavePath(path, sizeof(path), saveName);

    Com_Printf("Loading game from '%s' (map: %s)...\n", path, mapname);

    /* Validate the archive before committing */
    ge = SV_GetGameExport();
    if (ge && ge->LevelArchiveValid) {
        if (!ge->LevelArchiveValid(path)) {
            Com_Printf("Save file is invalid or corrupted: %s\n", path);
            return;
        }
    }

    /* Restore persistent state (player stats, inventory) */
    if (ge && ge->ReadPersistant) {
        ge->ReadPersistant(path);
    }

    /* Spawn the server fresh for that map -- this loads the BSP,
     * initialises the game module, spawns default entities, and
     * connects the local client. */
    SV_SpawnServer(mapname);

    /* Now overlay the saved entity state on top of the fresh level */
    ge = SV_GetGameExport();
    if (ge && ge->ReadLevel) {
        if (!ge->ReadLevel(path)) {
            Com_Printf("WARNING: failed to read level state from '%s'\n", path);
        }
    }

    Com_Printf("Game loaded.\n");
}

/* Autosave -- called after successful map load if desired */
void SV_Autosave(void) {
    if (sv.state != SS_GAME) return;

    game_export_t *ge = SV_GetGameExport();
    if (!ge) return;

    char path[MAX_QPATH];
    SV_BuildSavePath(path, sizeof(path), "autosave");

    Com_DPrintf("Autosaving to '%s'...\n", path);

    if (ge->WritePersistant) {
        ge->WritePersistant(path);
    }
    if (ge->WriteLevel) {
        ge->WriteLevel(path, qtrue);  /* autosave = qtrue */
    }

    /* Sidecar with map info */
    char infopath[MAX_QPATH];
    snprintf(infopath, sizeof(infopath), "%s/autosave.ssv", SAVE_DIR);
    char info[256];
    snprintf(info, sizeof(info), "%s\n%d\n", sv.mapname, sv.time);
    FS_WriteFile(infopath, info, (int)strlen(info));

    Com_DPrintf("Autosave complete.\n");
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

        /* Run Morpheus script threads */
        extern void Script_RunThreads(float currentTime);
        Script_RunThreads((float)sv.time / 1000.0f);
    }

    /* Prepare frame for snapshot building */
    ge->PrepFrame();

    /* Build and send snapshots to clients */
    sv.snapCount++;
    SV_SendClientSnapshots();
}

/* =========================================================================
 * Snapshot building and transmission
 *
 * For single-player (loopback), we directly copy entity state into
 * the client's snapshot buffer. This avoids the overhead of delta
 * encoding through the network message system.
 * ========================================================================= */

/* Implemented in cl_cgame.c -- receives snapshot data from server */
extern void CL_SetSnapshot(int serverTime, int snapNum,
                           const playerState_t *ps,
                           const entityState_t *entities, int numEntities);

static void SV_SendClientSnapshots(void) {
    game_export_t *ge = SV_GetGameExport();
    if (!ge || !ge->gentities) return;

    /* Only send to active local client */
    if (!sv.clients[0].active) return;

    /* Get player state for client 0 */
    extern playerState_t *SV_GetClientPlayerState(int clientNum);
    playerState_t ps;
    memset(&ps, 0, sizeof(ps));
    playerState_t *clientPS = SV_GetClientPlayerState(0);
    if (clientPS) {
        ps = *clientPS;
    }

    /* Collect visible entities */
    entityState_t snapEntities[256];
    int numSnapEntities = 0;

    int numEnts = ge->num_entities;
    if (numEnts > ge->max_entities) numEnts = ge->max_entities;

    for (int i = 0; i < numEnts && numSnapEntities < 256; i++) {
        gentity_t *ent = (gentity_t *)((byte *)ge->gentities + i * ge->gentitySize);
        if (!ent->inuse) continue;
        if (ent->svFlags & SVF_NOCLIENT) continue;

        snapEntities[numSnapEntities] = ent->s;
        snapEntities[numSnapEntities].number = i;
        numSnapEntities++;
    }

    /* Push to client via direct function call (loopback fast path) */
    CL_SetSnapshot(sv.time, sv.snapCount, &ps, snapEntities, numSnapEntities);
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

/* Store a usercmd from the client -- used by loopback path */
void SV_ClientUsercmd(int clientNum, const usercmd_t *cmd) {
    if (sv.state != SS_GAME) return;
    if (clientNum < 0 || clientNum >= sv.numClients) return;
    if (!sv.clients[clientNum].active) return;
    if (!cmd) return;
    sv.clients[clientNum].lastUsercmd = *cmd;
}

/* Public wrapper for loopback client command dispatch */
void SV_ExecuteClientCommandStr(int clientNum, const char *s) {
    if (sv.state != SS_GAME) return;
    if (clientNum < 0 || clientNum >= sv.numClients) return;
    if (!sv.clients[clientNum].active) return;
    SV_ExecuteClientCommand(&sv.clients[clientNum], s);
}

/* =========================================================================
 * Collision model accessors (used by sv_world.c)
 * ========================================================================= */

extern const char *CM_EntityString(void);
