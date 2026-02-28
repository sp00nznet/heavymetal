/*
 * r_bsp.c -- BSP world loading and rendering
 *
 * FAKK2 BSP format (from SDK qfiles.h):
 *   Header: magic 0x464B414B ("FAKK"), version 12, checksum, 20 lumps
 *   Extended from Q3 BSP with 3 additional lumps:
 *     LUMP_ENTLIGHTS (17)    -- entity-based light sources
 *     LUMP_ENTLIGHTSVIS (18) -- entity light visibility data
 *     LUMP_LIGHTDEFS (19)    -- per-shader lighting properties
 *
 * pak0.pk3 contains 30 BSP maps spanning the game:
 *   1_introjungle, 2_swamp, 3_creaturegarden, 4_watermine,
 *   5_waterminemine, 6_lava, ... 28_julie_end, 29_credits
 *
 * Key differences from Q3 BSP:
 *   - dshader_t has 'subdivisions' field (76 bytes vs Q3's 72)
 *   - dsurface_t has 'subdivisions' field
 *   - MST_TERRAIN surface type for terrain rendering
 *   - dheader_t has 'checksum' field after version
 */

#include "../common/qcommon.h"
#include "../common/qfiles.h"
#include "tr_types.h"

#include <string.h>

/* =========================================================================
 * Loaded world data
 * ========================================================================= */

typedef struct {
    char        name[MAX_QPATH];
    dheader_t   header;

    int         numShaders;
    dshader_t   *shaders;

    int         numPlanes;
    dplane_t    *planes;

    int         numNodes;
    dnode_t     *nodes;

    int         numLeafs;
    dleaf_t     *leafs;

    int         numLeafSurfaces;
    int         *leafSurfaces;

    int         numLeafBrushes;
    int         *leafBrushes;

    int         numModels;
    dmodel_t    *models;

    int         numBrushes;
    dbrush_t    *brushes;

    int         numBrushSides;
    dbrushside_t *brushSides;

    int         numSurfaces;
    dsurface_t  *surfaces;

    int         numVerts;
    drawVert_t  *verts;

    int         numIndexes;
    int         *indexes;

    int         numFogs;
    dfog_t      *fogs;

    /* Lightmaps stored as raw pixel data */
    int         numLightmaps;
    byte        *lightmapData;

    /* Visibility */
    dvis_t      *vis;
    int         visLen;

    /* Light grid */
    byte        *lightGrid;
    int         lightGridLen;

    /* Entity string */
    char        *entityString;
    int         entityStringLen;

    /* FAKK2 extensions */
    int         numEntLights;
    dentlight_t *entLights;
    byte        *entLightsVis;
    int         entLightsVisLen;
    int         numLightDefs;
    dlightdef_t *lightDefs;

    qboolean    loaded;
} bspWorld_t;

static bspWorld_t s_world;

/* =========================================================================
 * Lump loading helpers
 * ========================================================================= */

static void *R_LoadLump(const byte *filedata, const dheader_t *header,
                        int lumpId, int elemSize, int *count) {
    const lump_t *lump = &header->lumps[lumpId];

    if (lump->filelen == 0) {
        *count = 0;
        return NULL;
    }

    if (lump->filelen % elemSize != 0) {
        Com_Printf("R_LoadLump: lump %d has odd size (%d / %d)\n",
                   lumpId, lump->filelen, elemSize);
    }

    *count = lump->filelen / elemSize;
    void *out = Z_Malloc(lump->filelen);
    memcpy(out, filedata + lump->fileofs, lump->filelen);
    return out;
}

/* Load a variable-length lump (entity string, visibility, lightgrid) */
static void *R_LoadLumpRaw(const byte *filedata, const dheader_t *header,
                           int lumpId, int *length) {
    const lump_t *lump = &header->lumps[lumpId];

    if (lump->filelen == 0) {
        *length = 0;
        return NULL;
    }

    *length = lump->filelen;
    void *out = Z_Malloc(lump->filelen + 1);   /* +1 for entity string null */
    memcpy(out, filedata + lump->fileofs, lump->filelen);
    ((byte *)out)[lump->filelen] = '\0';
    return out;
}

/* =========================================================================
 * R_LoadBSP -- load a FAKK2 BSP world map
 * ========================================================================= */

