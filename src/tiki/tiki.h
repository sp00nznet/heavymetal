/*
 * tiki.h -- TIKI model system header
 *
 * The TIKI (Text-based Integrated Kinematic Interface) system is Ritual's
 * model definition framework. It evolved from SiN's DEF system and provides:
 *
 *   - Text-file model definitions (.tik files, binary cached as .cik)
 *   - Skeletal animation with bone hierarchy
 *   - Frame-synchronized events (sound, particles, etc.)
 *   - LOD (Level of Detail) support
 *   - $include inheritance and $define macro substitution
 *   - Up to 11,000 animation frames, 6,500 used in final game
 *
 * TIKI files reference:
 *   .tik  -- Text-based model definition (surfaces, animations, events)
 *   .cik  -- Binary compiled/cached TIKI data
 *   .skb  -- Skeletal model geometry + bone bind pose
 *   .ska  -- Skeletal animation sequences
 *   .tan  -- Non-skeletal (TAN) model geometry + animation
 *
 * FAKK2 shipped with 772 TIKI definitions for Julie, enemies,
 * weapons, items, and world objects.
 *
 * Structures based on SDK qfiles.h (dtiki_t, dtikianimdef_t, dtikicmd_t).
 */

#ifndef TIKI_H
#define TIKI_H

#include "../common/fakk_types.h"
#include "../common/g_public.h"     /* for tiki_cmd_t, orientation_t */

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Constants from SDK qfiles.h
 * ========================================================================= */

#define TIKI_CMD_MAX_ARGS       10      /* max args per frame command */
#define MAX_TIKI_MODELS         512     /* max simultaneously loaded models */
#define MAX_TIKI_DEFINES        64      /* max $define macros per file */
#define MAX_ANIMDEFNAME         48      /* max animation alias length */
#define MAX_ANIMFULLNAME        MAX_QPATH
#define MAX_TIKI_INITCMDS       256     /* max init commands per section */

/* Frame command special frame numbers (from SDK) */
#define TIKI_FRAME_EVERY        -1      /* execute every frame */
#define TIKI_FRAME_EXIT         -2      /* execute when animation exits */
#define TIKI_FRAME_ENTRY        -3      /* execute when animation enters */
#define TIKI_FRAME_LAST         -4      /* resolved at runtime to last frame */

/* Animation flags (from SDK) */
#define MDL_ANIM_DELTA_DRIVEN   (1 << 0)
#define MDL_ANIM_DEFAULT_ANGLES (1 << 3)

/* Surface flags (from SDK qfiles.h) */
#define MDL_SURFACE_SKINOFFSET_BIT0     (1 << 0)
#define MDL_SURFACE_SKINOFFSET_BIT1     (1 << 1)
#define MDL_SURFACE_NODRAW              (1 << 2)
#define MDL_SURFACE_SURFACETYPE_BIT0    (1 << 3)
#define MDL_SURFACE_SURFACETYPE_BIT1    (1 << 4)
#define MDL_SURFACE_SURFACETYPE_BIT2    (1 << 5)
#define MDL_SURFACE_CROSSFADE_SKINS     (1 << 6)
#define MDL_SURFACE_SKIN_NO_DAMAGE      (1 << 7)
#define MDL_SURFACE_NOMIPMAPS           (1 << 8)
#define MDL_SURFACE_NOPICMIP            (1 << 9)

/* =========================================================================
 * Handle type
 * ========================================================================= */

typedef int dtiki_t;    /* handle to a loaded TIKI model (index into cache) */

/* =========================================================================
 * Frame command -- executed at specific animation frames
 *
 * Matches SDK dtikicmd_t but uses direct pointers instead of buffer offsets.
 * ========================================================================= */

typedef struct {
    int     frame_num;                      /* frame number or TIKI_FRAME_* */
    int     num_args;
    char    *args[TIKI_CMD_MAX_ARGS];       /* args[0] = command name */
} tiki_frame_cmd_t;

/* =========================================================================
 * Init command -- server or client initialization command
 *
 * Executed once when the TIKI model is spawned.
 * E.g., "classname Player", "health 100", "setsize \"-15 -15 0\" \"15 15 30\""
 * ========================================================================= */

typedef struct {
    int     num_args;
    char    *args[TIKI_CMD_MAX_ARGS];       /* args[0] = command name */
} tiki_initcmd_t;

/* =========================================================================
 * Animation definition
 *
 * Matches SDK dtikianimdef_t. Each animation references an .ska or .tan file
 * and can have per-frame server and client commands.
 * ========================================================================= */

typedef struct {
    char    alias[MAX_ANIMDEFNAME];         /* animation name ("idle", "walk") */
    char    filename[MAX_QPATH];            /* full path to .ska/.tan file */
    float   weight;                         /* blend weight (default 1.0) */
    int     blendtime;                      /* crossblend time in ms */
    int     flags;                          /* MDL_ANIM_* flags */

    int     num_server_cmds;
    tiki_frame_cmd_t *server_cmds;

    int     num_client_cmds;
    tiki_frame_cmd_t *client_cmds;
} tiki_animdef_t;

