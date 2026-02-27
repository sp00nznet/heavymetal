/*
 * sv_world.c -- Server-side world/collision
 *
 * Entity linking, area management, and collision detection.
 * Bridges between the game DLL's entity system and the collision
 * model (cm_main.c). The game DLL calls linkentity/unlinkentity
 * to register entity positions, then trace/pointcontents to
 * test collision against both the world BSP and linked entities.
 *
 * FAKK2's third-person gameplay requires careful collision for:
 *   - Julie's bounding box (larger than Q3 player)
 *   - Melee weapon sweeps (sword traces)
 *   - AI navigation and monster collision
 *   - Destructible objects
 */

#include "../common/qcommon.h"
#include "../common/g_public.h"
#include "../collision/cm_local.h"
#include <string.h>

/* =========================================================================
 * World entity list
 *
 * Linked entities are stored in a flat array for simplicity.
 * Q3 uses an area-based spatial partitioning tree, but a flat scan
 * is sufficient for initial bring-up.
 * ========================================================================= */

#define MAX_WORLD_ENTITIES  MAX_GENTITIES

typedef struct {
    gentity_t   *ent;
    qboolean    linked;
} worldEntity_t;

static worldEntity_t    sv_worldEntities[MAX_WORLD_ENTITIES];
static int              sv_numWorldEntities;

/* =========================================================================
 * Game data locator
 *
 * The game DLL calls LocateGameData to tell us where entities live
 * in its address space, so we can index them by entity number.
 * ========================================================================= */

static gentity_t    *sv_gentities;
static int          sv_gentitySize;
static int          sv_numEntities;
static playerState_t *sv_clients;
static int          sv_clientSize;

void SV_LocateGameData(gentity_t *gEnts, int numGEntities,
                       int sizeofGEntity_t, playerState_t *clients,
                       int sizeofGameClient) {
    sv_gentities = gEnts;
    sv_numEntities = numGEntities;
    sv_gentitySize = sizeofGEntity_t;
    sv_clients = clients;
    sv_clientSize = sizeofGameClient;
}

static gentity_t *SV_GentityNum(int num) {
    if (!sv_gentities || num < 0 || num >= sv_numEntities)
        return NULL;
    return (gentity_t *)((byte *)sv_gentities + num * sv_gentitySize);
}

playerState_t *SV_GetClientPlayerState(int clientNum) {
    if (!sv_clients || clientNum < 0) return NULL;
    return (playerState_t *)((byte *)sv_clients + clientNum * sv_clientSize);
}

/* =========================================================================
 * Entity linking
 * ========================================================================= */

void SV_LinkEntity(gentity_t *ent) {
    if (!ent) return;

    /* Unlink first if already linked */
    if (ent->linked) {
        SV_UnlinkEntity(ent);
    }

    /* Compute absolute bounding box */
    VectorAdd(ent->currentOrigin, ent->mins, ent->absmin);
    VectorAdd(ent->currentOrigin, ent->maxs, ent->absmax);

    /* For BSP models, use the model's bounds */
    if (ent->bmodel) {
        /* absmin/absmax already set from SetBrushModel */
    }

    /* Determine area */
    ent->areanum = CM_LeafArea(CM_PointLeafnum(ent->currentOrigin));

    /* Register in world entity list */
    if (sv_numWorldEntities < MAX_WORLD_ENTITIES) {
        int slot = ent->s.number;
        if (slot >= 0 && slot < MAX_WORLD_ENTITIES) {
            sv_worldEntities[slot].ent = ent;
            sv_worldEntities[slot].linked = qtrue;
        }
    }

    ent->linked = qtrue;
    ent->linkcount++;
}

void SV_UnlinkEntity(gentity_t *ent) {
    if (!ent || !ent->linked) return;

    int slot = ent->s.number;
    if (slot >= 0 && slot < MAX_WORLD_ENTITIES) {
        sv_worldEntities[slot].linked = qfalse;
        sv_worldEntities[slot].ent = NULL;
    }

    ent->linked = qfalse;
}

/* =========================================================================
 * SV_SetBrushModel -- associate a BSP inline model with an entity
 * ========================================================================= */

