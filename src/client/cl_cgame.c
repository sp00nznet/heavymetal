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
#include "../sound/snd_local.h"
#include "../tiki/tiki.h"

/* Global client game interface pointers */
static clientGameImport_t   cgi;    /* engine -> cgame */
static clientGameExport_t   *cge;   /* cgame -> engine */
static void                 *cgameLib;

/* =========================================================================
 * Stub implementations for clientGameImport_t functions
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

static int CGI_FS_ReadFile(const char *name, void **buf, qboolean quiet) {
    (void)quiet;
    return (int)FS_ReadFile(name, buf);
}

/* =========================================================================
 * TIKI wrappers for clientGameImport_t
 *
 * The cgame DLL uses TIKI_GetHandle(qhandle_t) to get a tikihandle,
 * then queries animations/surfaces/tags via that handle.
 * ========================================================================= */

static int CGI_TIKI_GetHandle(qhandle_t handle) {
    /* In the original, this converts a renderer model handle to a TIKI handle.
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
 * Fill the clientGameImport_t structure
 * ========================================================================= */

static void CL_InitCGameImport(void) {
    memset(&cgi, 0, sizeof(cgi));

    cgi.apiversion = CGAME_IMPORT_API_VERSION;

    /* Core */
    cgi.Printf = CGI_Printf;
    cgi.DPrintf = CGI_Printf;
    cgi.DebugPrintf = CGI_Printf;
    cgi.Malloc = CGI_Malloc;
    cgi.Free = CGI_Free;
    cgi.Error = CGI_Error;
    cgi.Milliseconds = Sys_Milliseconds;

    /* CVars */
    cgi.Cvar_Get = Cvar_Get;
    cgi.Cvar_Set = (void (*)(const char*, const char*))Cvar_Set;

    /* Commands */
    cgi.Argc = Cmd_Argc;
    cgi.Argv = Cmd_Argv;
    cgi.Args = Cmd_Args;

    /* Filesystem */
    cgi.FS_ReadFile = CGI_FS_ReadFile;
    cgi.FS_FreeFile = FS_FreeFile;

    /* TIKI queries */
    cgi.TIKI_GetHandle = CGI_TIKI_GetHandle;
    cgi.NumAnims = CGI_NumAnims;
    cgi.NumSkins = CGI_NumSkins;
    cgi.NumSurfaces = CGI_NumSurfaces;
    cgi.NumTags = CGI_NumTags;
    cgi.InitCommands = CGI_InitCommands;
    cgi.CalculateBounds = CGI_CalculateBounds;
    cgi.FlushAll = TIKI_FlushAll;
    cgi.TIKI_NameForNum = CGI_TIKI_NameForNum;

    /* Animation queries */
    cgi.Anim_NameForNum = CGI_Anim_NameForNum;
    cgi.Anim_NumForName = CGI_Anim_NumForName;
    cgi.Anim_Random = CGI_Anim_Random;
    cgi.Anim_NumFrames = CGI_Anim_NumFrames;
    cgi.Anim_Time = CGI_Anim_Time;
    cgi.Anim_Delta = CGI_Anim_Delta;
    cgi.Anim_Flags = CGI_Anim_Flags;
    cgi.Anim_CrossblendTime = CGI_Anim_CrossblendTime;
    cgi.Anim_HasCommands = CGI_Anim_HasCommands;

    /* Frame queries */
    cgi.Frame_Commands = CGI_Frame_Commands;
    cgi.Frame_Delta = CGI_Frame_Delta;
    cgi.Frame_Time = CGI_Frame_Time;
    cgi.Frame_Bounds = CGI_Frame_Bounds;
    cgi.Frame_Radius = CGI_Frame_Radius;

    /* Surface queries */
    cgi.Surface_NameToNum = CGI_Surface_NameToNum;
    cgi.Surface_NumToName = CGI_Surface_NumToName;
    cgi.Surface_Flags = CGI_Surface_Flags;
    cgi.Surface_NumSkins = CGI_Surface_NumSkins;

    /* Tag (bone) queries */
    cgi.Tag_NumForName = CGI_Tag_NumForName;
    cgi.Tag_NameForNum = CGI_Tag_NameForNum;
    cgi.Tag_Orientation = CGI_Tag_Orientation;

    /* TODO: Fill in remaining clientGameImport_t functions:
     *   - Collision model (CM_*)
     *   - Sound (S_*)
     *   - Music (MUSIC_*)
     *   - Renderer (R_*)
     *   - Snapshot access
     *   - Alias system
     */

    Com_Printf("CL_InitCGameImport: import table initialized\n");
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
    cge->CG_DrawActiveFrame(serverTime, STEREO_CENTER, qfalse);
}

void CL_CGameDraw2D(void) {
    if (!cge) return;
    cge->CG_Draw2D();
}

clientGameExport_t *CL_GetCGameExport(void) {
    return cge;
}
