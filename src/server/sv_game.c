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
#include "../common/alias.h"
#include "../collision/cm_local.h"
#include "../sound/snd_local.h"
#include "../tiki/tiki.h"

/* Global game interface pointers */
static game_import_t    gi;     /* engine -> game */
static game_export_t    *ge;    /* game -> engine */
static void             *gameLib;

/* =========================================================================
 * External declarations from sv_world.c
 * ========================================================================= */

extern void SV_LocateGameData(gentity_t *gEnts, int numGEntities,
                               int sizeofGEntity_t, playerState_t *clients,
                               int sizeofGameClient);
extern void SV_LinkEntity(gentity_t *ent);
extern void SV_UnlinkEntity(gentity_t *ent);
extern int  SV_AreaEntities(vec3_t mins, vec3_t maxs, int *list, int maxcount);
extern void SV_ClipToEntity(trace_t *trace, const vec3_t start, const vec3_t mins,
                             const vec3_t maxs, const vec3_t end, int entityNum,
                             int contentmask);
extern void SV_Trace(trace_t *result, const vec3_t start, const vec3_t mins,
                      const vec3_t maxs, const vec3_t end, int passEntityNum,
                      int contentmask, qboolean cylinder);
extern int  SV_PointContents(const vec3_t p, int passEntityNum);
extern void SV_SetBrushModel(gentity_t *ent, const char *name);
extern void SV_SetConfigstring(int index, const char *val);
extern char *SV_GetConfigstring(int index);
extern void SV_SetUserinfo(int index, const char *val);
extern void SV_GetUserinfo(int index, char *buffer, int bufferSize);
extern void SV_SendServerCommand(int clientnum, const char *fmt, ...);
extern int  SV_ModelIndex(const char *name);
extern int  SV_SoundIndex(const char *name);
extern int  SV_ImageIndex(const char *name);
extern int  SV_ItemIndex(const char *name);
extern const char *SV_GameDir(void);
extern qboolean SV_IsModel(int index);
extern void SV_SetModel(gentity_t *ent, const char *name);
extern void SV_SetLightStyle(int i, const char *data);
extern void SV_SetFarPlane(int farplane);
extern void SV_SetSkyPortal(qboolean skyportal);
extern void SV_DebugGraph(float value, int color);
extern void SV_Centerprintf(gentity_t *ent, const char *fmt, ...);
extern void SV_Locationprintf(gentity_t *ent, int x, int y, const char *fmt, ...);
extern unsigned short SV_CalcCRC(const unsigned char *start, int count);
extern const char *SV_GetArchiveFileName(const char *filename, const char *extension);

/* =========================================================================
 * Core stubs for game_import_t
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
    Cmd_AddCommand(cmd, NULL);
}

static void GI_SendConsoleCommand(const char *text) {
    Cbuf_AddText(text);
}

/* =========================================================================
 * Filesystem wrappers
 * ========================================================================= */

static int GI_FS_ReadFile(const char *name, void **buf, qboolean quiet) {
    (void)quiet;
    return (int)FS_ReadFile(name, buf);
}

static fileHandle_t GI_FS_FOpenFileWrite(const char *qpath) {
    fileHandle_t f;
    FS_FOpenFileWrite(qpath, &f);
    return f;
}

static fileHandle_t GI_FS_FOpenFileAppend(const char *filename) {
    extern int FS_FOpenFileAppend(const char *filename, fileHandle_t *file);
    fileHandle_t f;
    FS_FOpenFileAppend(filename, &f);
    return f;
}

static char *GI_FS_PrepFileWrite(const char *filename) {
    /* Returns the OS path for writing -- used for save files */
    static char path[MAX_OSPATH];
    snprintf(path, sizeof(path), "%s/%s", FAKK_GAME_DIR, filename);
    return path;
}

/* =========================================================================
 * Sound wrappers for game_import_t
 * ========================================================================= */

static void GI_Sound(vec3_t *org, int entnum, int channel, const char *sound_name,
                     float volume, float attenuation) {
    sfxHandle_t sfx = S_RegisterSound(sound_name);
    S_StartSound(org ? *org : NULL, entnum, channel, sfx, volume, attenuation, 1.0f);
}

static void GI_StopSound(int entnum, int channel) {
    S_StopSound(entnum, channel);
}

