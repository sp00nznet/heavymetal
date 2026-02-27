/*
 * tiki_main.c -- TIKI model system: registration cache and queries
 *
 * Manages the model cache (up to 512 simultaneous models) and provides
 * query functions used by the game DLL interface (game_import_t and
 * clientGameImport_t function pointer tables).
 *
 * Flow:
 *   1. TIKI_RegisterModel("models/julie.tik") -> parse + cache
 *   2. Game DLL queries via handle: TIKI_NumAnims(h), TIKI_AnimName(h, i), etc.
 *   3. TIKI_Shutdown() frees all cached models
 */

#include "tiki.h"
#include "../common/qcommon.h"
#include <string.h>

/* =========================================================================
 * Model cache
 * ========================================================================= */

static tiki_model_t     *tiki_cache[MAX_TIKI_MODELS];
static int              tiki_count;
static qboolean         tiki_initialized;

/* =========================================================================
 * System lifecycle
 * ========================================================================= */

void TIKI_Init(void) {
    Com_Printf("--- TIKI_Init ---\n");
    memset(tiki_cache, 0, sizeof(tiki_cache));
    tiki_count = 0;
    tiki_initialized = qtrue;
    Com_Printf("TIKI model system initialized (max %d models)\n", MAX_TIKI_MODELS);
}

void TIKI_Shutdown(void) {
    if (!tiki_initialized) return;

    Com_Printf("TIKI_Shutdown: freeing %d cached models\n", tiki_count);

    for (int i = 0; i < MAX_TIKI_MODELS; i++) {
        if (tiki_cache[i]) {
            TIKI_FreeModelData(tiki_cache[i]);
            Z_Free(tiki_cache[i]);
            tiki_cache[i] = NULL;
        }
    }

    tiki_count = 0;
    tiki_initialized = qfalse;
}

void TIKI_FlushAll(void) {
    Com_Printf("TIKI_FlushAll: flushing %d models\n", tiki_count);

    for (int i = 0; i < MAX_TIKI_MODELS; i++) {
        if (tiki_cache[i]) {
            TIKI_FreeModelData(tiki_cache[i]);
            Z_Free(tiki_cache[i]);
            tiki_cache[i] = NULL;
        }
    }
    tiki_count = 0;
}

/* =========================================================================
 * Model registration -- cache lookup + parse on miss
 * ========================================================================= */

dtiki_t TIKI_RegisterModel(const char *name) {
    if (!name || !name[0]) return -1;

    /* Search cache for existing model */
    for (int i = 0; i < MAX_TIKI_MODELS; i++) {
        if (tiki_cache[i] && !Q_stricmp(tiki_cache[i]->name, name)) {
            return i;
        }
    }

    /* Find a free slot */
    int slot = -1;
    for (int i = 0; i < MAX_TIKI_MODELS; i++) {
        if (!tiki_cache[i]) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        Com_Printf("TIKI_RegisterModel: cache full, can't load '%s'\n", name);
        return -1;
    }

    /* Parse the TIKI file */
    tiki_model_t *model = TIKI_ParseFile(name);
    if (!model) {
        return -1;
    }

    tiki_cache[slot] = model;
    tiki_count++;

    Com_DPrintf("TIKI_RegisterModel: '%s' -> slot %d (%d surfaces, %d anims)\n",
               name, slot, model->num_surfaces, model->num_anims);

    return slot;
}

tiki_model_t *TIKI_GetModel(dtiki_t handle) {
    if (handle < 0 || handle >= MAX_TIKI_MODELS)
        return NULL;
    return tiki_cache[handle];
}

void TIKI_FreeModel(dtiki_t handle) {
    if (handle < 0 || handle >= MAX_TIKI_MODELS) return;
    if (!tiki_cache[handle]) return;

    TIKI_FreeModelData(tiki_cache[handle]);
    Z_Free(tiki_cache[handle]);
    tiki_cache[handle] = NULL;
    tiki_count--;
}

const char *TIKI_NameForNum(dtiki_t handle) {
    tiki_model_t *m = TIKI_GetModel(handle);
    return m ? m->name : "";
}

/* =========================================================================
 * Animation queries -- used by game_import_t function pointers
 * ========================================================================= */

int TIKI_NumAnims(dtiki_t handle) {
    tiki_model_t *m = TIKI_GetModel(handle);
    return m ? m->num_anims : 0;
}

const char *TIKI_AnimName(dtiki_t handle, int animnum) {
    tiki_model_t *m = TIKI_GetModel(handle);
    if (!m || animnum < 0 || animnum >= m->num_anims) return "";
    return m->anims[animnum].alias;
}

int TIKI_AnimNumForName(dtiki_t handle, const char *name) {
    tiki_model_t *m = TIKI_GetModel(handle);
    if (!m || !name) return -1;

    for (int i = 0; i < m->num_anims; i++) {
        if (!Q_stricmp(m->anims[i].alias, name))
            return i;
    }
    return -1;
}

