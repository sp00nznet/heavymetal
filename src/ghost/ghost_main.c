/*
 * ghost_main.c -- Ghost particle system implementation
 *
 * Manages a pool of particles and emitters. Each frame:
 *   1. Update emitter positions (tag attachments)
 *   2. Spawn new particles from active emitters
 *   3. Simulate physics on all particles
 *   4. Animate color/size/rotation
 *   5. Remove dead particles
 *
 * Effect definition files (.eff) are loaded from the PK3 filesystem.
 * The game DLL and client-side code create emitters via Ghost_CreateEmitter.
 */

#include "ghost.h"
#include "../common/qcommon.h"
#include "../renderer/tr_types.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>

/* Forward declaration */
static void Ghost_RenderParticles(void);

/* =========================================================================
 * State
 * ========================================================================= */

static qboolean             ghost_initialized = qfalse;
static float                ghost_time = 0.0f;

/* Particle pool */
static ghostParticle_t      ghost_particles[GHOST_MAX_PARTICLES];
static int                  ghost_numActiveParticles;

/* Active emitters */
static ghostActiveEmitter_t ghost_emitters[GHOST_MAX_EMITTERS_ACTIVE];

/* Effect definitions */
static ghostEffectDef_t     ghost_effects[GHOST_MAX_EFFECTS];
static int                  ghost_numEffects;

/* =========================================================================
 * Helpers
 * ========================================================================= */

static float Ghost_Random(void) {
    return (float)(rand() & 0x7FFF) / 32767.0f;
}

static float Ghost_RandomRange(float base, float variance) {
    return base + variance * (Ghost_Random() * 2.0f - 1.0f);
}

static void Ghost_RandomDirection(vec3_t dir, const vec3_t baseDir, float spreadDeg) {
    if (spreadDeg <= 0.0f) {
        VectorCopy(baseDir, dir);
        return;
    }

    float spreadRad = spreadDeg * (3.14159f / 180.0f);
    float theta = Ghost_Random() * 2.0f * 3.14159f;
    float phi = Ghost_Random() * spreadRad;

    /* Generate random direction within cone around baseDir */
    float sinPhi = sinf(phi);
    dir[0] = baseDir[0] + sinPhi * cosf(theta);
    dir[1] = baseDir[1] + sinPhi * sinf(theta);
    dir[2] = baseDir[2] + cosf(phi) - 1.0f;
    VectorNormalize(dir);
}

/* =========================================================================
 * Particle allocation
 * ========================================================================= */

static ghostParticle_t *Ghost_AllocParticle(void) {
    for (int i = 0; i < GHOST_MAX_PARTICLES; i++) {
        if (!ghost_particles[i].active) {
            return &ghost_particles[i];
        }
    }
    return NULL;
}

/* =========================================================================
 * Spawn a particle from an emitter
 * ========================================================================= */