void SV_SetBrushModel(gentity_t *ent, const char *name) {
    if (!ent || !name) return;

    if (name[0] != '*') {
        Com_Error(ERR_DROP, "SV_SetBrushModel: '%s' is not a brush model", name);
        return;
    }

    int modelIndex = atoi(name + 1);
    if (modelIndex < 0 || modelIndex >= CM_NumInlineModels()) {
        Com_Error(ERR_DROP, "SV_SetBrushModel: bad modelindex %d", modelIndex);
        return;
    }

    ent->bmodel = qtrue;
    ent->s.modelindex = modelIndex;

    clipHandle_t h = CM_InlineModel(modelIndex);
    /* TODO: Get bounds from CM submodel and set ent->mins/maxs */
    (void)h;
}

/* =========================================================================
 * Area queries
 * ========================================================================= */

int SV_AreaEntities(vec3_t mins, vec3_t maxs, int *list, int maxcount) {
    int count = 0;

    for (int i = 0; i < MAX_WORLD_ENTITIES && count < maxcount; i++) {
        if (!sv_worldEntities[i].linked) continue;
        gentity_t *ent = sv_worldEntities[i].ent;
        if (!ent) continue;

        /* AABB overlap test */
        if (ent->absmin[0] > maxs[0] || ent->absmin[1] > maxs[1] || ent->absmin[2] > maxs[2])
            continue;
        if (ent->absmax[0] < mins[0] || ent->absmax[1] < mins[1] || ent->absmax[2] < mins[2])
            continue;

        list[count++] = i;
    }

    return count;
}

/* =========================================================================
 * SV_ClipToEntity -- trace against a single entity
 * ========================================================================= */

void SV_ClipToEntity(trace_t *trace, const vec3_t start, const vec3_t mins,
                     const vec3_t maxs, const vec3_t end, int entityNum,
                     int contentmask) {
    memset(trace, 0, sizeof(*trace));
    trace->fraction = 1.0f;
    trace->entityNum = entityNum;

    gentity_t *ent = SV_GentityNum(entityNum);
    if (!ent) return;

    if (ent->bmodel) {
        /* Trace against BSP inline model */
        clipHandle_t h = CM_InlineModel(ent->s.modelindex);
        CM_TransformedBoxTrace(trace, start, end, mins, maxs, h,
                               contentmask, ent->currentOrigin,
                               ent->currentAngles, qfalse);
    } else {
        /* Trace against entity bounding box */
        clipHandle_t h = CM_TempBoxModel(ent->mins, ent->maxs, ent->contents);
        CM_TransformedBoxTrace(trace, start, end, mins, maxs, h,
                               contentmask, ent->currentOrigin,
                               ent->currentAngles, qfalse);
    }

    if (trace->fraction < 1.0f) {
        trace->entityNum = entityNum;
    }
}

/* =========================================================================
 * SV_Trace -- full server-side trace against world + entities
 *
 * This is the main collision function called by the game DLL.
 * First traces against the world BSP, then against all linked entities.
 * ========================================================================= */

void SV_Trace(trace_t *result, const vec3_t start, const vec3_t mins,
              const vec3_t maxs, const vec3_t end, int passEntityNum,
              int contentmask, qboolean cylinder) {
    /* Trace against world */
    CM_BoxTrace(result, start, end, mins, maxs, 0, contentmask, cylinder);
    result->entityNum = result->fraction < 1.0f ? 1022 : -1;  /* ENTITYNUM_WORLD */

    /* Trace against entities */
    vec3_t clipMins, clipMaxs;
    for (int i = 0; i < 3; i++) {
        float startVal = (start[i] < end[i]) ? start[i] : end[i];
        float endVal = (start[i] > end[i]) ? start[i] : end[i];
        clipMins[i] = startVal + (mins ? mins[i] : 0) - 1;
        clipMaxs[i] = endVal + (maxs ? maxs[i] : 0) + 1;
    }

    for (int i = 0; i < MAX_WORLD_ENTITIES; i++) {
        if (!sv_worldEntities[i].linked) continue;
        gentity_t *ent = sv_worldEntities[i].ent;
        if (!ent || i == passEntityNum) continue;

        /* Skip entities that don't match content mask */
        if (!(ent->contents & contentmask)) continue;

        /* Quick AABB reject */
        if (ent->absmin[0] > clipMaxs[0] || ent->absmin[1] > clipMaxs[1] || ent->absmin[2] > clipMaxs[2])
            continue;
        if (ent->absmax[0] < clipMins[0] || ent->absmax[1] < clipMins[1] || ent->absmax[2] < clipMins[2])
            continue;

        trace_t entTrace;
        SV_ClipToEntity(&entTrace, start, mins, maxs, end, i, contentmask);

        if (entTrace.fraction < result->fraction) {
            *result = entTrace;
            result->entityNum = i;
        }
    }
}

