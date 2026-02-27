/*
 * cm_main.c -- Collision model: BSP-based clipping
 *
 * Loads collision data from FAKK2 BSP files and provides trace/query
 * functions used by both server (sv_game.c) and client (cl_cgame.c).
 *
 * FAKK2 extends Q3's collision with:
 *   - Cylinder traces for third-person character collision
 *   - Additional content flags (CONTENTS_WEAPONCLIP)
 *   - Material surface types (wood, glass, rock, etc.)
 *
 * The collision model operates independently from the renderer --
 * both load the same BSP file but keep separate copies of the data.
 */

#include "cm_local.h"
#include "../common/qfiles.h"
#include <string.h>
#include <math.h>

/* =========================================================================
 * Global collision state
 * ========================================================================= */

static clipMap_t cm;

/* Temporary box model for entity-vs-entity collision */
static cmodel_t     box_model;
static cplane_t     box_planes[12];
static cbrushside_t box_sides[6];
static cbrush_t     box_brush;

/* =========================================================================
 * Initialization
 * ========================================================================= */

void CM_Init(void) {
    Com_Printf("--- CM_Init ---\n");
    CM_ClearMap();
}

void CM_Shutdown(void) {
    CM_ClearMap();
}

void CM_ClearMap(void) {
    if (cm.planes) Z_Free(cm.planes);
    if (cm.nodes) Z_Free(cm.nodes);
    if (cm.leafs) Z_Free(cm.leafs);
    if (cm.leafBrushes) Z_Free(cm.leafBrushes);
    if (cm.brushSides) Z_Free(cm.brushSides);
    if (cm.brushes) Z_Free(cm.brushes);
    if (cm.visibility) Z_Free(cm.visibility);
    if (cm.entityString) Z_Free(cm.entityString);

    memset(&cm, 0, sizeof(cm));

    /* Set up an empty leaf at index 0 */
    cm.numLeafs = 1;
    cm.leafs = (cleaf_t *)Z_TagMalloc(sizeof(cleaf_t), TAG_GENERAL);
    memset(cm.leafs, 0, sizeof(cleaf_t));

    /* Default submodel 0 (world) */
    cm.numSubModels = 1;
    memset(&cm.subModels[0], 0, sizeof(cmodel_t));
}

/* =========================================================================
 * CM_LoadMap -- parse BSP collision data
 *
 * Reads planes, brushes, brush sides, nodes, leafs, and visibility
 * from the BSP file. The renderer loads the same file separately
 * for its own surface/vertex data.
 * ========================================================================= */