static void Ghost_SpawnParticle(ghostActiveEmitter_t *ae, const ghostEmitterDef_t *def) {
    ghostParticle_t *p = Ghost_AllocParticle();
    if (!p) return;

    memset(p, 0, sizeof(*p));
    p->active = qtrue;
    p->emitterIndex = (int)(ae - ghost_emitters);
    p->spawnTime = ghost_time;
    p->lifetime = Ghost_RandomRange(def->particleLife, def->particleLifeRand);
    if (p->lifetime <= 0.0f) p->lifetime = 0.1f;

    /* Position based on emission shape */
    VectorCopy(ae->origin, p->origin);
    switch (def->shape) {
    case GHOST_SHAPE_SPHERE: {
        vec3_t offset;
        offset[0] = Ghost_Random() * 2.0f - 1.0f;
        offset[1] = Ghost_Random() * 2.0f - 1.0f;
        offset[2] = Ghost_Random() * 2.0f - 1.0f;
        VectorNormalize(offset);
        VectorScale(offset, Ghost_Random() * def->shapeRadius, offset);
        VectorAdd(p->origin, offset, p->origin);
        break;
    }
    case GHOST_SHAPE_BOX:
        p->origin[0] += (Ghost_Random() - 0.5f) * def->shapeSize[0];
        p->origin[1] += (Ghost_Random() - 0.5f) * def->shapeSize[1];
        p->origin[2] += (Ghost_Random() - 0.5f) * def->shapeSize[2];
        break;
    case GHOST_SHAPE_CYLINDER:
    case GHOST_SHAPE_CONE: {
        float angle = Ghost_Random() * 2.0f * 3.14159f;
        float radius = def->shapeRadius;
        if (def->shape == GHOST_SHAPE_CONE) {
            float h = Ghost_Random();
            radius *= h;
            p->origin[2] += h * def->shapeHeight;
        } else {
            p->origin[2] += Ghost_Random() * def->shapeHeight;
        }
        p->origin[0] += cosf(angle) * radius;
        p->origin[1] += sinf(angle) * radius;
        break;
    }
    case GHOST_SHAPE_LINE:
        VectorMA(p->origin, Ghost_Random() * def->shapeRadius,
                 def->velocityDir, p->origin);
        break;
    default:
        break;
    }

    /* Velocity */
    vec3_t dir;
    Ghost_RandomDirection(dir, def->velocityDir, def->spreadAngle);
    float speed = Ghost_RandomRange(def->speed, def->speedRand);
    VectorScale(dir, speed, p->velocity);

    /* Acceleration (gravity) */
    p->accel[0] = 0.0f;
    p->accel[1] = 0.0f;
    p->accel[2] = -800.0f * def->gravity; /* 800 = standard game gravity */

    /* Size */
    p->sizeStart = Ghost_RandomRange(def->sizeStart, def->sizeStartRand);
    p->sizeEnd = def->sizeEnd;
    p->size = p->sizeStart;

    /* Rotation */
    p->rotation = Ghost_RandomRange(def->rotationStart, def->rotationStartRand);
    p->rotationSpeed = def->rotationSpeed;

    /* Color */
    for (int i = 0; i < 4; i++) {
        p->colorStart[i] = def->colorStart[i];
        p->colorEnd[i] = def->colorEnd[i];
        p->color[i] = p->colorStart[i];
    }

    p->renderMode = def->renderMode;
    p->shaderHandle = def->shaderHandle;

    ghost_numActiveParticles++;
}

/* =========================================================================
 * Emitter spawning logic
 * ========================================================================= */

static void Ghost_UpdateEmitter(ghostActiveEmitter_t *ae, float dt) {
    if (!ae->active) return;

    int effectIdx = ae->effectIndex;
    if (effectIdx < 0 || effectIdx >= ghost_numEffects) {
        ae->active = qfalse;
        return;
    }

    const ghostEmitterDef_t *def = &ghost_effects[effectIdx].emitters[ae->emitterDefIdx];

    /* Check emitter lifetime */
    if (def->emitterLife > 0.0f) {
        if (ghost_time - ae->startTime > def->emitterLife) {
            ae->active = qfalse;
            return;
        }
    }

    /* Initial burst */
    if (!ae->burstSpawned && def->burstCount > 0) {
        for (int i = 0; i < def->burstCount; i++) {
            Ghost_SpawnParticle(ae, def);
        }
        ae->burstSpawned = qtrue;
    }

    /* Continuous emission */
    if (def->spawnRate > 0.0f) {
        ae->spawnAccum += def->spawnRate * dt;
        while (ae->spawnAccum >= 1.0f) {
            Ghost_SpawnParticle(ae, def);
            ae->spawnAccum -= 1.0f;
        }
    }
}

/* =========================================================================
 * Particle simulation
 * ========================================================================= */

static void Ghost_UpdateParticle(ghostParticle_t *p, float dt) {
    if (!p->active) return;

    float age = ghost_time - p->spawnTime;
    if (age >= p->lifetime) {
        p->active = qfalse;
        ghost_numActiveParticles--;
        return;
    }

    float frac = age / p->lifetime; /* 0..1 */

    /* Physics integration (simple Euler) */
    p->velocity[0] += p->accel[0] * dt;
    p->velocity[1] += p->accel[1] * dt;
    p->velocity[2] += p->accel[2] * dt;

    p->origin[0] += p->velocity[0] * dt;
    p->origin[1] += p->velocity[1] * dt;
    p->origin[2] += p->velocity[2] * dt;

    /* Size interpolation */
    p->size = p->sizeStart + (p->sizeEnd - p->sizeStart) * frac;
    if (p->size < 0.0f) p->size = 0.0f;

    /* Rotation */
    p->rotation += p->rotationSpeed * dt;

    /* Color/alpha interpolation */
    for (int i = 0; i < 4; i++) {
        p->color[i] = p->colorStart[i] + (p->colorEnd[i] - p->colorStart[i]) * frac;
        if (p->color[i] < 0.0f) p->color[i] = 0.0f;
        if (p->color[i] > 1.0f) p->color[i] = 1.0f;
    }
}

