/*
 * r_model.c -- Model loading and management
 *
 * FAKK2 supports three model types:
 *   - BSP world model (FAKK BSP v12) -- loaded by r_bsp.c
 *   - TIKI skeletal models (.tik -> .skb + .ska) -- via tiki_skel.c
 *   - MD3 simple models (inherited from Q3, rarely used)
 *
 * The model cache maps string names to integer handles.
 * Handle 0 is always "no model" / world.
 *
 * TIKI models are the primary model format:
 *   .tik text file -> references .skb (skeleton mesh) + .ska (animations)
 *   The TIKI system handles parsing; this module caches the renderer-side
 *   representation (vertex buffers, material bindings).
 */

#include "../common/qcommon.h"
#include "../common/qfiles.h"
#include "tr_types.h"
#include <string.h>

/* =========================================================================
 * Model types and structures
 * ========================================================================= */

typedef enum {
    MOD_BAD,
    MOD_BRUSH,      /* BSP inline model */
    MOD_TIKI,       /* TIKI skeletal model */
    MOD_MD3,        /* Q3 vertex animation */
    MOD_SPRITE      /* 2D sprite */
} modtype_t;

typedef struct {
    char        name[MAX_QPATH];
    modtype_t   type;
    int         index;          /* unique model handle */
    qboolean    loaded;

    /* Bounding volume */
    vec3_t      mins;
    vec3_t      maxs;
    float       radius;

    /* Type-specific data */
    union {
        struct {
            int         firstSurface;
            int         numSurfaces;
        } brush;
        struct {
            int         tikiHandle;     /* handle from TIKI system */
            int         numSurfaces;
            int         numBones;
        } tiki;
        struct {
            int         numFrames;
            int         numSurfaces;
        } md3;
    } data;
} model_t;

/* =========================================================================
 * Model cache
 * ========================================================================= */

#define MAX_MODELS  1024

static model_t  r_models[MAX_MODELS];
static int      r_numModels;

/* =========================================================================
 * Model registration
 * ========================================================================= */

static model_t *R_AllocModel(void) {
    if (r_numModels >= MAX_MODELS) {
        Com_Printf("R_AllocModel: MAX_MODELS hit\n");
        return NULL;
    }

    model_t *mod = &r_models[r_numModels];
    memset(mod, 0, sizeof(*mod));
    mod->index = r_numModels;
    r_numModels++;
    return mod;
}

static model_t *R_FindModel(const char *name) {
    for (int i = 0; i < r_numModels; i++) {
        if (!Q_stricmp(r_models[i].name, name)) {
            return &r_models[i];
        }
    }
    return NULL;
}

/* =========================================================================
 * TIKI model loading
 *
 * TIKI models are loaded by the TIKI subsystem (tiki_main.c / tiki_skel.c).
 * The renderer just needs to know about surfaces and bones for drawing.
 * ========================================================================= */

static qboolean R_LoadTIKIModel(model_t *mod, const char *name) {
    /* The TIKI system owns the model data. We just cache the handle
     * and query it for surface/bone counts at draw time. */
    extern int TIKI_RegisterModel(const char *name);
    extern int TIKI_NumSurfaces(int modelindex);
    extern int TIKI_NumTags(int modelindex);
    extern void TIKI_CalculateBounds(int modelindex, float scale, vec3_t mins, vec3_t maxs);

    int handle = TIKI_RegisterModel(name);
    if (handle <= 0) {
        Com_DPrintf("R_LoadTIKIModel: TIKI_RegisterModel failed for '%s'\n", name);
        return qfalse;
    }

    mod->type = MOD_TIKI;
    mod->data.tiki.tikiHandle = handle;
    mod->data.tiki.numSurfaces = TIKI_NumSurfaces(handle);
    mod->data.tiki.numBones = TIKI_NumTags(handle);

    /* Get bounds */
    TIKI_CalculateBounds(handle, 1.0f, mod->mins, mod->maxs);
    vec3_t size;
    VectorSubtract(mod->maxs, mod->mins, size);
    mod->radius = VectorLength(size) * 0.5f;

    mod->loaded = qtrue;
    Com_DPrintf("R_LoadTIKIModel: '%s' (%d surfaces, %d bones)\n",
                name, mod->data.tiki.numSurfaces, mod->data.tiki.numBones);
    return qtrue;
}

