/*
 * cl_cgame.c -- Client game module interface
 *
 * Loads cgamex86.dll and exchanges function pointer tables.
 * The engine fills clientGameImport_t with rendering, sound, collision,
 * and TIKI functions, calls GetCGameAPI(), and receives clientGameExport_t.
 *
 * Original binary calls:
 *   LoadLibraryA("fakk/cgamex86.dll")
 *   GetProcAddress(handle, "GetCGameAPI")
 *   cgExport = GetCGameAPI()
 *   cgExport->CG_Init(&cgImport, ...)
 */

#include "../common/qcommon.h"
#include "../common/cg_public.h"
#include "../common/alias.h"
#include "../collision/cm_local.h"
#include "../sound/snd_local.h"
#include "../tiki/tiki.h"

/* Global client game interface pointers */
static clientGameImport_t   cgi;    /* engine -> cgame */
static clientGameExport_t   *cge;   /* cgame -> engine */
static void                 *cgameLib;

/* =========================================================================
 * External declarations from renderer
 * ========================================================================= */

extern void R_ClearScene(void);
extern void R_RenderScene(const refdef_t *fd);
extern void R_LoadWorldMap(const char *mapname);
extern qhandle_t R_RegisterModel(const char *name);
extern qhandle_t R_RegisterSkin(const char *name);
extern qhandle_t R_RegisterShader(const char *name);
extern qhandle_t R_RegisterShaderNoMip(const char *name);
extern void R_AddRefEntityToScene(refEntity_t *ent);
extern void R_AddRefSpriteToScene(refEntity_t *ent);
extern void R_AddLightToScene(vec3_t origin, float intensity, float r, float g, float b, int type);
extern void R_AddPolyToScene(qhandle_t hShader, int numVerts, const polyVert_t *verts, int renderfx);
extern void R_SetColor(const float *rgba);
extern void R_DrawStretchPic(float x, float y, float w, float h,
                              float s1, float t1, float s2, float t2, qhandle_t hShader);
extern refEntity_t *R_GetRenderEntity(int entityNumber);
extern void R_ModelBounds(clipHandle_t model, vec3_t mins, vec3_t maxs);
extern float R_ModelRadius(clipHandle_t model);
extern float R_Noise(float x, float y, float z, float t);
extern void R_DebugLine(vec3_t start, vec3_t end, float r, float g, float b, float alpha);
extern void R_SwipeBegin(float thistime, float life, qhandle_t shader);
extern void R_SwipePoint(vec3_t p1, vec3_t p2, float time);
extern void R_SwipeEnd(void);
extern int  R_GetShaderWidth(qhandle_t shader);
extern int  R_GetShaderHeight(qhandle_t shader);
extern void R_DrawBox(float x, float y, float w, float h);
extern void R_BeginRegistration(void);
extern void R_EndRegistration(void);
extern void R_GetGlconfig(glconfig_t *config);
extern float *R_GetCameraOffset(qboolean *lookactive, qboolean *resetview);

/* =========================================================================
 * Core stubs for clientGameImport_t
 * ========================================================================= */

static void CGI_Printf(const char *fmt, ...) {
    va_list args;
    char buf[4096];
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Com_Printf("%s", buf);
}

static void CGI_Error(int level, const char *fmt, ...) {
    va_list args;
    char buf[4096];
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Com_Error(level, "%s", buf);
}

static void *CGI_Malloc(int size) {
    return Z_TagMalloc(size, TAG_CGAME);
}

static void CGI_Free(void *block) {
    Z_Free(block);
}

static void CGI_CvarSet(const char *name, const char *value) {
    Cvar_Set(name, value);
}

/* =========================================================================
 * Filesystem wrappers
 * ========================================================================= */

static int CGI_FS_ReadFile(const char *name, void **buf, qboolean quiet) {
    (void)quiet;
    return (int)FS_ReadFile(name, buf);
}

static void CGI_FS_WriteTextFile(const char *qpath, const void *buffer, int size) {
    FS_WriteFile(qpath, buffer, size);
}

static void CGI_UpdateLoadingScreen(void) {
    /* TODO: Redraw loading screen during level load */
}

static void CGI_AddCommand(const char *cmd) {
    Cmd_AddCommand(cmd, NULL);
}

static void CGI_SendClientCommand(const char *s) {
    /* For single-player loopback, execute on server directly */
    extern void SV_ExecuteClientCommandStr(int clientNum, const char *s);
    SV_ExecuteClientCommandStr(0, s);
}