/* =========================================================================
 * Per-frame update
 * ========================================================================= */

void Ghost_Frame(int msec) {
    if (!ghost_initialized) return;

    float dt = (float)msec / 1000.0f;
    ghost_time += dt;

    /* Update emitters (spawn particles) */
    for (int i = 0; i < GHOST_MAX_EMITTERS_ACTIVE; i++) {
        Ghost_UpdateEmitter(&ghost_emitters[i], dt);
    }

    /* Simulate all particles */
    for (int i = 0; i < GHOST_MAX_PARTICLES; i++) {
        Ghost_UpdateParticle(&ghost_particles[i], dt);
    }

    /* Submit live particles to renderer */
    Ghost_RenderParticles();
}

/* =========================================================================
 * Particle rendering -- submit sprites to the scene
 *
 * Each active particle becomes a refEntity_t submitted via
 * R_AddRefSpriteToScene. The renderer draws them as camera-facing
 * quads (sprites) or oriented quads depending on renderMode.
 * ========================================================================= */

extern void R_AddRefSpriteToScene(refEntity_t *ent);

static void Ghost_RenderParticles(void) {
    for (int i = 0; i < GHOST_MAX_PARTICLES; i++) {
        ghostParticle_t *p = &ghost_particles[i];
        if (!p->active) continue;

        refEntity_t ent;
        memset(&ent, 0, sizeof(ent));

        /* Position */
        VectorCopy(p->origin, ent.origin);

        /* Scale from particle size */
        ent.scale = p->size;
        if (ent.scale <= 0.0f) ent.scale = 1.0f;

        /* Color and alpha */
        ent.shaderRGBA[0] = (byte)(p->color[0] * 255.0f);
        ent.shaderRGBA[1] = (byte)(p->color[1] * 255.0f);
        ent.shaderRGBA[2] = (byte)(p->color[2] * 255.0f);
        ent.shaderRGBA[3] = (byte)(p->color[3] * 255.0f);

        /* Skip fully transparent particles */
        if (ent.shaderRGBA[3] == 0) continue;

        /* Custom shader from effect definition */
        ent.customShader = p->shaderHandle;

        /* Rotation stored in shaderTexCoord[0] for the sprite renderer */
        ent.shaderTexCoord[0] = p->rotation;

        /* Identity axis (sprites are billboarded by the renderer) */
        ent.axis[0][0] = 1.0f;
        ent.axis[1][1] = 1.0f;
        ent.axis[2][2] = 1.0f;

        /* Render flags based on particle mode */
        if (p->renderMode == GHOST_RENDER_ORIENTED) {
            /* Oriented particles use velocity as forward axis */
            float vlen = VectorLength(p->velocity);
            if (vlen > 0.001f) {
                VectorScale(p->velocity, 1.0f / vlen, ent.axis[0]);
            }
        }

        /* Additive blending flag */
        int emIdx = p->emitterIndex;
        if (emIdx >= 0 && emIdx < GHOST_MAX_EMITTERS_ACTIVE &&
            ghost_emitters[emIdx].active) {
            int effIdx = ghost_emitters[emIdx].effectIndex;
            int defIdx = ghost_emitters[emIdx].emitterDefIdx;
            if (effIdx >= 0 && effIdx < ghost_numEffects) {
                if (ghost_effects[effIdx].emitters[defIdx].additive) {
                    ent.renderfx |= RF_ADDITIVE_DLIGHT;
                }
            }
        }

        R_AddRefSpriteToScene(&ent);
    }
}

/* =========================================================================
 * Effect definition loading
 *
 * .eff files define emitter parameters:
 *   spawnrate 50
 *   life 2.0
 *   gravity 1.0
 *   speed 200 50
 *   size 4 0 12
 *   color 1 0.8 0.3 1  0.5 0.1 0.0 0
 *   shader textures/effects/smoke1
 *   shape sphere 32
 * ========================================================================= */