qboolean R_LoadBSP(const char *name) {
    void *buffer;
    long len;

    Com_Printf("--- R_LoadBSP ---\n");
    Com_Printf("Loading %s\n", name);

    len = FS_ReadFile(name, &buffer);
    if (len < 0 || !buffer) {
        Com_Printf("R_LoadBSP: couldn't load '%s'\n", name);
        return qfalse;
    }

    if (len < (long)sizeof(dheader_t)) {
        Com_Printf("R_LoadBSP: '%s' too small for BSP header\n", name);
        FS_FreeFile(buffer);
        return qfalse;
    }

    const byte *filedata = (const byte *)buffer;
    const dheader_t *header = (const dheader_t *)filedata;

    /* Validate header */
    if (header->ident != BSP_IDENT) {
        Com_Printf("R_LoadBSP: '%s' has wrong ident (0x%08X, expected 0x%08X)\n",
                   name, header->ident, BSP_IDENT);
        FS_FreeFile(buffer);
        return qfalse;
    }

    if (header->version != BSP_FAKK_VERSION) {
        Com_Printf("R_LoadBSP: '%s' has wrong version (%d, expected %d)\n",
                   name, header->version, BSP_FAKK_VERSION);
        FS_FreeFile(buffer);
        return qfalse;
    }

    Com_Printf("BSP: ident=FAKK version=%d checksum=%d\n",
               header->version, header->checksum);

    /* Clear previous world */
    R_FreeWorld();

    memset(&s_world, 0, sizeof(s_world));
    Q_strncpyz(s_world.name, name, sizeof(s_world.name));
    memcpy(&s_world.header, header, sizeof(dheader_t));

    /* Load all lumps */
    s_world.shaders = (dshader_t *)R_LoadLump(filedata, header,
        LUMP_SHADERS, sizeof(dshader_t), &s_world.numShaders);

    s_world.planes = (dplane_t *)R_LoadLump(filedata, header,
        LUMP_PLANES, sizeof(dplane_t), &s_world.numPlanes);

    s_world.nodes = (dnode_t *)R_LoadLump(filedata, header,
        LUMP_NODES, sizeof(dnode_t), &s_world.numNodes);

    s_world.leafs = (dleaf_t *)R_LoadLump(filedata, header,
        LUMP_LEAFS, sizeof(dleaf_t), &s_world.numLeafs);

    s_world.leafSurfaces = (int *)R_LoadLump(filedata, header,
        LUMP_LEAFSURFACES, sizeof(int), &s_world.numLeafSurfaces);

    s_world.leafBrushes = (int *)R_LoadLump(filedata, header,
        LUMP_LEAFBRUSHES, sizeof(int), &s_world.numLeafBrushes);

    s_world.models = (dmodel_t *)R_LoadLump(filedata, header,
        LUMP_MODELS, sizeof(dmodel_t), &s_world.numModels);

    s_world.brushes = (dbrush_t *)R_LoadLump(filedata, header,
        LUMP_BRUSHES, sizeof(dbrush_t), &s_world.numBrushes);

    s_world.brushSides = (dbrushside_t *)R_LoadLump(filedata, header,
        LUMP_BRUSHSIDES, sizeof(dbrushside_t), &s_world.numBrushSides);

    s_world.surfaces = (dsurface_t *)R_LoadLump(filedata, header,
        LUMP_SURFACES, sizeof(dsurface_t), &s_world.numSurfaces);

    s_world.verts = (drawVert_t *)R_LoadLump(filedata, header,
        LUMP_DRAWVERTS, sizeof(drawVert_t), &s_world.numVerts);

    s_world.indexes = (int *)R_LoadLump(filedata, header,
        LUMP_DRAWINDEXES, sizeof(int), &s_world.numIndexes);

    s_world.fogs = (dfog_t *)R_LoadLump(filedata, header,
        LUMP_FOGS, sizeof(dfog_t), &s_world.numFogs);

    /* Variable-length lumps */
    s_world.lightmapData = (byte *)R_LoadLumpRaw(filedata, header,
        LUMP_LIGHTMAPS, &s_world.numLightmaps);

    s_world.vis = (dvis_t *)R_LoadLumpRaw(filedata, header,
        LUMP_VISIBILITY, &s_world.visLen);

    s_world.lightGrid = (byte *)R_LoadLumpRaw(filedata, header,
        LUMP_LIGHTGRID, &s_world.lightGridLen);

    s_world.entityString = (char *)R_LoadLumpRaw(filedata, header,
        LUMP_ENTITIES, &s_world.entityStringLen);

    /* FAKK2 extension lumps */
    s_world.entLights = (dentlight_t *)R_LoadLump(filedata, header,
        LUMP_ENTLIGHTS, sizeof(dentlight_t), &s_world.numEntLights);

    s_world.entLightsVis = (byte *)R_LoadLumpRaw(filedata, header,
        LUMP_ENTLIGHTSVIS, &s_world.entLightsVisLen);

    s_world.lightDefs = (dlightdef_t *)R_LoadLump(filedata, header,
        LUMP_LIGHTDEFS, sizeof(dlightdef_t), &s_world.numLightDefs);

    s_world.loaded = qtrue;

    /* Setup light grid for entity lighting */
    if (s_world.lightGrid && s_world.lightGridLen > 0 && s_world.models) {
        extern void R_SetupLightGrid(const float *worldMins, const float *worldMaxs,
                                      byte *gridData, int gridLen);
        R_SetupLightGrid(s_world.models[0].mins, s_world.models[0].maxs,
                          s_world.lightGrid, s_world.lightGridLen);
    }

    /* Load lightmaps into GL textures */
    if (s_world.lightmapData && s_world.numLightmaps > 0) {
        extern void R_LoadLightmaps(const byte *data, int dataLen);
        R_LoadLightmaps(s_world.lightmapData, s_world.numLightmaps);
    }

    FS_FreeFile(buffer);

    /* Print statistics */
    Com_Printf("BSP loaded: %s\n", name);
    Com_Printf("  %d shaders, %d planes, %d nodes, %d leafs\n",
               s_world.numShaders, s_world.numPlanes,
               s_world.numNodes, s_world.numLeafs);
    Com_Printf("  %d surfaces, %d verts, %d indexes\n",
               s_world.numSurfaces, s_world.numVerts, s_world.numIndexes);
    Com_Printf("  %d brushes, %d models, %d fogs\n",
               s_world.numBrushes, s_world.numModels, s_world.numFogs);
    Com_Printf("  %d entity lights, %d light defs (FAKK2 extensions)\n",
               s_world.numEntLights, s_world.numLightDefs);

    return qtrue;
}