/* =========================================================================
 * SV_PointContents -- content flags at a point (world + entities)
 * ========================================================================= */

int SV_PointContents(const vec3_t p, int passEntityNum) {
    int contents = CM_PointContents(p, 0);

    /* Check entities */
    for (int i = 0; i < MAX_WORLD_ENTITIES; i++) {
        if (!sv_worldEntities[i].linked) continue;
        if (i == passEntityNum) continue;
        gentity_t *ent = sv_worldEntities[i].ent;
        if (!ent) continue;

        /* Quick AABB check */
        if (p[0] < ent->absmin[0] || p[0] > ent->absmax[0]) continue;
        if (p[1] < ent->absmin[1] || p[1] > ent->absmax[1]) continue;
        if (p[2] < ent->absmin[2] || p[2] > ent->absmax[2]) continue;

        if (ent->bmodel) {
            contents |= CM_TransformedPointContents(p, CM_InlineModel(ent->s.modelindex),
                                                     ent->currentOrigin, ent->currentAngles);
        } else {
            contents |= ent->contents;
        }
    }

    return contents;
}

/* =========================================================================
 * Config strings
 *
 * Config strings are indexed strings shared between server and clients.
 * They carry model names, sound names, and other level-specific data.
 * ========================================================================= */

static char sv_configstrings[MAX_CONFIGSTRINGS][MAX_INFO_STRING];

void SV_SetConfigstring(int index, const char *val) {
    if (index < 0 || index >= MAX_CONFIGSTRINGS) {
        Com_Error(ERR_DROP, "SV_SetConfigstring: bad index %d", index);
        return;
    }
    Q_strncpyz(sv_configstrings[index], val ? val : "", sizeof(sv_configstrings[0]));
}

char *SV_GetConfigstring(int index) {
    if (index < 0 || index >= MAX_CONFIGSTRINGS) return "";
    return sv_configstrings[index];
}

/* =========================================================================
 * Userinfo strings
 * ========================================================================= */

static char sv_userinfo[MAX_CLIENTS][MAX_INFO_STRING];

void SV_SetUserinfo(int index, const char *val) {
    if (index < 0 || index >= MAX_CLIENTS) return;
    Q_strncpyz(sv_userinfo[index], val ? val : "", sizeof(sv_userinfo[0]));
}

void SV_GetUserinfo(int index, char *buffer, int bufferSize) {
    if (index < 0 || index >= MAX_CLIENTS) {
        if (buffer && bufferSize > 0) buffer[0] = '\0';
        return;
    }
    Q_strncpyz(buffer, sv_userinfo[index], bufferSize);
}

/* =========================================================================
 * Server commands
 * ========================================================================= */

void SV_SendServerCommand(int clientnum, const char *fmt, ...) {
    va_list args;
    char buf[4096];
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    /* TODO: Actually queue command for transmission to client */
    Com_DPrintf("SV_SendServerCommand(%d): %s\n", clientnum, buf);
}

/* =========================================================================
 * Resource indexing
 *
 * Maps resource names to integer indices for network transmission.
 * Each resource type uses a range of config strings.
 * ========================================================================= */

#define CS_MODELS       32
#define CS_SOUNDS       (CS_MODELS + MAX_MODELS)
#define CS_IMAGES       (CS_SOUNDS + MAX_SOUNDS)