static void Ghost_ParseEmitterDef(ghostEmitterDef_t *def, const char **data) {
    const char *token;

    /* Set defaults */
    memset(def, 0, sizeof(*def));
    def->particleLife = 1.0f;
    def->emitterLife = -1.0f;
    def->sizeStart = 4.0f;
    def->sizeEnd = 4.0f;
    def->colorStart[0] = def->colorStart[1] = def->colorStart[2] = def->colorStart[3] = 1.0f;
    def->colorEnd[0] = def->colorEnd[1] = def->colorEnd[2] = 1.0f;
    def->colorEnd[3] = 0.0f; /* fade out by default */
    def->velocityDir[2] = 1.0f; /* emit upward by default */
    def->renderMode = GHOST_RENDER_SPRITE;
    def->spawnRate = 10.0f;

    while (1) {
        token = COM_Parse(data);
        if (!token[0] || token[0] == '}') break;

        if (!Q_stricmp(token, "spawnrate")) {
            token = COM_Parse(data);
            def->spawnRate = (float)atof(token);
        }
        else if (!Q_stricmp(token, "burst")) {
            token = COM_Parse(data);
            def->burstCount = atoi(token);
        }
        else if (!Q_stricmp(token, "emitterlife") || !Q_stricmp(token, "emitterLife")) {
            token = COM_Parse(data);
            def->emitterLife = (float)atof(token);
        }
        else if (!Q_stricmp(token, "life")) {
            token = COM_Parse(data);
            def->particleLife = (float)atof(token);
            token = COM_Parse(data);
            if (token[0]) def->particleLifeRand = (float)atof(token);
        }
        else if (!Q_stricmp(token, "speed")) {
            token = COM_Parse(data);
            def->speed = (float)atof(token);
            token = COM_Parse(data);
            if (token[0]) def->speedRand = (float)atof(token);
        }
        else if (!Q_stricmp(token, "spread")) {
            token = COM_Parse(data);
            def->spreadAngle = (float)atof(token);
        }
        else if (!Q_stricmp(token, "gravity")) {
            token = COM_Parse(data);
            def->gravity = (float)atof(token);
        }
        else if (!Q_stricmp(token, "drag")) {
            token = COM_Parse(data);
            def->drag = (float)atof(token);
        }
        else if (!Q_stricmp(token, "bounce")) {
            token = COM_Parse(data);
            def->bounce = (float)atof(token);
            def->collide = (def->bounce > 0.0f);
        }
        else if (!Q_stricmp(token, "size")) {
            token = COM_Parse(data);
            def->sizeStart = (float)atof(token);
            token = COM_Parse(data);
            if (token[0]) def->sizeStartRand = (float)atof(token);
            token = COM_Parse(data);
            if (token[0]) def->sizeEnd = (float)atof(token);
        }
        else if (!Q_stricmp(token, "rotation")) {
            token = COM_Parse(data);
            def->rotationStart = (float)atof(token);
            token = COM_Parse(data);
            if (token[0]) def->rotationStartRand = (float)atof(token);
            token = COM_Parse(data);
            if (token[0]) def->rotationSpeed = (float)atof(token);
        }
        else if (!Q_stricmp(token, "color")) {
            /* color r1 g1 b1 a1  r2 g2 b2 a2 */
            for (int i = 0; i < 4; i++) {
                token = COM_Parse(data);
                def->colorStart[i] = (float)atof(token);
            }
            for (int i = 0; i < 4; i++) {
                token = COM_Parse(data);
                if (token[0]) def->colorEnd[i] = (float)atof(token);
            }
        }
        else if (!Q_stricmp(token, "shader")) {
            token = COM_Parse(data);
            Q_strncpyz(def->shaderName, token, sizeof(def->shaderName));
        }
        else if (!Q_stricmp(token, "direction")) {
            for (int i = 0; i < 3; i++) {
                token = COM_Parse(data);
                def->velocityDir[i] = (float)atof(token);
            }
            VectorNormalize(def->velocityDir);
        }
        else if (!Q_stricmp(token, "shape")) {
            token = COM_Parse(data);
            if (!Q_stricmp(token, "sphere")) {
                def->shape = GHOST_SHAPE_SPHERE;
                token = COM_Parse(data);
                def->shapeRadius = (float)atof(token);
            } else if (!Q_stricmp(token, "box")) {
                def->shape = GHOST_SHAPE_BOX;
                for (int i = 0; i < 3; i++) {
                    token = COM_Parse(data);
                    def->shapeSize[i] = (float)atof(token);
                }
            } else if (!Q_stricmp(token, "cylinder")) {
                def->shape = GHOST_SHAPE_CYLINDER;
                token = COM_Parse(data);
                def->shapeRadius = (float)atof(token);
                token = COM_Parse(data);
                def->shapeHeight = (float)atof(token);
            } else if (!Q_stricmp(token, "cone")) {
                def->shape = GHOST_SHAPE_CONE;
                token = COM_Parse(data);
                def->shapeRadius = (float)atof(token);
                token = COM_Parse(data);
                def->shapeHeight = (float)atof(token);
            } else if (!Q_stricmp(token, "line")) {
                def->shape = GHOST_SHAPE_LINE;
                token = COM_Parse(data);
                def->shapeRadius = (float)atof(token);
            }
        }
        else if (!Q_stricmp(token, "rendermode")) {
            token = COM_Parse(data);
            if (!Q_stricmp(token, "sprite"))    def->renderMode = GHOST_RENDER_SPRITE;
            else if (!Q_stricmp(token, "oriented")) def->renderMode = GHOST_RENDER_ORIENTED;
            else if (!Q_stricmp(token, "beam"))  def->renderMode = GHOST_RENDER_BEAM;
            else if (!Q_stricmp(token, "model")) def->renderMode = GHOST_RENDER_MODEL;
            else if (!Q_stricmp(token, "decal")) def->renderMode = GHOST_RENDER_DECAL;
        }
        else if (!Q_stricmp(token, "additive")) {
            def->additive = qtrue;
        }
        else if (!Q_stricmp(token, "oriented")) {
            def->oriented = qtrue;
        }
        else if (!Q_stricmp(token, "collide")) {
            def->collide = qtrue;
        }
    }
}