int TIKI_AnimRandom(dtiki_t handle, const char *name) {
    tiki_model_t *m = TIKI_GetModel(handle);
    if (!m || !name) return -1;

    /* Collect all animations matching this name (for random selection) */
    int matches[64];
    int count = 0;

    for (int i = 0; i < m->num_anims && count < 64; i++) {
        if (!Q_stricmp(m->anims[i].alias, name))
            matches[count++] = i;
    }

    if (count == 0) return -1;
    if (count == 1) return matches[0];

    /* Simple pseudo-random selection */
    return matches[Sys_Milliseconds() % count];
}

int TIKI_AnimNumFrames(dtiki_t handle, int animnum) {
    /* Frame count comes from the .ska/.tan binary file, not the .tik text.
     * For now return 0 -- this will be filled when we parse animation binaries. */
    (void)handle; (void)animnum;
    return 0;
}

float TIKI_AnimTime(dtiki_t handle, int animnum) {
    /* Animation length in seconds -- requires binary animation data */
    (void)handle; (void)animnum;
    return 0.0f;
}

int TIKI_AnimFlags(dtiki_t handle, int animnum) {
    tiki_model_t *m = TIKI_GetModel(handle);
    if (!m || animnum < 0 || animnum >= m->num_anims) return 0;
    return m->anims[animnum].flags;
}

int TIKI_AnimCrossblendTime(dtiki_t handle, int animnum) {
    tiki_model_t *m = TIKI_GetModel(handle);
    if (!m || animnum < 0 || animnum >= m->num_anims) return 0;
    return m->anims[animnum].blendtime;
}

qboolean TIKI_AnimHasCommands(dtiki_t handle, int animnum) {
    tiki_model_t *m = TIKI_GetModel(handle);
    if (!m || animnum < 0 || animnum >= m->num_anims) return qfalse;
    return (m->anims[animnum].num_server_cmds > 0 ||
            m->anims[animnum].num_client_cmds > 0);
}

/* =========================================================================
 * Surface queries
 * ========================================================================= */

int TIKI_NumSurfaces(dtiki_t handle) {
    tiki_model_t *m = TIKI_GetModel(handle);
    return m ? m->num_surfaces : 0;
}

int TIKI_SurfaceNameToNum(dtiki_t handle, const char *name) {
    tiki_model_t *m = TIKI_GetModel(handle);
    if (!m || !name) return -1;

    for (int i = 0; i < m->num_surfaces; i++) {
        if (!Q_stricmp(m->surfaces[i].name, name))
            return i;
    }
    return -1;
}

const char *TIKI_SurfaceNumToName(dtiki_t handle, int num) {
    tiki_model_t *m = TIKI_GetModel(handle);
    if (!m || num < 0 || num >= m->num_surfaces) return "";
    return m->surfaces[num].name;
}

int TIKI_SurfaceFlags(dtiki_t handle, int num) {
    tiki_model_t *m = TIKI_GetModel(handle);
    if (!m || num < 0 || num >= m->num_surfaces) return 0;
    return m->surfaces[num].flags;
}

/* =========================================================================
 * Tag (bone) queries -- requires skeleton binary data
 * ========================================================================= */

int TIKI_NumTags(dtiki_t handle) {
    /* Tag count comes from .skb binary -- stub until skeleton loader exists */
    (void)handle;
    return 0;
}

int TIKI_TagNumForName(dtiki_t handle, const char *name) {
    (void)handle; (void)name;
    return -1;
}

const char *TIKI_TagNameForNum(dtiki_t handle, int num) {
    (void)handle; (void)num;
    return "";
}

/* =========================================================================
 * Bounds and init commands
 * ========================================================================= */

void TIKI_CalculateBounds(dtiki_t handle, float scale, vec3_t mins, vec3_t maxs) {
    /* Bounds calculation requires skeleton/mesh binary data */
    (void)handle;
    VectorSet(mins, -16 * scale, -16 * scale, 0);
    VectorSet(maxs, 16 * scale, 16 * scale, 72 * scale);
}

qboolean TIKI_InitCommands(dtiki_t handle, tiki_cmd_t *tiki_cmd) {
    tiki_model_t *m = TIKI_GetModel(handle);
    if (!m || !tiki_cmd) return qfalse;

    tiki_cmd->num_cmds = 0;

    /* Convert server init commands to tiki_cmd_t format for the game DLL */
    for (int i = 0; i < m->num_server_initcmds && tiki_cmd->num_cmds < 128; i++) {
        tiki_initcmd_t *src = &m->server_initcmds[i];
        int idx = tiki_cmd->num_cmds;

        tiki_cmd->cmds[idx].argc = src->num_args;
        tiki_cmd->cmds[idx].argv = src->args;
        tiki_cmd->num_cmds++;
    }

    return (tiki_cmd->num_cmds > 0);
}