/* =========================================================================
 * R_FreeWorld -- free loaded world data
 * ========================================================================= */

void R_FreeWorld(void) {
    if (!s_world.loaded) return;

    if (s_world.shaders) Z_Free(s_world.shaders);
    if (s_world.planes) Z_Free(s_world.planes);
    if (s_world.nodes) Z_Free(s_world.nodes);
    if (s_world.leafs) Z_Free(s_world.leafs);
    if (s_world.leafSurfaces) Z_Free(s_world.leafSurfaces);
    if (s_world.leafBrushes) Z_Free(s_world.leafBrushes);
    if (s_world.models) Z_Free(s_world.models);
    if (s_world.brushes) Z_Free(s_world.brushes);
    if (s_world.brushSides) Z_Free(s_world.brushSides);
    if (s_world.surfaces) Z_Free(s_world.surfaces);
    if (s_world.verts) Z_Free(s_world.verts);
    if (s_world.indexes) Z_Free(s_world.indexes);
    if (s_world.fogs) Z_Free(s_world.fogs);
    if (s_world.lightmapData) Z_Free(s_world.lightmapData);
    if (s_world.vis) Z_Free(s_world.vis);
    if (s_world.lightGrid) Z_Free(s_world.lightGrid);
    if (s_world.entityString) Z_Free(s_world.entityString);
    if (s_world.entLights) Z_Free(s_world.entLights);
    if (s_world.entLightsVis) Z_Free(s_world.entLightsVis);
    if (s_world.lightDefs) Z_Free(s_world.lightDefs);

    memset(&s_world, 0, sizeof(s_world));
}

/* =========================================================================
 * World queries
 * ========================================================================= */

const char *R_GetEntityString(void) {
    return s_world.entityString ? s_world.entityString : "";
}

int R_GetNumInlineModels(void) {
    return s_world.numModels;
}

qboolean R_WorldLoaded(void) {
    return s_world.loaded;
}

void *R_GetBSPWorldPtr(void) {
    return &s_world;
}
