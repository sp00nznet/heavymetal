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
 *   - Up to 11,000 animation frames, 6,500 used in final game
 *
 * TIKI files reference:
 *   .tik  -- Text-based model definition (surfaces, animations, events)
 *   .cik  -- Binary compiled/cached TIKI data
 *   .skl  -- Skeleton data
 *   .tan  -- TAN model geometry
 *   .anm  -- Animation sequences
 *
 * FAKK2 shipped with extensive TIKI definitions for Julie, enemies,
 * weapons, and world objects. The SDK documents this as enabling
 * "data-driven design" where content creators could define models
 * and behaviors without recompiling.
 */

#ifndef TIKI_H
#define TIKI_H

#include "../common/fakk_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * TIKI handle types
 * ========================================================================= */

typedef int dtiki_t;    /* handle to a loaded TIKI model */

/* =========================================================================
 * TIKI animation command types
 * Frame events that fire at specific animation frames
 * ========================================================================= */

typedef enum {
    TIKI_CMD_SOUND,         /* Play a sound effect */
    TIKI_CMD_FOOTSTEP,      /* Footstep sound (surface-dependent) */
    TIKI_CMD_EFFECT,        /* Spawn a Ghost particle effect */
    TIKI_CMD_TAGSPAWN,      /* Spawn entity at tag (bone) */
    TIKI_CMD_TAGDLIGHT,     /* Dynamic light at tag */
    TIKI_CMD_TAGEMITTER,    /* Particle emitter at tag */
    TIKI_CMD_ALIASCACHE,    /* Pre-cache an alias */
    TIKI_CMD_BODYFALL,      /* Body hitting ground */
    TIKI_CMD_COUNT
} tiki_cmd_type_t;

/* =========================================================================
 * TIKI animation frame command
 * ========================================================================= */

typedef struct {
    int                 frame_num;      /* animation frame this fires on */
    tiki_cmd_type_t     type;
    char                args[256];      /* command arguments (sound name, effect name, etc.) */
} tiki_anim_cmd_t;

/* =========================================================================
 * TIKI animation definition
 * ========================================================================= */

typedef struct {
    char                name[MAX_QPATH];    /* animation name ("idle", "walk", "attack_l") */
    char                alias[MAX_QPATH];   /* alias name for scripting */
    float               weight;             /* blend weight */
    float               blendtime;          /* blend transition time */
    int                 flags;
    int                 num_frames;
    float               framerate;
    int                 num_commands;
    tiki_anim_cmd_t     commands[TIKI_MAX_COMMANDS];
} tiki_anim_t;

/* =========================================================================
 * TIKI surface definition
 * ========================================================================= */

typedef struct {
    char                name[MAX_QPATH];
    char                shader[MAX_QPATH];
    int                 flags;
} tiki_surface_t;

/* =========================================================================
 * TIKI model definition (loaded from .tik file)
 * ========================================================================= */

typedef struct {
    char                name[MAX_QPATH];        /* e.g. "models/julie.tik" */
    char                skelmodel[MAX_QPATH];   /* skeleton model path */

    int                 num_surfaces;
    tiki_surface_t      surfaces[TIKI_MAX_SURFACES];

    int                 num_anims;
    tiki_anim_t         *anims;

    float               scale;
    float               lod_scale;
    float               lod_bias;

    vec3_t              light_offset;
    vec3_t              load_origin;

    qboolean            is_character;   /* has skeleton for skeletal animation */
} tiki_model_t;

/* =========================================================================
 * TIKI system interface
 * ========================================================================= */

/* System lifecycle */
void            TIKI_Init(void);
void            TIKI_Shutdown(void);

/* Model loading */
dtiki_t         TIKI_RegisterModel(const char *name);
tiki_model_t    *TIKI_GetModel(dtiki_t handle);
void            TIKI_FreeModel(dtiki_t handle);

/* Animation queries */
int             TIKI_GetAnimIndex(dtiki_t handle, const char *anim_name);
int             TIKI_GetNumAnims(dtiki_t handle);
const char      *TIKI_GetAnimName(dtiki_t handle, int anim_index);
float           TIKI_GetAnimLength(dtiki_t handle, int anim_index);

/* Bone/tag queries */
int             TIKI_GetBoneIndex(dtiki_t handle, const char *bone_name);
int             TIKI_GetNumBones(dtiki_t handle);
const char      *TIKI_GetBoneName(dtiki_t handle, int bone_index);

/* Surface queries */
int             TIKI_GetSurfaceIndex(dtiki_t handle, const char *surface_name);
int             TIKI_GetNumSurfaces(dtiki_t handle);
const char      *TIKI_GetSurfaceName(dtiki_t handle, int surface_index);

/* File parsing */
tiki_model_t    *TIKI_ParseFile(const char *filename);

#ifdef __cplusplus
}
#endif

#endif /* TIKI_H */