/* =========================================================================
 * Sound wrappers for clientGameImport_t
 *
 * The cgame import uses slightly different signatures than snd_local.h.
 * These wrappers bridge the gap.
 * ========================================================================= */

static void CGI_S_StartSound(vec3_t origin, int entnum, int entchannel,
                              sfxHandle_t sfx, float volume, float min_dist) {
    S_StartSound(origin, entnum, entchannel, sfx, volume, min_dist, 1.0f);
}

static void CGI_S_StartLocalSound(const char *sound_name) {
    S_StartLocalSoundName(sound_name);
}

/* =========================================================================
 * Client state queries
 *
 * The cgame needs access to game state, snapshots, and user commands.
 * These are managed by the client main loop (cl_main.c).
 * ========================================================================= */

static gameState_t      cl_gameState;
static snapshot_t       cl_snapshot;
static int              cl_currentSnapshot;
static int              cl_currentServerTime;

/* =========================================================================
 * Snapshot reception -- called from server (loopback fast path)
 * ========================================================================= */

void CL_SetSnapshot(int serverTime, int snapNum,
                    const playerState_t *ps,
                    const entityState_t *entities, int numEntities) {
    cl_currentServerTime = serverTime;
    cl_currentSnapshot = snapNum;

    memset(&cl_snapshot, 0, sizeof(cl_snapshot));
    cl_snapshot.serverTime = serverTime;

    if (ps) {
        cl_snapshot.ps = *ps;
    }

    if (entities && numEntities > 0) {
        if (numEntities > MAX_ENTITIES_IN_SNAPSHOT) {
            numEntities = MAX_ENTITIES_IN_SNAPSHOT;
        }
        memcpy(cl_snapshot.entities, entities,
               numEntities * sizeof(entityState_t));
        cl_snapshot.numEntities = numEntities;
    }
}

static void CGI_GetGameState(gameState_t *gs) {
    if (gs) *gs = cl_gameState;
}

static int CGI_GetSnapshot(int snapshotNumber, snapshot_t *snapshot) {
    if (!snapshot) return 0;
    *snapshot = cl_snapshot;
    return 1;
}

static void CGI_GetCurrentSnapshotNumber(int *snapshotNumber, int *serverTime) {
    if (snapshotNumber) *snapshotNumber = cl_currentSnapshot;
    if (serverTime) *serverTime = cl_currentServerTime;
}

static void CGI_GetGlconfig(glconfig_t *glconfig) {
    R_GetGlconfig(glconfig);
}

static qboolean CGI_GetParseEntityState(int parseEntityNumber, entityState_t *state) {
    (void)parseEntityNumber; (void)state;
    return qfalse;
}

static int CGI_GetCurrentCmdNumber(void) {
    return 0;
}

static qboolean CGI_GetUserCmd(int cmdNumber, usercmd_t *ucmd) {
    (void)cmdNumber;
    if (ucmd) memset(ucmd, 0, sizeof(*ucmd));
    return qfalse;
}

static qboolean CGI_GetServerCommand(int serverCommandNumber) {
    (void)serverCommandNumber;
    return qfalse;
}

/* =========================================================================
 * TIKI wrappers for clientGameImport_t
 * ========================================================================= */

static int CGI_TIKI_GetHandle(qhandle_t handle) {
    /* Converts a renderer model handle to a TIKI handle.
     * For now, pass through since both are int indices. */
    return (int)handle;
}

static int CGI_NumAnims(int h) { return TIKI_NumAnims(h); }
static int CGI_NumSkins(int h) { (void)h; return 1; }
static int CGI_NumSurfaces(int h) { return TIKI_NumSurfaces(h); }
static int CGI_NumTags(int h) { return TIKI_NumTags(h); }

static qboolean CGI_InitCommands(int h, tiki_cmd_t *tc) {
    return TIKI_InitCommands(h, tc);
}

static void CGI_CalculateBounds(int h, float scale, vec3_t mins, vec3_t maxs) {
    TIKI_CalculateBounds(h, scale, mins, maxs);
}

static const char *CGI_TIKI_NameForNum(int h) { return TIKI_NameForNum(h); }