void CM_LoadMap(const char *name) {
    if (!name || !name[0]) {
        CM_ClearMap();
        return;
    }

    /* Already loaded? */
    if (cm.loaded && !Q_stricmp(cm.name, name)) {
        return;
    }

    CM_ClearMap();

    Com_Printf("CM_LoadMap: %s\n", name);

    void *buffer;
    long filelen = FS_ReadFile(name, &buffer);
    if (filelen < 0 || !buffer) {
        Com_Printf("CM_LoadMap: couldn't load '%s'\n", name);
        return;
    }

    const byte *fileBase = (const byte *)buffer;
    const dheader_t *header = (const dheader_t *)fileBase;

    /* Validate header */
    if (header->ident != BSP_IDENT) {
        Com_Printf("CM_LoadMap: '%s' wrong ident (0x%08X)\n", name, header->ident);
        FS_FreeFile(buffer);
        return;
    }

    if (header->version != BSP_FAKK_VERSION) {
        Com_Printf("CM_LoadMap: '%s' wrong version (%d)\n", name, header->version);
        FS_FreeFile(buffer);
        return;
    }

    Q_strncpyz(cm.name, name, sizeof(cm.name));

    /* --- Load shaders --- */
    {
        const lump_t *l = &header->lumps[LUMP_SHADERS];
        int count = l->filelen / sizeof(dshader_t);
        if (count > MAX_CM_SHADERS) count = MAX_CM_SHADERS;
        const dshader_t *in = (const dshader_t *)(fileBase + l->fileofs);
        cm.numShaders = count;
        for (int i = 0; i < count; i++) {
            cm.shaders[i].surfaceFlags = in[i].surfaceFlags;
            cm.shaders[i].contentFlags = in[i].contentFlags;
        }
    }

    /* --- Load planes --- */
    {
        const lump_t *l = &header->lumps[LUMP_PLANES];
        int count = l->filelen / sizeof(dplane_t);
        cm.numPlanes = count;
        cm.planes = (cplane_t *)Z_TagMalloc(count * sizeof(cplane_t), TAG_GENERAL);
        const dplane_t *in = (const dplane_t *)(fileBase + l->fileofs);
        for (int i = 0; i < count; i++) {
            cm.planes[i].normal[0] = in[i].normal[0];
            cm.planes[i].normal[1] = in[i].normal[1];
            cm.planes[i].normal[2] = in[i].normal[2];
            cm.planes[i].dist = in[i].dist;

            /* Classify plane type */
            if (cm.planes[i].normal[0] == 1.0f) cm.planes[i].type = 0;
            else if (cm.planes[i].normal[1] == 1.0f) cm.planes[i].type = 1;
            else if (cm.planes[i].normal[2] == 1.0f) cm.planes[i].type = 2;
            else cm.planes[i].type = 3;

            /* Compute signbits */
            int bits = 0;
            for (int j = 0; j < 3; j++) {
                if (cm.planes[i].normal[j] < 0) bits |= (1 << j);
            }
            cm.planes[i].signbits = bits;
        }
    }

    /* --- Load nodes --- */
    {
        const lump_t *l = &header->lumps[LUMP_NODES];
        int count = l->filelen / sizeof(dnode_t);
        cm.numNodes = count;
        cm.nodes = (cnode_t *)Z_TagMalloc(count * sizeof(cnode_t), TAG_GENERAL);
        const dnode_t *in = (const dnode_t *)(fileBase + l->fileofs);
        for (int i = 0; i < count; i++) {
            cm.nodes[i].planeNum = in[i].planeNum;
            cm.nodes[i].children[0] = in[i].children[0];
            cm.nodes[i].children[1] = in[i].children[1];
        }
    }

    /* --- Load leafs --- */
    {
        const lump_t *l = &header->lumps[LUMP_LEAFS];
        int count = l->filelen / sizeof(dleaf_t);
        /* Reallocate since ClearMap set up one leaf */
        Z_Free(cm.leafs);
        cm.numLeafs = count;
        cm.leafs = (cleaf_t *)Z_TagMalloc(count * sizeof(cleaf_t), TAG_GENERAL);
        const dleaf_t *in = (const dleaf_t *)(fileBase + l->fileofs);
        for (int i = 0; i < count; i++) {
            cm.leafs[i].cluster = in[i].cluster;
            cm.leafs[i].area = in[i].area;
            cm.leafs[i].firstLeafBrush = in[i].firstLeafBrush;
            cm.leafs[i].numLeafBrushes = in[i].numLeafBrushes;
        }

        /* Count clusters */
        cm.numClusters = 0;
        for (int i = 0; i < count; i++) {
            if (cm.leafs[i].cluster >= cm.numClusters)
                cm.numClusters = cm.leafs[i].cluster + 1;
        }
    }

    /* --- Load leaf brushes --- */
    {
        const lump_t *l = &header->lumps[LUMP_LEAFBRUSHES];
        int count = l->filelen / sizeof(int);
        cm.numLeafBrushes = count;
        cm.leafBrushes = (int *)Z_TagMalloc(count * sizeof(int), TAG_GENERAL);
        memcpy(cm.leafBrushes, fileBase + l->fileofs, count * sizeof(int));
    }

    /* --- Load brush sides --- */
    {
        const lump_t *l = &header->lumps[LUMP_BRUSHSIDES];
        int count = l->filelen / sizeof(dbrushside_t);
        cm.numBrushSides = count;
        cm.brushSides = (cbrushside_t *)Z_TagMalloc(count * sizeof(cbrushside_t), TAG_GENERAL);
        const dbrushside_t *in = (const dbrushside_t *)(fileBase + l->fileofs);
        for (int i = 0; i < count; i++) {
            cm.brushSides[i].plane = &cm.planes[in[i].planeNum];
            cm.brushSides[i].shaderNum = in[i].shaderNum;
            if (in[i].shaderNum >= 0 && in[i].shaderNum < cm.numShaders) {
                cm.brushSides[i].surfaceFlags = cm.shaders[in[i].shaderNum].surfaceFlags;
            }
        }
    }

    /* --- Load brushes --- */
    {
        const lump_t *l = &header->lumps[LUMP_BRUSHES];
        int count = l->filelen / sizeof(dbrush_t);
        cm.numBrushes = count;
        cm.brushes = (cbrush_t *)Z_TagMalloc(count * sizeof(cbrush_t), TAG_GENERAL);
        const dbrush_t *in = (const dbrush_t *)(fileBase + l->fileofs);
        for (int i = 0; i < count; i++) {
            cm.brushes[i].sides = &cm.brushSides[in[i].firstSide];
            cm.brushes[i].numSides = in[i].numSides;
            cm.brushes[i].checkcount = 0;
            if (in[i].shaderNum >= 0 && in[i].shaderNum < cm.numShaders) {
                cm.brushes[i].contentFlags = cm.shaders[in[i].shaderNum].contentFlags;
            }
        }
    }

    /* --- Load submodels --- */
    {
        const lump_t *l = &header->lumps[LUMP_MODELS];
        int count = l->filelen / sizeof(dmodel_t);
        if (count > MAX_SUBMODELS) count = MAX_SUBMODELS;
        cm.numSubModels = count;
        const dmodel_t *in = (const dmodel_t *)(fileBase + l->fileofs);
        for (int i = 0; i < count; i++) {
            cm.subModels[i].mins[0] = in[i].mins[0];
            cm.subModels[i].mins[1] = in[i].mins[1];
            cm.subModels[i].mins[2] = in[i].mins[2];
            cm.subModels[i].maxs[0] = in[i].maxs[0];
            cm.subModels[i].maxs[1] = in[i].maxs[1];
            cm.subModels[i].maxs[2] = in[i].maxs[2];
            cm.subModels[i].firstBrush = in[i].firstBrush;
            cm.subModels[i].numBrushes = in[i].numBrushes;
        }
    }

    /* --- Load visibility --- */
    {
        const lump_t *l = &header->lumps[LUMP_VISIBILITY];
        if (l->filelen > 0) {
            const dvis_t *vis = (const dvis_t *)(fileBase + l->fileofs);
            cm.numClusters = vis->numClusters;
            cm.clusterBytes = vis->clusterBytes;
            int visSize = cm.numClusters * cm.clusterBytes;
            cm.visibility = (byte *)Z_TagMalloc(visSize, TAG_GENERAL);
            memcpy(cm.visibility, (const byte *)vis + sizeof(dvis_t), visSize);
        }
    }

    /* --- Load entity string --- */
    {
        const lump_t *l = &header->lumps[LUMP_ENTITIES];
        if (l->filelen > 0) {
            cm.numEntityChars = l->filelen;
            cm.entityString = (char *)Z_TagMalloc(l->filelen + 1, TAG_GENERAL);
            memcpy(cm.entityString, fileBase + l->fileofs, l->filelen);
            cm.entityString[l->filelen] = '\0';
        }
    }

    cm.loaded = qtrue;

    Com_Printf("CM_LoadMap: %d planes, %d brushes, %d nodes, %d leafs, %d submodels\n",
               cm.numPlanes, cm.numBrushes, cm.numNodes, cm.numLeafs, cm.numSubModels);

    FS_FreeFile(buffer);
}

