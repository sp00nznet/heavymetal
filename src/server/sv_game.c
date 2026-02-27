/*
 * sv_game.c -- Server-side game module interface
 *
 * Loads gamex86.dll and exchanges function pointer tables.
 * The engine fills game_import_t with engine functions, calls GetGameAPI(),
 * and receives game_export_t with game callbacks.
 *
 * Original binary calls:
 *   LoadLibraryA("fakk/gamex86.dll")
 *   GetProcAddress(handle, "GetGameAPI")
 *   gExport = GetGameAPI(&gImport)
 */

#include "../common/qcommon.h"
#include "../common/g_public.h"
#include "../tiki/tiki.h"

/* Global game interface pointers */
static game_import_t    gi;     /* engine -> game */
static game_export_t    *ge;    /* game -> engine */
static void             *gameLib;

/* =========================================================================
 * Stub implementations for game_import_t functions
 *
 * These bridge the engine's internal functions to the game_import_t
 * function pointer table format expected by the game DLL.
 * ========================================================================= */

static void GI_Printf(const char *fmt, ...) {
    va_list args;
    char buf[4096];
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Com_Printf("%s", buf);
}

static void GI_DPrintf(const char *fmt, ...) {
    va_list args;
    char buf[4096];
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Com_DPrintf("%s", buf);
}

static void GI_Error(int level, const char *fmt, ...) {
    va_list args;
    char buf[4096];
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Com_Error(level, "%s", buf);
}

static void *GI_Malloc(int size) {
    return Z_TagMalloc(size, TAG_GAME);
}

static void GI_Free(void *block) {
    Z_Free(block);
}

static int GI_Milliseconds(void) {
    return Sys_Milliseconds();
}

static cvar_t *GI_Cvar(const char *name, const char *value, int flags) {
    return Cvar_Get(name, value, flags);
}

static void GI_CvarSet(const char *name, const char *value) {
    Cvar_Set(name, value);
}

static int GI_Argc(void) { return Cmd_Argc(); }
static char *GI_Argv(int n) { return Cmd_Argv(n); }
static const char *GI_Args(void) { return Cmd_Args(); }

static void GI_AddCommand(const char *cmd) {
    /* Register a game command -- the engine will forward it to the game */
    Cmd_AddCommand(cmd, NULL);
}

static int GI_FS_ReadFile(const char *name, void **buf, qboolean quiet) {
    (void)quiet;
    return (int)FS_ReadFile(name, buf);
}

static void GI_SendConsoleCommand(const char *text) {
    Cbuf_AddText(text);
}

/* =========================================================================
 * TIKI wrappers for game_import_t
 *
 * The game DLL uses modelindex (int) as a TIKI handle. These wrappers
 * pass through to the TIKI system using dtiki_t (which is also int).
 * ========================================================================= */

static int GI_NumAnims(int modelindex) { return TIKI_NumAnims(modelindex); }
static int GI_NumSkins(int modelindex) { (void)modelindex; return 1; }
static int GI_NumSurfaces(int modelindex) { return TIKI_NumSurfaces(modelindex); }
static int GI_NumTags(int modelindex) { return TIKI_NumTags(modelindex); }

static qboolean GI_InitCommands(int modelindex, tiki_cmd_t *tiki_cmd) {
    return TIKI_InitCommands(modelindex, tiki_cmd);
}

static void GI_CalculateBounds(int modelindex, float scale, vec3_t mins, vec3_t maxs) {
    TIKI_CalculateBounds(modelindex, scale, mins, maxs);
}