int SV_ModelIndex(const char *name) {
    if (!name || !name[0]) return 0;

    /* Search existing */
    for (int i = 1; i < MAX_MODELS; i++) {
        if (sv_configstrings[CS_MODELS + i][0] &&
            !Q_stricmp(sv_configstrings[CS_MODELS + i], name))
            return i;
    }

    /* Find empty slot */
    for (int i = 1; i < MAX_MODELS; i++) {
        if (!sv_configstrings[CS_MODELS + i][0]) {
            SV_SetConfigstring(CS_MODELS + i, name);

            /* Also register with TIKI system if it's a .tik file */
            int len = (int)strlen(name);
            if (len > 4 && !Q_stricmp(name + len - 4, ".tik")) {
                extern dtiki_t TIKI_RegisterModel(const char *name);
                TIKI_RegisterModel(name);
            }

            return i;
        }
    }

    Com_Error(ERR_DROP, "SV_ModelIndex: overflow");
    return 0;
}

int SV_SoundIndex(const char *name) {
    if (!name || !name[0]) return 0;

    for (int i = 1; i < MAX_SOUNDS; i++) {
        if (sv_configstrings[CS_SOUNDS + i][0] &&
            !Q_stricmp(sv_configstrings[CS_SOUNDS + i], name))
            return i;
    }

    for (int i = 1; i < MAX_SOUNDS; i++) {
        if (!sv_configstrings[CS_SOUNDS + i][0]) {
            SV_SetConfigstring(CS_SOUNDS + i, name);
            return i;
        }
    }

    Com_Error(ERR_DROP, "SV_SoundIndex: overflow");
    return 0;
}

int SV_ImageIndex(const char *name) {
    if (!name || !name[0]) return 0;

    for (int i = 1; i < 256; i++) {
        if (sv_configstrings[CS_IMAGES + i][0] &&
            !Q_stricmp(sv_configstrings[CS_IMAGES + i], name))
            return i;
    }

    for (int i = 1; i < 256; i++) {
        if (!sv_configstrings[CS_IMAGES + i][0]) {
            SV_SetConfigstring(CS_IMAGES + i, name);
            return i;
        }
    }

    return 0;
}

int SV_ItemIndex(const char *name) {
    /* Items use model indices in FAKK2 */
    return SV_ModelIndex(name);
}

/* =========================================================================
 * Misc server utilities
 * ========================================================================= */

const char *SV_GameDir(void) {
    return FAKK_GAME_DIR;
}

qboolean SV_IsModel(int index) {
    if (index <= 0 || index >= MAX_MODELS) return qfalse;
    return sv_configstrings[CS_MODELS + index][0] != '\0';
}

void SV_SetModel(gentity_t *ent, const char *name) {
    if (!ent || !name) return;

    if (name[0] == '*') {
        SV_SetBrushModel(ent, name);
    } else {
        ent->s.modelindex = SV_ModelIndex(name);
    }
}

void SV_SetLightStyle(int i, const char *data) {
    /* TODO: Store light style data for transmission to clients */
    (void)i; (void)data;
}

void SV_SetFarPlane(int farplane) {
    (void)farplane;
}

void SV_SetSkyPortal(qboolean skyportal) {
    (void)skyportal;
}

void SV_DebugGraph(float value, int color) {
    (void)value; (void)color;
}

void SV_Centerprintf(gentity_t *ent, const char *fmt, ...) {
    va_list args;
    char buf[4096];
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    (void)ent;
    Com_Printf("CENTERPRINT: %s\n", buf);
}

void SV_Locationprintf(gentity_t *ent, int x, int y, const char *fmt, ...) {
    va_list args;
    char buf[4096];
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    (void)ent; (void)x; (void)y;
    Com_DPrintf("LOCATIONPRINT(%d,%d): %s\n", x, y, buf);
}

/* CRC utility */
unsigned short SV_CalcCRC(const unsigned char *start, int count) {
    unsigned short crc = 0xFFFF;
    for (int i = 0; i < count; i++) {
        crc ^= start[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }
    return crc;
}

const char *SV_GetArchiveFileName(const char *filename, const char *extension) {
    static char buf[MAX_QPATH];
    snprintf(buf, sizeof(buf), "%s.%s", filename, extension);
    return buf;
}