/* =========================================================================
 * Surface binding -- maps a model surface to a shader
 * ========================================================================= */

typedef struct {
    char    name[MAX_QPATH];
    char    shader[MAX_QPATH];
    int     flags;                          /* MDL_SURFACE_* flags */
} tiki_surface_t;

/* =========================================================================
 * TIKI model -- complete parsed .tik definition
 *
 * Matches SDK dtiki_t layout conceptually, but uses dynamic allocations
 * instead of offset-based flat buffer.
 * ========================================================================= */

typedef struct {
    char    name[MAX_QPATH];                /* e.g. "models/julie.tik" */
    char    path[MAX_QPATH];                /* base path from 'path' directive */
    char    skelmodel[MAX_QPATH];           /* skeleton model (.skb) */

    float   scale;
    float   lod_scale;
    float   lod_bias;
    float   radius;
    vec3_t  light_offset;
    vec3_t  load_origin;

    int     num_surfaces;
    tiki_surface_t surfaces[TIKI_MAX_SURFACES];

    int     num_anims;
    tiki_animdef_t *anims;

    int     num_server_initcmds;
    tiki_initcmd_t *server_initcmds;
    int     num_client_initcmds;
    tiki_initcmd_t *client_initcmds;

    qboolean is_character;                  /* has skeletal model */
} tiki_model_t;

/* =========================================================================
 * System lifecycle
 * ========================================================================= */

void            TIKI_Init(void);
void            TIKI_Shutdown(void);
void            TIKI_FlushAll(void);

/* =========================================================================
 * Model loading and cache
 * ========================================================================= */

dtiki_t         TIKI_RegisterModel(const char *name);
tiki_model_t    *TIKI_GetModel(dtiki_t handle);
void            TIKI_FreeModel(dtiki_t handle);
const char      *TIKI_NameForNum(dtiki_t handle);

/* =========================================================================
 * Animation queries (used by game DLL interface)
 * ========================================================================= */

int             TIKI_NumAnims(dtiki_t handle);
const char      *TIKI_AnimName(dtiki_t handle, int animnum);
int             TIKI_AnimNumForName(dtiki_t handle, const char *name);
int             TIKI_AnimRandom(dtiki_t handle, const char *name);
int             TIKI_AnimNumFrames(dtiki_t handle, int animnum);
float           TIKI_AnimTime(dtiki_t handle, int animnum);
int             TIKI_AnimFlags(dtiki_t handle, int animnum);
int             TIKI_AnimCrossblendTime(dtiki_t handle, int animnum);
qboolean        TIKI_AnimHasCommands(dtiki_t handle, int animnum);

/* =========================================================================
 * Surface queries
 * ========================================================================= */

int             TIKI_NumSurfaces(dtiki_t handle);
int             TIKI_SurfaceNameToNum(dtiki_t handle, const char *name);
const char      *TIKI_SurfaceNumToName(dtiki_t handle, int num);
int             TIKI_SurfaceFlags(dtiki_t handle, int num);

/* =========================================================================
 * Tag (bone) queries
 * ========================================================================= */

int             TIKI_NumTags(dtiki_t handle);
int             TIKI_TagNumForName(dtiki_t handle, const char *name);
const char      *TIKI_TagNameForNum(dtiki_t handle, int num);

/* =========================================================================
 * Animation delta and frame queries (tiki_anim.c)
 * ========================================================================= */

void            TIKI_AnimDelta(dtiki_t handle, int animnum, vec3_t delta);
void            TIKI_AnimAbsoluteDelta(dtiki_t handle, int animnum, vec3_t delta);
void            TIKI_FrameDelta(dtiki_t handle, int animnum, int framenum, vec3_t delta);
float           TIKI_FrameTime(dtiki_t handle, int animnum, int framenum);
void            TIKI_FrameBounds(dtiki_t handle, int animnum, int framenum,
                                 float scale, vec3_t mins, vec3_t maxs);
float           TIKI_FrameRadius(dtiki_t handle, int animnum, int framenum);
qboolean        TIKI_FrameCommands(dtiki_t handle, int animnum, int framenum,
                                    tiki_cmd_t *tiki_cmd);
orientation_t   TIKI_TagOrientation(dtiki_t handle, int anim, int frame,
                                     int tagnum, float scale,
                                     int *bone_tag, vec4_t *bone_quat);

/* =========================================================================
 * Bounds and init commands
 * ========================================================================= */

void            TIKI_CalculateBounds(dtiki_t handle, float scale, vec3_t mins, vec3_t maxs);
qboolean        TIKI_InitCommands(dtiki_t handle, tiki_cmd_t *tiki_cmd);

/* =========================================================================
 * File parsing (internal, called by TIKI_RegisterModel)
 * ========================================================================= */

tiki_model_t    *TIKI_ParseFile(const char *filename);
void            TIKI_FreeModelData(tiki_model_t *model);

#ifdef __cplusplus
}
#endif

#endif /* TIKI_H */
