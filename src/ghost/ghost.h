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
 */

#ifndef GHOST_H
#define GHOST_H

#include "../common/fakk_types.h"

#ifdef __cplusplus
extern "C" {
#endif

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

/* Ghost system interface */
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

#ifdef __cplusplus
}
#endif

#endif /* GHOST_H */