qboolean Ghost_LoadEffect(const char *filename) {
    if (!ghost_initialized) return qfalse;
    if (ghost_numEffects >= GHOST_MAX_EFFECTS) {
        Com_Printf("Ghost_LoadEffect: effect table full\n");
        return qfalse;
    }

    /* Check if already loaded */
    for (int i = 0; i < ghost_numEffects; i++) {
        if (!Q_stricmp(ghost_effects[i].name, filename)) {
            return qtrue;
        }
    }

    void *buffer;
    long len = FS_ReadFile(filename, &buffer);
    if (len <= 0 || !buffer) {
        Com_DPrintf("Ghost_LoadEffect: can't load '%s'\n", filename);
        return qfalse;
    }

    ghostEffectDef_t *eff = &ghost_effects[ghost_numEffects];
    memset(eff, 0, sizeof(*eff));
    Q_strncpyz(eff->name, filename, sizeof(eff->name));

    const char *data = (const char *)buffer;
    const char *token;

    while (1) {
        token = COM_Parse(&data);
        if (!token[0]) break;

        if (!Q_stricmp(token, "emitter") || token[0] == '{') {
            if (eff->numEmitters < GHOST_MAX_EFFECT_EMITTERS) {
                /* Skip opening brace if present */
                if (token[0] != '{') {
                    token = COM_Parse(&data);
                }
                Ghost_ParseEmitterDef(&eff->emitters[eff->numEmitters], &data);
                eff->numEmitters++;
            }
        }
    }

    FS_FreeFile(buffer);

    if (eff->numEmitters > 0) {
        ghost_numEffects++;
        Com_DPrintf("Ghost_LoadEffect: '%s' (%d emitter(s))\n",
                   filename, eff->numEmitters);
        return qtrue;
    }

    return qfalse;
}

void Ghost_ClearEffects(void) {
    ghost_numEffects = 0;
}

/* =========================================================================
 * Emitter management
 * ========================================================================= */

static int Ghost_FindEffect(const char *name) {
    for (int i = 0; i < ghost_numEffects; i++) {
        if (!Q_stricmp(ghost_effects[i].name, name)) {
            return i;
        }
    }

    /* Try loading on demand */
    if (Ghost_LoadEffect(name)) {
        return ghost_numEffects - 1;
    }

    /* Try with effect/ prefix */
    char path[MAX_QPATH];
    snprintf(path, sizeof(path), "effect/%s", name);
    if (Ghost_LoadEffect(path)) {
        return ghost_numEffects - 1;
    }

    snprintf(path, sizeof(path), "effect/%s.eff", name);
    if (Ghost_LoadEffect(path)) {
        return ghost_numEffects - 1;
    }

    return -1;
}