static const char *CGI_Anim_NameForNum(int h, int an) { return TIKI_AnimName(h, an); }
static int CGI_Anim_NumForName(int h, const char *n) { return TIKI_AnimNumForName(h, n); }
static int CGI_Anim_Random(int h, const char *n) { return TIKI_AnimRandom(h, n); }
static int CGI_Anim_NumFrames(int h, int an) { return TIKI_AnimNumFrames(h, an); }
static float CGI_Anim_Time(int h, int an) { return TIKI_AnimTime(h, an); }
static int CGI_Anim_Flags(int h, int an) { return TIKI_AnimFlags(h, an); }
static int CGI_Anim_CrossblendTime(int h, int an) { return TIKI_AnimCrossblendTime(h, an); }
static qboolean CGI_Anim_HasCommands(int h, int an) { return TIKI_AnimHasCommands(h, an); }

static void CGI_Anim_Delta(int h, int an, vec3_t delta) {
    TIKI_AnimDelta(h, an, delta);
}

static qboolean CGI_Frame_Commands(int h, int an, int fn, tiki_cmd_t *tc) {
    return TIKI_FrameCommands(h, an, fn, tc);
}

static void CGI_Frame_Delta(int h, int an, int fn, vec3_t delta) {
    TIKI_FrameDelta(h, an, fn, delta);
}

static float CGI_Frame_Time(int h, int an, int fn) {
    return TIKI_FrameTime(h, an, fn);
}

static void CGI_Frame_Bounds(int h, int an, int fn, float s, vec3_t mins, vec3_t maxs) {
    TIKI_FrameBounds(h, an, fn, s, mins, maxs);
}

static float CGI_Frame_Radius(int h, int an, int fn) {
    return TIKI_FrameRadius(h, an, fn);
}

static int CGI_Surface_NameToNum(int h, const char *n) { return TIKI_SurfaceNameToNum(h, n); }
static const char *CGI_Surface_NumToName(int h, int n) { return TIKI_SurfaceNumToName(h, n); }
static int CGI_Surface_Flags(int h, int n) { return TIKI_SurfaceFlags(h, n); }
static int CGI_Surface_NumSkins(int h, int n) { (void)h; (void)n; return 1; }

static int CGI_Tag_NumForName(int h, const char *n) { return TIKI_TagNumForName(h, n); }
static const char *CGI_Tag_NameForNum(int h, int n) { return TIKI_TagNameForNum(h, n); }

static orientation_t CGI_Tag_Orientation(int h, int an, int fr, int num,
                                          float scale, int *bt, vec4_t *bq) {
    return TIKI_TagOrientation(h, an, fr, num, scale, bt, bq);
}

/* =========================================================================
 * Fill the clientGameImport_t structure -- ALL function pointers
 * ========================================================================= */