static const char *GI_Anim_NameForNum(int mi, int an) { return TIKI_AnimName(mi, an); }
static int GI_Anim_NumForName(int mi, const char *n) { return TIKI_AnimNumForName(mi, n); }
static int GI_Anim_Random(int mi, const char *n) { return TIKI_AnimRandom(mi, n); }
static int GI_Anim_NumFrames(int mi, int an) { return TIKI_AnimNumFrames(mi, an); }
static float GI_Anim_Time(int mi, int an) { return TIKI_AnimTime(mi, an); }
static int GI_Anim_Flags(int mi, int an) { return TIKI_AnimFlags(mi, an); }
static qboolean GI_Anim_HasCommands(int mi, int an) { return TIKI_AnimHasCommands(mi, an); }

static void GI_Anim_Delta(int mi, int an, vec3_t delta) {
    TIKI_AnimDelta(mi, an, delta);
}

static void GI_Anim_AbsoluteDelta(int mi, int an, vec3_t delta) {
    TIKI_AnimAbsoluteDelta(mi, an, delta);
}

static qboolean GI_Frame_Commands(int mi, int an, int fn, tiki_cmd_t *tc) {
    return TIKI_FrameCommands(mi, an, fn, tc);
}

static void GI_Frame_Delta(int mi, int an, int fn, vec3_t delta) {
    TIKI_FrameDelta(mi, an, fn, delta);
}

static float GI_Frame_Time(int mi, int an, int fn) {
    return TIKI_FrameTime(mi, an, fn);
}

static void GI_Frame_Bounds(int mi, int an, int fn, float s, vec3_t mins, vec3_t maxs) {
    TIKI_FrameBounds(mi, an, fn, s, mins, maxs);
}

static int GI_Surface_NameToNum(int mi, const char *n) { return TIKI_SurfaceNameToNum(mi, n); }
static const char *GI_Surface_NumToName(int mi, int n) { return TIKI_SurfaceNumToName(mi, n); }
static int GI_Surface_Flags(int mi, int n) { return TIKI_SurfaceFlags(mi, n); }
static int GI_Surface_NumSkins(int mi, int n) { (void)mi; (void)n; return 1; }

static int GI_Tag_NumForName(int mi, const char *n) { return TIKI_TagNumForName(mi, n); }
static const char *GI_Tag_NameForNum(int mi, int n) { return TIKI_TagNameForNum(mi, n); }

static orientation_t GI_Tag_Orientation(int mi, int an, int fr, int num,
                                        float scale, int *bt, vec4_t *bq) {
    return TIKI_TagOrientation(mi, an, fr, num, scale, bt, bq);
}

static const char *GI_NameForNum(int mi) { return TIKI_NameForNum(mi); }

/* =========================================================================
 * Fill the game_import_t structure
 * ========================================================================= */