/* =========================================================================
 * Model queries
 * ========================================================================= */

int CM_NumInlineModels(void) {
    return cm.numSubModels;
}

clipHandle_t CM_InlineModel(int index) {
    if (index < 0 || index >= cm.numSubModels) {
        Com_Error(ERR_DROP, "CM_InlineModel: bad number %d", index);
        return 0;
    }
    return index;
}

const char *CM_EntityString(void) {
    return cm.entityString ? cm.entityString : "";
}

/* =========================================================================
 * CM_TempBoxModel -- create a temporary box brush for entity collision
 *
 * Used when tracing against entity bounding boxes rather than BSP models.
 * ========================================================================= */

clipHandle_t CM_TempBoxModel(const vec3_t mins, const vec3_t maxs, int contents) {
    box_model.mins[0] = mins[0];
    box_model.mins[1] = mins[1];
    box_model.mins[2] = mins[2];
    box_model.maxs[0] = maxs[0];
    box_model.maxs[1] = maxs[1];
    box_model.maxs[2] = maxs[2];

    box_brush.numSides = 6;
    box_brush.sides = box_sides;
    box_brush.contentFlags = contents;

    /* Set up 6 axial planes */
    for (int i = 0; i < 6; i++) {
        int axis = i / 2;
        box_sides[i].plane = &box_planes[i];
        box_sides[i].surfaceFlags = 0;

        VectorClear(box_planes[i].normal);
        if (i & 1) {
            box_planes[i].normal[axis] = -1.0f;
            box_planes[i].dist = -mins[axis];
        } else {
            box_planes[i].normal[axis] = 1.0f;
            box_planes[i].dist = maxs[axis];
        }
        box_planes[i].type = axis;
        box_planes[i].signbits = (i & 1) ? (1 << axis) : 0;
    }

    return BOX_MODEL_HANDLE;
}