static void CL_InitCGameImport(void) {
    memset(&cgi, 0, sizeof(cgi));

    cgi.apiversion = CGAME_IMPORT_API_VERSION;

    /* --- Core --- */
    cgi.Printf = CGI_Printf;
    cgi.DPrintf = CGI_Printf;
    cgi.DebugPrintf = CGI_Printf;
    cgi.Malloc = CGI_Malloc;
    cgi.Free = CGI_Free;
    cgi.Error = CGI_Error;
    cgi.Milliseconds = Sys_Milliseconds;

    /* --- CVars --- */
    cgi.Cvar_Get = Cvar_Get;
    cgi.Cvar_Set = CGI_CvarSet;

    /* --- Commands --- */
    cgi.Argc = Cmd_Argc;
    cgi.Argv = Cmd_Argv;
    cgi.Args = Cmd_Args;
    cgi.AddCommand = CGI_AddCommand;

    /* --- Filesystem --- */
    cgi.FS_ReadFile = CGI_FS_ReadFile;
    cgi.FS_FreeFile = FS_FreeFile;
    cgi.FS_WriteFile = FS_WriteFile;
    cgi.FS_WriteTextFile = CGI_FS_WriteTextFile;
    cgi.SendConsoleCommand = Cbuf_AddText;
    cgi.UpdateLoadingScreen = CGI_UpdateLoadingScreen;

    /* --- Client --- */
    cgi.SendClientCommand = CGI_SendClientCommand;

    /* --- Collision Model --- */
    cgi.CM_LoadMap = CM_LoadMap;
    cgi.CM_InlineModel = CM_InlineModel;
    cgi.CM_NumInlineModels = CM_NumInlineModels;
    cgi.CM_PointContents = CM_PointContents;
    cgi.CM_TransformedPointContents = CM_TransformedPointContents;
    cgi.CM_BoxTrace = CM_BoxTrace;
    cgi.CM_TransformedBoxTrace = CM_TransformedBoxTrace;
    cgi.CM_TempBoxModel = CM_TempBoxModel;
    cgi.CM_MarkFragments = CM_MarkFragments;

    /* --- Sound --- */
    cgi.S_StartSound = CGI_S_StartSound;
    cgi.S_StartLocalSound = CGI_S_StartLocalSound;
    cgi.S_StopSound = S_StopSound;
    cgi.S_ClearLoopingSounds = S_ClearLoopingSounds;
    cgi.S_AddLoopingSound = S_AddLoopingSound;
    cgi.S_Respatialize = S_Respatialize;
    cgi.S_BeginRegistration = S_BeginRegistration;
    cgi.S_RegisterSound = S_RegisterSound;
    cgi.S_EndRegistration = S_EndRegistration;
    cgi.S_UpdateEntity = S_UpdateEntity;
    cgi.S_SetReverb = S_SetReverb;
    cgi.S_SetGlobalAmbientVolumeLevel = S_SetGlobalAmbientVolumeLevel;

    /* --- Music --- */
    cgi.MUSIC_NewSoundtrack = MUSIC_NewSoundtrack;
    cgi.MUSIC_UpdateMood = MUSIC_UpdateMood;
    cgi.MUSIC_UpdateVolume = MUSIC_UpdateVolume;

    /* --- Lip sync --- */
    cgi.get_lip_length = S_GetLipLength;
    cgi.get_lip_amplitudes = S_GetLipAmplitudes;

    /* --- Camera --- */
    cgi.get_camera_offset = R_GetCameraOffset;

    /* --- Renderer --- */
    cgi.BeginRegistration = R_BeginRegistration;
    cgi.EndRegistration = R_EndRegistration;
    cgi.R_ClearScene = R_ClearScene;
    cgi.R_RenderScene = R_RenderScene;
    cgi.R_LoadWorldMap = R_LoadWorldMap;
    cgi.R_RegisterModel = R_RegisterModel;
    cgi.R_RegisterSkin = R_RegisterSkin;
    cgi.R_RegisterShader = R_RegisterShader;
    cgi.R_RegisterShaderNoMip = R_RegisterShaderNoMip;
    cgi.R_AddRefEntityToScene = R_AddRefEntityToScene;
    cgi.R_AddRefSpriteToScene = R_AddRefSpriteToScene;
    cgi.R_AddLightToScene = R_AddLightToScene;
    cgi.R_AddPolyToScene = R_AddPolyToScene;
    cgi.R_SetColor = (void (*)(const vec4_t))R_SetColor;
    cgi.R_DrawStretchPic = R_DrawStretchPic;
    cgi.R_GetRenderEntity = R_GetRenderEntity;
    cgi.R_ModelBounds = R_ModelBounds;
    cgi.R_ModelRadius = R_ModelRadius;
    cgi.R_Noise = R_Noise;
    cgi.R_DebugLine = R_DebugLine;

    /* --- Swipes (weapon trail effects) --- */
    cgi.R_SwipeBegin = R_SwipeBegin;
    cgi.R_SwipePoint = R_SwipePoint;
    cgi.R_SwipeEnd = R_SwipeEnd;
    cgi.R_GetShaderWidth = R_GetShaderWidth;
    cgi.R_GetShaderHeight = R_GetShaderHeight;
    cgi.R_DrawBox = R_DrawBox;

    /* --- Client state --- */
    cgi.GetGameState = CGI_GetGameState;
    cgi.GetSnapshot = CGI_GetSnapshot;
    cgi.GetCurrentSnapshotNumber = CGI_GetCurrentSnapshotNumber;
    cgi.GetGlconfig = CGI_GetGlconfig;
    cgi.GetParseEntityState = CGI_GetParseEntityState;
    cgi.GetCurrentCmdNumber = CGI_GetCurrentCmdNumber;
    cgi.GetUserCmd = CGI_GetUserCmd;
    cgi.GetServerCommand = CGI_GetServerCommand;

    /* --- Alias system (global) --- */
    cgi.Alias_Add = Alias_GlobalAdd;
    cgi.Alias_FindRandom = Alias_GlobalFindRandom;
    cgi.Alias_Dump = Alias_GlobalDump;
    cgi.Alias_Clear = Alias_GlobalClear;

    /* --- TIKI queries --- */
    cgi.TIKI_GetHandle = CGI_TIKI_GetHandle;
    cgi.NumAnims = CGI_NumAnims;
    cgi.NumSkins = CGI_NumSkins;
    cgi.NumSurfaces = CGI_NumSurfaces;
    cgi.NumTags = CGI_NumTags;
    cgi.InitCommands = CGI_InitCommands;
    cgi.CalculateBounds = CGI_CalculateBounds;
    cgi.FlushAll = TIKI_FlushAll;
    cgi.TIKI_NameForNum = CGI_TIKI_NameForNum;

    /* --- Animation queries --- */
    cgi.Anim_NameForNum = CGI_Anim_NameForNum;
    cgi.Anim_NumForName = CGI_Anim_NumForName;
    cgi.Anim_Random = CGI_Anim_Random;
    cgi.Anim_NumFrames = CGI_Anim_NumFrames;
    cgi.Anim_Time = CGI_Anim_Time;
    cgi.Anim_Delta = CGI_Anim_Delta;
    cgi.Anim_Flags = CGI_Anim_Flags;
    cgi.Anim_CrossblendTime = CGI_Anim_CrossblendTime;
    cgi.Anim_HasCommands = CGI_Anim_HasCommands;

    /* --- Frame queries --- */
    cgi.Frame_Commands = CGI_Frame_Commands;
    cgi.Frame_Delta = CGI_Frame_Delta;
    cgi.Frame_Time = CGI_Frame_Time;
    cgi.Frame_Bounds = CGI_Frame_Bounds;
    cgi.Frame_Radius = CGI_Frame_Radius;

    /* --- Surface queries --- */
    cgi.Surface_NameToNum = CGI_Surface_NameToNum;
    cgi.Surface_NumToName = CGI_Surface_NumToName;
    cgi.Surface_Flags = CGI_Surface_Flags;
    cgi.Surface_NumSkins = CGI_Surface_NumSkins;

    /* --- Tag (bone) queries --- */
    cgi.Tag_NumForName = CGI_Tag_NumForName;
    cgi.Tag_NameForNum = CGI_Tag_NameForNum;
    cgi.Tag_Orientation = CGI_Tag_Orientation;

    /* --- TIKI alias system --- */
    cgi.TIKI_Alias_Add = Alias_ModelAdd;
    cgi.TIKI_Alias_FindRandom = Alias_ModelFindRandom;
    cgi.TIKI_Alias_Dump = Alias_ModelDump;
    cgi.TIKI_Alias_Clear = Alias_ModelClear;
    cgi.TIKI_Alias_FindDialog = Alias_ModelFindDialog;

    Com_Printf("CL_InitCGameImport: all %d function pointers populated\n",
               (int)(sizeof(cgi) / sizeof(void *)));
}

