/*
 * ghost_main.c -- Ghost particle system implementation
 */

#include "ghost.h"
#include "../common/qcommon.h"

static qboolean ghost_initialized = qfalse;

void Ghost_Init(void) {
    Com_Printf("--- Ghost_Init ---\n");
    Com_Printf("Ghost particle system: up to %d emitters, ~%d params each\n",
               GHOST_MAX_EMITTERS, GHOST_MAX_PARAMS);
    ghost_initialized = qtrue;
}

void Ghost_Shutdown(void) {
    if (!ghost_initialized) return;
    Com_Printf("Ghost particle system shutdown\n");
    ghost_initialized = qfalse;
}

void Ghost_Frame(int msec) {
    (void)msec;
    /* TODO: Update all active particle emitters */
}

ghostEmitter_t Ghost_CreateEmitter(const char *effect_name) {
    (void)effect_name;
    /* TODO */
    return -1;
}

void Ghost_DestroyEmitter(ghostEmitter_t emitter) {
    (void)emitter;
}

void Ghost_SetEmitterOrigin(ghostEmitter_t emitter, const vec3_t origin) {
    (void)emitter; (void)origin;
}

void Ghost_SetEmitterAngles(ghostEmitter_t emitter, const vec3_t angles) {
    (void)emitter; (void)angles;
}

void Ghost_AttachToTag(ghostEmitter_t emitter, int entity_num, int tag_index) {
    (void)emitter; (void)entity_num; (void)tag_index;
}

qboolean Ghost_LoadEffect(const char *filename) {
    (void)filename;
    /* TODO: Parse effect definition file */
    return qfalse;
}