/* =========================================================================
 * Point leaf lookup
 * ========================================================================= */

int CM_PointLeafnum(const vec3_t p) {
    if (!cm.numNodes) return 0;

    int num = 0;
    while (num >= 0) {
        cnode_t *node = &cm.nodes[num];
        cplane_t *plane = &cm.planes[node->planeNum];

        float d;
        if (plane->type < 3) {
            d = p[plane->type] - plane->dist;
        } else {
            d = DotProduct(p, plane->normal) - plane->dist;
        }

        if (d >= 0)
            num = node->children[0];
        else
            num = node->children[1];
    }

    return -(num + 1);
}

int CM_LeafCluster(int leafnum) {
    if (leafnum < 0 || leafnum >= cm.numLeafs) return -1;
    return cm.leafs[leafnum].cluster;
}

int CM_LeafArea(int leafnum) {
    if (leafnum < 0 || leafnum >= cm.numLeafs) return 0;
    return cm.leafs[leafnum].area;
}

/* =========================================================================
 * CM_PointContents -- content flags at a world point
 * ========================================================================= */

static int CM_PointContentsLeaf(const vec3_t p, int leafnum) {
    if (leafnum < 0 || leafnum >= cm.numLeafs) return 0;

    cleaf_t *leaf = &cm.leafs[leafnum];
    int contents = 0;

    for (int i = 0; i < leaf->numLeafBrushes; i++) {
        int brushnum = cm.leafBrushes[leaf->firstLeafBrush + i];
        if (brushnum < 0 || brushnum >= cm.numBrushes) continue;
        cbrush_t *brush = &cm.brushes[brushnum];

        /* Test point against all brush sides */
        qboolean inside = qtrue;
        for (int j = 0; j < brush->numSides && inside; j++) {
            cplane_t *plane = brush->sides[j].plane;
            float d;
            if (plane->type < 3)
                d = p[plane->type] - plane->dist;
            else
                d = DotProduct(p, plane->normal) - plane->dist;

            if (d > 0) inside = qfalse;
        }

        if (inside)
            contents |= brush->contentFlags;
    }

    return contents;
}

int CM_PointContents(const vec3_t p, clipHandle_t model) {
    if (!cm.loaded) return 0;

    if (model == BOX_MODEL_HANDLE) {
        /* Test against temp box */
        if (p[0] < box_model.mins[0] || p[0] > box_model.maxs[0] ||
            p[1] < box_model.mins[1] || p[1] > box_model.maxs[1] ||
            p[2] < box_model.mins[2] || p[2] > box_model.maxs[2])
            return 0;
        return box_brush.contentFlags;
    }

    int leafnum = CM_PointLeafnum(p);
    return CM_PointContentsLeaf(p, leafnum);
}