/* =========================================================================
 * TIKI wrappers for game_import_t
 * ========================================================================= */

static int GI_NumAnims(int mi) { return TIKI_NumAnims(mi); }
static int GI_NumSkins(int mi) { (void)mi; return 1; }
static int GI_NumSurfaces(int mi) { return TIKI_NumSurfaces(mi); }
static int GI_NumTags(int mi) { return TIKI_NumTags(mi); }

static qboolean GI_InitCommands(int mi, tiki_cmd_t *tc) {
    return TIKI_InitCommands(mi, tc);
}

static void GI_CalculateBounds(int mi, float scale, vec3_t mins, vec3_t maxs) {
    TIKI_CalculateBounds(mi, scale, mins, maxs);
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
 * Collision wrappers
 * ========================================================================= */

static void GI_AdjustAreaPortalState(gentity_t *ent, qboolean open) {
    /* The game passes an entity; we need its area for the CM function */
    if (!ent) return;
    /* Use entity's area number for both sides of the portal */
    CM_AdjustAreaPortalState(ent->areanum, ent->areanum, open);
}

/* =========================================================================
 * Debug lines
 * ========================================================================= */

#define MAX_DEBUG_LINES 256
static debugline_t  debug_lines[MAX_DEBUG_LINES];
static int          num_debug_lines;
static debugline_t  *debug_lines_ptr = debug_lines;

/* =========================================================================
 * Fill the game_import_t structure -- ALL function pointers
 * ========================================================================= */

static void SV_InitGameImport(void) {
    memset(&gi, 0, sizeof(gi));

    /* --- Printing --- */
    gi.Printf = GI_Printf;
    gi.DPrintf = GI_DPrintf;
    gi.DebugPrintf = GI_DPrintf;
    gi.Error = GI_Error;

    /* --- Timing --- */
    gi.Milliseconds = GI_Milliseconds;

    /* --- Memory --- */
    gi.Malloc = GI_Malloc;
    gi.Free = GI_Free;

    /* --- CVars --- */
    gi.cvar = GI_Cvar;
    gi.cvar_set = GI_CvarSet;

    /* --- Commands --- */
    gi.argc = GI_Argc;
    gi.argv = GI_Argv;
    gi.args = GI_Args;
    gi.AddCommand = GI_AddCommand;

    /* --- Filesystem --- */
    gi.FS_ReadFile = GI_FS_ReadFile;
    gi.FS_FreeFile = FS_FreeFile;
    gi.FS_WriteFile = FS_WriteFile;
    gi.FS_FOpenFileWrite = GI_FS_FOpenFileWrite;
    gi.FS_FOpenFileAppend = GI_FS_FOpenFileAppend;
    gi.FS_PrepFileWrite = GI_FS_PrepFileWrite;
    gi.FS_Write = FS_Write;
    gi.FS_Read = FS_Read;
    gi.FS_FCloseFile = FS_FCloseFile;
    gi.FS_FTell = FS_FTell;
    gi.FS_FSeek = FS_Seek;
    gi.FS_Flush = FS_Flush;

    gi.GetArchiveFileName = SV_GetArchiveFileName;
    gi.SendConsoleCommand = GI_SendConsoleCommand;
    gi.DebugGraph = SV_DebugGraph;

    /* --- Server --- */
    gi.SendServerCommand = SV_SendServerCommand;
    gi.setConfigstring = SV_SetConfigstring;
    gi.getConfigstring = SV_GetConfigstring;
    gi.setUserinfo = SV_SetUserinfo;
    gi.getUserinfo = SV_GetUserinfo;

    /* --- Collision --- */
    gi.SetBrushModel = SV_SetBrushModel;
    gi.trace = SV_Trace;
    gi.pointcontents = SV_PointContents;
    gi.pointbrushnum = SV_PointContents;   /* same as pointcontents for now */
    gi.inPVS = CM_InPVS;
    gi.inPVSIgnorePortals = CM_InPVSIgnorePortals;
    gi.AdjustAreaPortalState = GI_AdjustAreaPortalState;
    gi.AreasConnected = CM_AreasConnected;

    /* --- Entity linking --- */
    gi.linkentity = SV_LinkEntity;
    gi.unlinkentity = SV_UnlinkEntity;
    gi.AreaEntities = SV_AreaEntities;
    gi.ClipToEntity = SV_ClipToEntity;

    /* --- Resource indexing --- */
    gi.imageindex = SV_ImageIndex;
    gi.itemindex = SV_ItemIndex;
    gi.soundindex = SV_SoundIndex;
    gi.modelindex = SV_ModelIndex;

    /* --- Rendering --- */
    gi.SetLightStyle = SV_SetLightStyle;
    gi.GameDir = SV_GameDir;
    gi.IsModel = SV_IsModel;
    gi.setmodel = SV_SetModel;

    /* --- TIKI model queries --- */
    gi.NumAnims = GI_NumAnims;
    gi.NumSkins = GI_NumSkins;
    gi.NumSurfaces = GI_NumSurfaces;
    gi.NumTags = GI_NumTags;
    gi.InitCommands = GI_InitCommands;
    gi.CalculateBounds = GI_CalculateBounds;

    /* --- Animation queries --- */
    gi.Anim_NameForNum = GI_Anim_NameForNum;
    gi.Anim_NumForName = GI_Anim_NumForName;
    gi.Anim_Random = GI_Anim_Random;
    gi.Anim_NumFrames = GI_Anim_NumFrames;
    gi.Anim_Time = GI_Anim_Time;
    gi.Anim_Delta = GI_Anim_Delta;
    gi.Anim_AbsoluteDelta = GI_Anim_AbsoluteDelta;
    gi.Anim_Flags = GI_Anim_Flags;
    gi.Anim_HasCommands = GI_Anim_HasCommands;

    /* --- Frame queries --- */
    gi.Frame_Commands = GI_Frame_Commands;
    gi.Frame_Delta = GI_Frame_Delta;
    gi.Frame_Time = GI_Frame_Time;
    gi.Frame_Bounds = GI_Frame_Bounds;

    /* --- Surface queries --- */
    gi.Surface_NameToNum = GI_Surface_NameToNum;
    gi.Surface_NumToName = GI_Surface_NumToName;
    gi.Surface_Flags = GI_Surface_Flags;
    gi.Surface_NumSkins = GI_Surface_NumSkins;

    /* --- Tag (bone) queries --- */
    gi.Tag_NumForName = GI_Tag_NumForName;
    gi.Tag_NameForNum = GI_Tag_NameForNum;
    gi.Tag_Orientation = GI_Tag_Orientation;

    /* --- Alias system --- */
    gi.Alias_Add = Alias_ModelAdd;
    gi.Alias_FindRandom = Alias_ModelFindRandom;
    gi.Alias_Dump = Alias_ModelDump;
    gi.Alias_Clear = Alias_ModelClear;
    gi.Alias_FindDialog = Alias_ModelFindDialog;
    gi.Alias_GetList = Alias_ModelGetList;
    gi.Alias_UpdateDialog = Alias_ModelUpdateDialog;
    gi.Alias_AddActorDialog = Alias_ModelAddActorDialog;

    gi.NameForNum = GI_NameForNum;

    /* --- Global alias system --- */
    gi.GlobalAlias_Add = Alias_GlobalAdd;
    gi.GlobalAlias_FindRandom = Alias_GlobalFindRandom;
    gi.GlobalAlias_Dump = Alias_GlobalDump;
    gi.GlobalAlias_Clear = Alias_GlobalClear;

    /* --- Screen printing --- */
    gi.centerprintf = SV_Centerprintf;
    gi.locationprintf = SV_Locationprintf;

    /* --- Sound --- */
    gi.Sound = GI_Sound;
    gi.StopSound = GI_StopSound;
    gi.SoundLength = S_SoundLength;
    gi.SoundAmplitudes = S_SoundAmplitudes;

    /* --- CRC --- */
    gi.CalcCRC = SV_CalcCRC;

    /* --- Debug --- */
    gi.DebugLines = &debug_lines_ptr;
    gi.numDebugLines = &num_debug_lines;

    /* --- Entity management --- */
    gi.LocateGameData = SV_LocateGameData;

    /* --- Rendering control --- */
    gi.SetFarPlane = SV_SetFarPlane;
    gi.SetSkyPortal = SV_SetSkyPortal;

    Com_Printf("SV_InitGameImport: all %d function pointers populated\n",
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
