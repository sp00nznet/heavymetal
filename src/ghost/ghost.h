/*
 * ghost.h -- Ghost particle system
 *
 * Ghost is Ritual's custom particle system, written from scratch for FAKK2.
 * Each emitter has ~50 customizable parameters. Effects are composited
 * from multiple emitters (e.g., the Uzi fires 5 particle systems
 * simultaneously: smoke, shell casings, muzzle flash, tracers, impact debris).
 *
 * Key features:
 *   - Per-particle physics (gravity, wind, collision)
 *   - Sprite and model particles
 *   - Color/alpha/size animation over lifetime
 *   - Emission shapes (point, sphere, cylinder, line)
 *   - Tag-based attachment to TIKI model bones
 *   - Effect chaining and triggering
 *
 * Effect files (.eff) define emitter parameters in plain text.
 * Multiple emitters can be grouped into a single effect.
 */

#ifndef GHOST_H
#define GHOST_H

#include "../common/fakk_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Constants
 * ========================================================================= */

#define GHOST_MAX_PARTICLES     4096    /* total particle pool */
#define GHOST_MAX_EMITTERS_ACTIVE 256   /* simultaneous emitters */
#define GHOST_MAX_EFFECT_EMITTERS 8     /* emitters per effect definition */
#define GHOST_MAX_EFFECTS       128     /* loaded effect definitions */

/* =========================================================================
 * Types
 * ========================================================================= */

/* Ghost emitter handle */
typedef int ghostEmitter_t;

/* Emission shape types */
typedef enum {
    GHOST_SHAPE_POINT,
    GHOST_SHAPE_SPHERE,
    GHOST_SHAPE_CYLINDER,
    GHOST_SHAPE_LINE,
    GHOST_SHAPE_CONE,
    GHOST_SHAPE_BOX
} ghost_shape_t;

/* Particle render mode */
typedef enum {
    GHOST_RENDER_SPRITE,        /* Camera-facing quad */
    GHOST_RENDER_ORIENTED,      /* Oriented quad */
    GHOST_RENDER_BEAM,          /* Stretched between points */
    GHOST_RENDER_MODEL,         /* 3D model per particle */
    GHOST_RENDER_DECAL          /* Surface decal */
} ghost_render_t;

/* =========================================================================
 * Particle -- individual particle instance
 * ========================================================================= */

typedef struct {
    qboolean    active;
    int         emitterIndex;       /* which emitter spawned this */

    vec3_t      origin;
    vec3_t      velocity;
    vec3_t      accel;              /* per-particle acceleration */

    float       spawnTime;
    float       lifetime;           /* seconds to live */

    /* Animation state */
    float       size;               /* current size */
    float       sizeStart;
    float       sizeEnd;
    float       rotation;           /* sprite rotation (degrees) */
    float       rotationSpeed;

    /* Color/alpha */
    float       color[4];           /* current RGBA */
    float       colorStart[4];
    float       colorEnd[4];

    /* Rendering */
    ghost_render_t renderMode;
    int         shaderHandle;
} ghostParticle_t;

/* =========================================================================
 * Emitter parameters -- defines how particles are spawned
 * ========================================================================= */

typedef struct {
    /* Spawn rate */
    float       spawnRate;          /* particles per second */
    int         burstCount;         /* initial burst (0 = no burst) */
    float       emitterLife;        /* emitter duration (-1 = infinite) */

    /* Emission shape */
    ghost_shape_t shape;
    float       shapeRadius;
    float       shapeHeight;        /* for cylinder/cone */
    vec3_t      shapeSize;          /* for box */

    /* Particle properties */
    float       particleLife;       /* base lifetime */
    float       particleLifeRand;   /* random lifetime variance */

    /* Velocity */
    float       speed;              /* initial speed */
    float       speedRand;          /* random speed variance */
    vec3_t      velocityDir;        /* emission direction (normalized) */
    float       spreadAngle;        /* cone spread in degrees */

    /* Physics */
    float       gravity;            /* gravity multiplier (1.0 = normal) */
    float       drag;               /* velocity damping (0 = none) */
    float       bounce;             /* collision bounce factor (0 = no bounce) */
    qboolean    collide;            /* test against world geometry */

    /* Size animation */
    float       sizeStart;
    float       sizeStartRand;
    float       sizeEnd;

    /* Rotation */
    float       rotationStart;
    float       rotationStartRand;
    float       rotationSpeed;

    /* Color animation */
    float       colorStart[4];      /* RGBA start */
    float       colorEnd[4];        /* RGBA end */

    /* Rendering */
    ghost_render_t renderMode;
    char        shaderName[MAX_QPATH];
    int         shaderHandle;       /* resolved at spawn time */

    /* Flags */
    qboolean    additive;           /* additive blending */
    qboolean    oriented;           /* orient to velocity */
} ghostEmitterDef_t;

/* =========================================================================
 * Effect definition -- a group of emitters
 * ========================================================================= */

typedef struct {
    char                name[MAX_QPATH];
    ghostEmitterDef_t   emitters[GHOST_MAX_EFFECT_EMITTERS];
    int                 numEmitters;
} ghostEffectDef_t;

/* =========================================================================
 * Active emitter instance
 * ========================================================================= */

typedef struct {
    qboolean            active;
    int                 effectIndex;    /* index into effect def table */
    int                 emitterDefIdx;  /* which sub-emitter in the effect */

    vec3_t              origin;
    vec3_t              angles;
    vec3_t              axis[3];        /* orientation matrix */

    float               startTime;
    float               lastSpawnTime;
    float               spawnAccum;     /* fractional particle accumulator */
    int                 burstSpawned;   /* has initial burst been done? */

    /* Attachment */
    int                 attachEntity;   /* -1 = none */
    int                 attachTag;      /* TIKI tag index */
} ghostActiveEmitter_t;

/* =========================================================================
 * Public API
 * ========================================================================= */

/* System lifecycle */
void            Ghost_Init(void);
void            Ghost_Shutdown(void);
void            Ghost_Frame(int msec);

/* Emitter management */
ghostEmitter_t  Ghost_CreateEmitter(const char *effect_name);
void            Ghost_DestroyEmitter(ghostEmitter_t emitter);
void            Ghost_SetEmitterOrigin(ghostEmitter_t emitter, const vec3_t origin);
void            Ghost_SetEmitterAngles(ghostEmitter_t emitter, const vec3_t angles);
void            Ghost_AttachToTag(ghostEmitter_t emitter, int entity_num, int tag_index);

/* Effect loading */
qboolean        Ghost_LoadEffect(const char *filename);
void            Ghost_ClearEffects(void);

/* Particle count query */
int             Ghost_NumActiveParticles(void);
int             Ghost_NumActiveEmitters(void);

#ifdef __cplusplus
}
#endif

#endif /* GHOST_H */
