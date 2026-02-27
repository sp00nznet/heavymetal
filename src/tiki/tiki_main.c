/*
 * tiki_main.c -- TIKI model system initialization
 */

#include "tiki.h"
#include "../common/qcommon.h"

static qboolean tiki_initialized = qfalse;

void TIKI_Init(void) {
    Com_Printf("--- TIKI_Init ---\n");
    Com_Printf("TIKI model system: text-based kinematic interface\n");
    Com_Printf("  Max surfaces per model: %d\n", TIKI_MAX_SURFACES);
    Com_Printf("  Max bones per skeleton: %d\n", TIKI_MAX_BONES);
    Com_Printf("  Max animation sequences: %d\n", TIKI_MAX_ANIMS);
    tiki_initialized = qtrue;
}

void TIKI_Shutdown(void) {
    if (!tiki_initialized) return;
    Com_Printf("TIKI model system shutdown\n");
    /* TODO: Free all loaded TIKI models */
    tiki_initialized = qfalse;
}

dtiki_t TIKI_RegisterModel(const char *name) {
    Com_DPrintf("TIKI_RegisterModel: %s (stub)\n", name);
    /* TODO: Load and cache TIKI model */
    return 0;
}

tiki_model_t *TIKI_GetModel(dtiki_t handle) {
    (void)handle;
    /* TODO */
    return NULL;
}

void TIKI_FreeModel(dtiki_t handle) {
    (void)handle;
    /* TODO */
}

int TIKI_GetAnimIndex(dtiki_t handle, const char *anim_name) {
    (void)handle; (void)anim_name;
    return -1;
}

int TIKI_GetNumAnims(dtiki_t handle) {
    (void)handle;
    return 0;
}

const char *TIKI_GetAnimName(dtiki_t handle, int anim_index) {
    (void)handle; (void)anim_index;
    return "";
}

float TIKI_GetAnimLength(dtiki_t handle, int anim_index) {
    (void)handle; (void)anim_index;
    return 0.0f;
}

int TIKI_GetBoneIndex(dtiki_t handle, const char *bone_name) {
    (void)handle; (void)bone_name;
    return -1;
}

int TIKI_GetNumBones(dtiki_t handle) {
    (void)handle;
    return 0;
}

const char *TIKI_GetBoneName(dtiki_t handle, int bone_index) {
    (void)handle; (void)bone_index;
    return "";
}

int TIKI_GetSurfaceIndex(dtiki_t handle, const char *surface_name) {
    (void)handle; (void)surface_name;
    return -1;
}

int TIKI_GetNumSurfaces(dtiki_t handle) {
    (void)handle;
    return 0;
}

const char *TIKI_GetSurfaceName(dtiki_t handle, int surface_index) {
    (void)handle; (void)surface_index;
    return "";
}