/* =========================================================================
 * Load the client game module
 * ========================================================================= */

void CL_InitCGame(void) {
    GetCGameAPI_t GetCGameAPI;

    Com_Printf("--- CL_InitCGame ---\n");

    CL_InitCGameImport();

    /* Try to load cgamex86.dll */
    char dllpath[MAX_OSPATH];
    snprintf(dllpath, sizeof(dllpath), "%s/cgamex86.dll", FAKK_GAME_DIR);

    cgameLib = Sys_LoadDll(dllpath);
    if (!cgameLib) {
        snprintf(dllpath, sizeof(dllpath), "cgamex86.dll");
        cgameLib = Sys_LoadDll(dllpath);
    }

    if (cgameLib) {
        GetCGameAPI = (GetCGameAPI_t)Sys_GetProcAddress(cgameLib, "GetCGameAPI");
        if (!GetCGameAPI) {
            Com_Error(ERR_FATAL, "CL_InitCGame: GetCGameAPI not found in %s", dllpath);
            return;
        }

        cge = GetCGameAPI();
        if (!cge) {
            Com_Error(ERR_FATAL, "CL_InitCGame: GetCGameAPI returned NULL");
            return;
        }

        /* Initialize the cgame module */
        cge->CG_Init(&cgi, 0, 0);

        Com_Printf("Client game module loaded: %s\n", dllpath);
    } else {
        Com_Printf("WARNING: Could not load client game module (cgamex86.dll)\n");
        cge = NULL;
    }
}

void CL_ShutdownCGame(void) {
    if (cge) {
        cge->CG_Shutdown();
        cge = NULL;
    }
    if (cgameLib) {
        Sys_UnloadDll(cgameLib);
        cgameLib = NULL;
    }
}

/* =========================================================================
 * Client game frame
 * ========================================================================= */

void CL_CGameFrame(int serverTime) {
    if (!cge) return;
    cl_currentServerTime = serverTime;
    cge->CG_DrawActiveFrame(serverTime, STEREO_CENTER, qfalse);
}

void CL_CGameDraw2D(void) {
    if (!cge) return;
    cge->CG_Draw2D();
}

clientGameExport_t *CL_GetCGameExport(void) {
    return cge;
}