static void SV_InitGameImport(void) {
    memset(&gi, 0, sizeof(gi));

    /* Core */
    gi.Printf = GI_Printf;
    gi.DPrintf = GI_DPrintf;
    gi.DebugPrintf = GI_DPrintf;
    gi.Error = GI_Error;
    gi.Milliseconds = GI_Milliseconds;
    gi.Malloc = GI_Malloc;
    gi.Free = GI_Free;

    /* CVars */
    gi.cvar = GI_Cvar;
    gi.cvar_set = GI_CvarSet;

    /* Commands */
    gi.argc = GI_Argc;
    gi.argv = GI_Argv;
    gi.args = GI_Args;
    gi.AddCommand = GI_AddCommand;

    /* Filesystem */
    gi.FS_ReadFile = GI_FS_ReadFile;
    gi.FS_FreeFile = FS_FreeFile;
    /* TODO: Fill in remaining FS functions */

    gi.SendConsoleCommand = GI_SendConsoleCommand;

    /* TIKI model queries */
    gi.NumAnims = GI_NumAnims;
    gi.NumSkins = GI_NumSkins;
    gi.NumSurfaces = GI_NumSurfaces;
    gi.NumTags = GI_NumTags;
    gi.InitCommands = GI_InitCommands;
    gi.CalculateBounds = GI_CalculateBounds;

    /* Animation queries */
    gi.Anim_NameForNum = GI_Anim_NameForNum;
    gi.Anim_NumForName = GI_Anim_NumForName;
    gi.Anim_Random = GI_Anim_Random;
    gi.Anim_NumFrames = GI_Anim_NumFrames;
    gi.Anim_Time = GI_Anim_Time;
    gi.Anim_Delta = GI_Anim_Delta;
    gi.Anim_AbsoluteDelta = GI_Anim_AbsoluteDelta;
    gi.Anim_Flags = GI_Anim_Flags;
    gi.Anim_HasCommands = GI_Anim_HasCommands;

    /* Frame queries */
    gi.Frame_Commands = GI_Frame_Commands;
    gi.Frame_Delta = GI_Frame_Delta;
    gi.Frame_Time = GI_Frame_Time;
    gi.Frame_Bounds = GI_Frame_Bounds;

    /* Surface queries */
    gi.Surface_NameToNum = GI_Surface_NameToNum;
    gi.Surface_NumToName = GI_Surface_NumToName;
    gi.Surface_Flags = GI_Surface_Flags;
    gi.Surface_NumSkins = GI_Surface_NumSkins;

    /* Tag (bone) queries */
    gi.Tag_NumForName = GI_Tag_NumForName;
    gi.Tag_NameForNum = GI_Tag_NameForNum;
    gi.Tag_Orientation = GI_Tag_Orientation;

    gi.NameForNum = GI_NameForNum;

    /* TODO: Fill in remaining game_import_t functions:
     *   - Collision (trace, pointcontents, linkentity, etc.)
     *   - Config strings
     *   - Sound
     *   - Alias system
     *   - Debug lines
     */

    Com_Printf("SV_InitGameImport: %d function pointers populated\n",
               (int)(sizeof(gi) / sizeof(void *)));
}

/* =========================================================================
 * Load the game module
 * ========================================================================= */

void SV_InitGameProgs(void) {
    GetGameAPI_t GetGameAPI;

    Com_Printf("--- SV_InitGameProgs ---\n");

    SV_InitGameImport();

    /* Try to load gamex86.dll from game directory */
    char dllpath[MAX_OSPATH];
    snprintf(dllpath, sizeof(dllpath), "%s/gamex86.dll", FAKK_GAME_DIR);

    gameLib = Sys_LoadDll(dllpath);
    if (!gameLib) {
        /* Try alternate path */
        snprintf(dllpath, sizeof(dllpath), "gamex86.dll");
        gameLib = Sys_LoadDll(dllpath);
    }

    if (gameLib) {
        GetGameAPI = (GetGameAPI_t)Sys_GetProcAddress(gameLib, "GetGameAPI");
        if (!GetGameAPI) {
            Com_Error(ERR_FATAL, "SV_InitGameProgs: GetGameAPI not found in %s", dllpath);
            return;
        }

        ge = GetGameAPI(&gi);
        if (!ge) {
            Com_Error(ERR_FATAL, "SV_InitGameProgs: GetGameAPI returned NULL");
            return;
        }

        if (ge->apiversion != GAME_API_VERSION) {
            Com_Error(ERR_FATAL, "SV_InitGameProgs: game API version mismatch (%d vs %d)",
                      ge->apiversion, GAME_API_VERSION);
            return;
        }

        Com_Printf("Game module loaded: %s (API version %d)\n", dllpath, ge->apiversion);
    } else {
        Com_Printf("WARNING: Could not load game module (gamex86.dll)\n");
        Com_Printf("  Game logic will not be available until the module is built.\n");
        ge = NULL;
    }
}

void SV_ShutdownGameProgs(void) {
    if (ge) {
        ge->Shutdown();
        ge = NULL;
    }
    if (gameLib) {
        Sys_UnloadDll(gameLib);
        gameLib = NULL;
    }
}

/* =========================================================================
 * Game module accessors
 * ========================================================================= */

game_export_t *SV_GetGameExport(void) {
    return ge;
}