int CM_TransformedPointContents(const vec3_t p, clipHandle_t model,
                                const vec3_t origin, const vec3_t angles) {
    /* Transform point into model space */
    vec3_t local;
    VectorSubtract(p, origin, local);

    /* TODO: Handle rotation via angles (for rotated brush entities) */
    (void)angles;

    return CM_PointContents(local, model);
}

/* =========================================================================
 * CM_BoxTrace -- trace a box through the BSP
 *
 * This is the core collision detection function. Traces a moving AABB
 * from start to end and returns the first solid surface hit.
 * ========================================================================= */

static void CM_TraceThroughLeaf(trace_t *tw, int leafnum,
                                const vec3_t start, const vec3_t end,
                                const vec3_t mins, const vec3_t maxs,
                                int brushmask) {
    if (leafnum < 0 || leafnum >= cm.numLeafs) return;

    cleaf_t *leaf = &cm.leafs[leafnum];

    for (int i = 0; i < leaf->numLeafBrushes; i++) {
        int brushnum = cm.leafBrushes[leaf->firstLeafBrush + i];
        if (brushnum < 0 || brushnum >= cm.numBrushes) continue;

        cbrush_t *brush = &cm.brushes[brushnum];
        if (!(brush->contentFlags & brushmask)) continue;
        if (brush->checkcount == cm.checkcount) continue;
        brush->checkcount = cm.checkcount;

        /* Test trace against brush sides */
        float enterFrac = -1.0f;
        float leaveFrac = 1.0f;
        qboolean startOut = qfalse;
        qboolean getOut = qfalse;
        cbrushside_t *hitSide = NULL;

        for (int j = 0; j < brush->numSides; j++) {
            cplane_t *plane = brush->sides[j].plane;

            float d1, d2;
            if (plane->type < 3) {
                d1 = start[plane->type] - plane->dist;
                d2 = end[plane->type] - plane->dist;
                /* Expand plane by box extent on this axis */
                if (plane->normal[plane->type] > 0) {
                    d1 -= maxs[plane->type];
                    d2 -= maxs[plane->type];
                } else {
                    d1 += mins[plane->type];
                    d2 += mins[plane->type];
                }
            } else {
                /* Non-axial plane */
                float offset = 0;
                for (int k = 0; k < 3; k++) {
                    if (plane->normal[k] < 0)
                        offset += mins[k] * plane->normal[k];
                    else
                        offset += maxs[k] * plane->normal[k];
                }
                d1 = DotProduct(start, plane->normal) - (plane->dist + offset);
                d2 = DotProduct(end, plane->normal) - (plane->dist + offset);
            }

            if (d1 > 0) startOut = qtrue;
            if (d2 > 0) getOut = qtrue;

            /* If completely in front of this plane, not inside brush */
            if (d1 > 0 && d2 >= d1) goto nextBrush;

            /* If completely behind this plane, check next side */
            if (d1 <= 0 && d2 <= 0) continue;

            /* Crossing the plane */
            if (d1 > d2) {
                float f = (d1 - 0.03125f) / (d1 - d2);
                if (f < 0) f = 0;
                if (f > enterFrac) {
                    enterFrac = f;
                    hitSide = &brush->sides[j];
                }
            } else {
                float f = (d1 + 0.03125f) / (d1 - d2);
                if (f > 1.0f) f = 1.0f;
                if (f < leaveFrac) leaveFrac = f;
            }
        }

        if (!startOut) {
            /* Started inside this brush */
            tw->startsolid = qtrue;
            if (!getOut) {
                tw->allsolid = qtrue;
                tw->fraction = 0;
                tw->contents = brush->contentFlags;
            }
            continue;
        }

        if (enterFrac < leaveFrac && enterFrac >= 0 && enterFrac < tw->fraction) {
            tw->fraction = enterFrac;
            tw->plane.normal[0] = hitSide->plane->normal[0];
            tw->plane.normal[1] = hitSide->plane->normal[1];
            tw->plane.normal[2] = hitSide->plane->normal[2];
            tw->plane.dist = hitSide->plane->dist;
            tw->surfaceFlags = hitSide->surfaceFlags;
            tw->contents = brush->contentFlags;
        }

        nextBrush:;
    }
}