ghostEmitter_t Ghost_CreateEmitter(const char *effect_name) {
    if (!ghost_initialized || !effect_name) return -1;

    int effectIdx = Ghost_FindEffect(effect_name);
    if (effectIdx < 0) {
        Com_DPrintf("Ghost_CreateEmitter: effect '%s' not found\n", effect_name);
        return -1;
    }

    ghostEffectDef_t *eff = &ghost_effects[effectIdx];

    /* Create one active emitter per sub-emitter in the effect */
    int firstHandle = -1;
    for (int e = 0; e < eff->numEmitters; e++) {
        /* Find free slot */
        ghostActiveEmitter_t *ae = NULL;
        int slot = -1;
        for (int i = 0; i < GHOST_MAX_EMITTERS_ACTIVE; i++) {
            if (!ghost_emitters[i].active) {
                ae = &ghost_emitters[i];
                slot = i;
                break;
            }
        }
        if (!ae) break;

        memset(ae, 0, sizeof(*ae));
        ae->active = qtrue;
        ae->effectIndex = effectIdx;
        ae->emitterDefIdx = e;
        ae->startTime = ghost_time;
        ae->attachEntity = -1;
        ae->attachTag = -1;

        if (firstHandle < 0) firstHandle = slot;
    }

    return firstHandle;
}

void Ghost_DestroyEmitter(ghostEmitter_t emitter) {
    if (emitter < 0 || emitter >= GHOST_MAX_EMITTERS_ACTIVE) return;
    ghost_emitters[emitter].active = qfalse;
}

void Ghost_SetEmitterOrigin(ghostEmitter_t emitter, const vec3_t origin) {
    if (emitter < 0 || emitter >= GHOST_MAX_EMITTERS_ACTIVE) return;
    if (!ghost_emitters[emitter].active) return;
    VectorCopy(origin, ghost_emitters[emitter].origin);
}

void Ghost_SetEmitterAngles(ghostEmitter_t emitter, const vec3_t angles) {
    if (emitter < 0 || emitter >= GHOST_MAX_EMITTERS_ACTIVE) return;
    if (!ghost_emitters[emitter].active) return;
    VectorCopy(angles, ghost_emitters[emitter].angles);

    /* Build orientation axis matrix from Euler angles */
    vec3_t forward, right, up;
    AngleVectors(angles, forward, right, up);
    VectorCopy(forward, ghost_emitters[emitter].axis[0]);
    VectorCopy(right, ghost_emitters[emitter].axis[1]);
    VectorCopy(up, ghost_emitters[emitter].axis[2]);
}

void Ghost_AttachToTag(ghostEmitter_t emitter, int entity_num, int tag_index) {
    if (emitter < 0 || emitter >= GHOST_MAX_EMITTERS_ACTIVE) return;
    if (!ghost_emitters[emitter].active) return;
    ghost_emitters[emitter].attachEntity = entity_num;
    ghost_emitters[emitter].attachTag = tag_index;
}

/* =========================================================================
 * Queries
 * ========================================================================= */

int Ghost_NumActiveParticles(void) {
    return ghost_numActiveParticles;
}

int Ghost_NumActiveEmitters(void) {
    int count = 0;
    for (int i = 0; i < GHOST_MAX_EMITTERS_ACTIVE; i++) {
        if (ghost_emitters[i].active) count++;
    }
    return count;
}

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

void Ghost_Init(void) {
    Com_Printf("--- Ghost_Init ---\n");

    memset(ghost_particles, 0, sizeof(ghost_particles));
    memset(ghost_emitters, 0, sizeof(ghost_emitters));
    memset(ghost_effects, 0, sizeof(ghost_effects));

    ghost_numActiveParticles = 0;
    ghost_numEffects = 0;
    ghost_time = 0.0f;
    ghost_initialized = qtrue;

    Com_Printf("Ghost particle system: %d particle pool, %d emitter slots\n",
               GHOST_MAX_PARTICLES, GHOST_MAX_EMITTERS_ACTIVE);
}

void Ghost_Shutdown(void) {
    if (!ghost_initialized) return;

    memset(ghost_particles, 0, sizeof(ghost_particles));
    memset(ghost_emitters, 0, sizeof(ghost_emitters));
    memset(ghost_effects, 0, sizeof(ghost_effects));

    ghost_numActiveParticles = 0;
    ghost_numEffects = 0;
    ghost_initialized = qfalse;
    Com_Printf("Ghost particle system shutdown\n");
}