/* =========================================================================
 * BSP inline model registration
 *
 * Inline models are brush entities within the BSP (doors, elevators, etc.).
 * They're referenced as "*N" where N is the submodel index.
 * ========================================================================= */

static qboolean R_LoadBrushModel(model_t *mod, const char *name) {
    if (name[0] != '*') return qfalse;

    int submodelIdx = atoi(name + 1);
    int numModels = R_GetNumInlineModels();

    if (submodelIdx < 0 || submodelIdx >= numModels) {
        Com_Printf("R_LoadBrushModel: bad inline model '%s'\n", name);
        return qfalse;
    }

    mod->type = MOD_BRUSH;
    /* TODO: Copy bounding info from BSP submodel data */
    VectorSet(mod->mins, -32, -32, -32);
    VectorSet(mod->maxs, 32, 32, 32);
    mod->radius = 46.0f;
    mod->loaded = qtrue;

    return qtrue;
}

/* =========================================================================
 * Public model API
 * ========================================================================= */

qhandle_t R_RegisterModel(const char *name) {
    if (!name || !name[0]) return 0;

    /* Check if already loaded */
    model_t *mod = R_FindModel(name);
    if (mod) return mod->index;

    /* Allocate new model slot */
    mod = R_AllocModel();
    if (!mod) return 0;

    Q_strncpyz(mod->name, name, sizeof(mod->name));

    /* Determine type and load */
    if (name[0] == '*') {
        /* BSP inline model */
        if (!R_LoadBrushModel(mod, name)) {
            mod->type = MOD_BAD;
        }
    }
    else if (strstr(name, ".tik") != NULL) {
        /* TIKI model */
        if (!R_LoadTIKIModel(mod, name)) {
            mod->type = MOD_BAD;
        }
    }
    else if (strstr(name, ".md3") != NULL) {
        /* MD3 model -- Q3 format, rarely used in FAKK2 */
        Com_DPrintf("R_RegisterModel: MD3 loading not implemented: %s\n", name);
        mod->type = MOD_BAD;
    }
    else {
        /* Try as TIKI first (many models referenced without extension) */
        char tikipath[MAX_QPATH];
        snprintf(tikipath, sizeof(tikipath), "%s.tik", name);
        if (!R_LoadTIKIModel(mod, tikipath)) {
            mod->type = MOD_BAD;
            Com_DPrintf("R_RegisterModel: unknown model type: %s\n", name);
        }
    }

    return mod->index;
}

model_t *R_GetModelByHandle(qhandle_t h) {
    if (h < 0 || h >= r_numModels) return &r_models[0];
    return &r_models[h];
}

void R_ModelBounds(clipHandle_t model, vec3_t mins, vec3_t maxs) {
    if (model < 0 || model >= r_numModels) {
        VectorSet(mins, -16, -16, 0);
        VectorSet(maxs, 16, 16, 72);
        return;
    }
    VectorCopy(r_models[model].mins, mins);
    VectorCopy(r_models[model].maxs, maxs);
}

float R_ModelRadius(clipHandle_t model) {
    if (model < 0 || model >= r_numModels) return 36.0f;
    return r_models[model].radius;
}

/* =========================================================================
 * Skin registration (TIKI surface material overrides)
 * ========================================================================= */

qhandle_t R_RegisterSkin(const char *name) {
    Com_DPrintf("R_RegisterSkin: %s\n", name);
    /* TODO: Parse .skin file (maps surface names to shader names) */
    return 0;
}

/* =========================================================================
 * Initialization
 * ========================================================================= */

void R_InitModels(void) {
    Com_Printf("--- R_InitModels ---\n");

    memset(r_models, 0, sizeof(r_models));
    r_numModels = 0;

    /* Model 0 is "no model" / world */
    model_t *world = R_AllocModel();
    Q_strncpyz(world->name, "*world*", sizeof(world->name));
    world->type = MOD_BRUSH;
    world->loaded = qtrue;
}

void R_ShutdownModels(void) {
    /* TODO: Free any GPU-side resources */
    memset(r_models, 0, sizeof(r_models));
    r_numModels = 0;
}