static void CM_TraceThroughTree(trace_t *tw, int num,
                                float p1f, float p2f,
                                const vec3_t p1, const vec3_t p2,
                                const vec3_t mins, const vec3_t maxs,
                                int brushmask) {
    if (tw->fraction <= p1f) return;     /* already hit something nearer */

    if (num < 0) {
        /* Leaf node */
        CM_TraceThroughLeaf(tw, -(num + 1), p1, p2, mins, maxs, brushmask);
        return;
    }

    cnode_t *node = &cm.nodes[num];
    cplane_t *plane = &cm.planes[node->planeNum];

    float d1, d2, offset;

    if (plane->type < 3) {
        d1 = p1[plane->type] - plane->dist;
        d2 = p2[plane->type] - plane->dist;
        offset = fabsf(maxs[plane->type]) > fabsf(mins[plane->type])
               ? fabsf(maxs[plane->type]) : fabsf(mins[plane->type]);
    } else {
        d1 = DotProduct(p1, plane->normal) - plane->dist;
        d2 = DotProduct(p2, plane->normal) - plane->dist;
        offset = 0;
        for (int i = 0; i < 3; i++) {
            float v = fabsf(mins[i]) > fabsf(maxs[i]) ? fabsf(mins[i]) : fabsf(maxs[i]);
            offset += v * fabsf(plane->normal[i]);
        }
    }

    if (d1 >= offset + 1 && d2 >= offset + 1) {
        CM_TraceThroughTree(tw, node->children[0], p1f, p2f, p1, p2, mins, maxs, brushmask);
        return;
    }
    if (d1 < -offset - 1 && d2 < -offset - 1) {
        CM_TraceThroughTree(tw, node->children[1], p1f, p2f, p1, p2, mins, maxs, brushmask);
        return;
    }

    /* Both sides */
    int side;
    float frac, frac2;

    if (d1 < d2) {
        float idist = 1.0f / (d1 - d2);
        side = 1;
        frac = (d1 + offset + 0.03125f) * idist;
        frac2 = (d1 - offset - 0.03125f) * idist;
    } else if (d1 > d2) {
        float idist = 1.0f / (d1 - d2);
        side = 0;
        frac = (d1 - offset - 0.03125f) * idist;
        frac2 = (d1 + offset + 0.03125f) * idist;
    } else {
        side = 0;
        frac = 1.0f;
        frac2 = 0.0f;
    }

    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    if (frac2 < 0) frac2 = 0;
    if (frac2 > 1) frac2 = 1;

    float midf = p1f + (p2f - p1f) * frac;
    vec3_t mid;
    for (int i = 0; i < 3; i++) mid[i] = p1[i] + frac * (p2[i] - p1[i]);

    CM_TraceThroughTree(tw, node->children[side], p1f, midf, p1, mid, mins, maxs, brushmask);

    midf = p1f + (p2f - p1f) * frac2;
    for (int i = 0; i < 3; i++) mid[i] = p1[i] + frac2 * (p2[i] - p1[i]);

    CM_TraceThroughTree(tw, node->children[side ^ 1], midf, p2f, mid, p2, mins, maxs, brushmask);
}

void CM_BoxTrace(trace_t *results, const vec3_t start, const vec3_t end,
                 const vec3_t mins, const vec3_t maxs, clipHandle_t model,
                 int brushmask, qboolean cylinder) {
    (void)cylinder; /* TODO: cylinder trace for FAKK2 character collision */

    memset(results, 0, sizeof(*results));
    results->fraction = 1.0f;
    results->entityNum = -1;

    if (!cm.loaded && model != BOX_MODEL_HANDLE) return;

    cm.checkcount++;

    /* Use zero-size box if mins/maxs are NULL */
    vec3_t tmins, tmaxs;
    if (mins) { VectorCopy(mins, tmins); } else { VectorClear(tmins); }
    if (maxs) { VectorCopy(maxs, tmaxs); } else { VectorClear(tmaxs); }

    if (model == BOX_MODEL_HANDLE) {
        /* Trace against temp box model */
        CM_TraceThroughLeaf(results, 0, start, end, tmins, tmaxs, brushmask);
    } else if (model > 0 && model < cm.numSubModels) {
        /* Trace against submodel -- treat as single leaf */
        cmodel_t *cmod = &cm.subModels[model];
        cleaf_t tempLeaf;
        tempLeaf.firstLeafBrush = cmod->firstBrush;
        tempLeaf.numLeafBrushes = cmod->numBrushes;

        /* Temporarily add the leaf */
        int savedLeafs = cm.numLeafs;
        cm.leafs[0] = tempLeaf;
        CM_TraceThroughLeaf(results, 0, start, end, tmins, tmaxs, brushmask);
        cm.numLeafs = savedLeafs;
    } else {
        /* Trace against world BSP tree */
        CM_TraceThroughTree(results, 0, 0, 1, start, end, tmins, tmaxs, brushmask);
    }

    /* Compute endpos */
    if (results->fraction == 1.0f) {
        VectorCopy(end, results->endpos);
    } else {
        for (int i = 0; i < 3; i++) {
            results->endpos[i] = start[i] + results->fraction * (end[i] - start[i]);
        }
    }
}

void CM_TransformedBoxTrace(trace_t *results, const vec3_t start, const vec3_t end,
                            const vec3_t mins, const vec3_t maxs, clipHandle_t model,
                            int brushmask, const vec3_t origin, const vec3_t angles,
                            qboolean cylinder) {
    /* Transform into model space */
    vec3_t localStart, localEnd;
    VectorSubtract(start, origin, localStart);
    VectorSubtract(end, origin, localEnd);

    /* TODO: Handle rotation via angles (for rotated brush entities) */
    (void)angles;

    CM_BoxTrace(results, localStart, localEnd, mins, maxs, model, brushmask, cylinder);

    /* Transform result back to world space */
    VectorAdd(results->endpos, origin, results->endpos);
}

/* =========================================================================
 * PVS queries
 * ========================================================================= */

qboolean CM_InPVS(vec3_t p1, vec3_t p2) {
    if (!cm.visibility) return qtrue;

    int leaf1 = CM_PointLeafnum(p1);
    int leaf2 = CM_PointLeafnum(p2);
    int cluster1 = CM_LeafCluster(leaf1);
    int cluster2 = CM_LeafCluster(leaf2);

    if (cluster1 < 0 || cluster2 < 0) return qfalse;

    const byte *vis = cm.visibility + cluster1 * cm.clusterBytes;
    return (vis[cluster2 >> 3] & (1 << (cluster2 & 7))) != 0;
}

qboolean CM_InPVSIgnorePortals(vec3_t p1, vec3_t p2) {
    /* Same as CM_InPVS for now -- area portal handling is TODO */
    return CM_InPVS(p1, p2);
}

/* =========================================================================
 * Area portals
 * ========================================================================= */

void CM_AdjustAreaPortalState(int area1, int area2, qboolean open) {
    if (area1 < 0 || area1 >= MAX_CM_AREAS) return;
    if (area2 < 0 || area2 >= MAX_CM_AREAS) return;
    cm.areaPortals[area1][area2] = open;
    cm.areaPortals[area2][area1] = open;
}

qboolean CM_AreasConnected(int area1, int area2) {
    if (area1 < 0 || area1 >= MAX_CM_AREAS) return qfalse;
    if (area2 < 0 || area2 >= MAX_CM_AREAS) return qfalse;
    if (area1 == area2) return qtrue;
    return cm.areaPortals[area1][area2];
}

/* =========================================================================
 * Mark fragments (for decals/bullet impacts)
 * ========================================================================= */

int CM_MarkFragments(int numPoints, const vec3_t *points, const vec3_t projection,
                     int maxPoints, vec3_t pointBuffer, int maxFragments,
                     markFragment_t *fragmentBuffer) {
    /* TODO: Clip projection polygon against BSP surfaces */
    (void)numPoints; (void)points; (void)projection;
    (void)maxPoints; (void)pointBuffer;
    (void)maxFragments; (void)fragmentBuffer;
    return 0;
}
