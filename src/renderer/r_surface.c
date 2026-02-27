/*
 * r_surface.c -- Surface rendering
 *
 * Handles rendering of BSP surfaces, patches, and meshes.
 * Each surface has a type (from mapSurfaceType_t in qfiles.h):
 *   MST_PLANAR         -- flat polygon surface
 *   MST_PATCH           -- Bezier patch (Q3-style)
 *   MST_TRIANGLE_SOUP   -- arbitrary triangle mesh
 *   MST_FLARE           -- lens flare sprite
 *   MST_TERRAIN          -- FAKK2 terrain surface
 *
 * Surface material types defined by UberTools additions affect
 * gameplay (footstep sounds, bullet impacts, particle effects):
 *   SURF_PAPER, SURF_WOOD, SURF_ROCK, SURF_DIRT, SURF_GRILL,
 *   SURF_GRASS, SURF_MUD, SURF_PUDDLE, SURF_GLASS, SURF_GRAVEL, SURF_SAND
 *
 * Rendering pipeline per surface:
 *   1. Frustum cull check
 *   2. Determine surface shader
 *   3. For each shader stage:
 *      a. Set texture, blend mode, alpha test
 *      b. Generate texture coordinates (tcGen)
 *      c. Generate vertex colors (rgbGen, alphaGen)
 *      d. Submit vertices/indices to GPU
 */

#include "../common/qcommon.h"
#include "../common/qfiles.h"
#include "tr_types.h"
#include "r_gl.h"
#include <string.h>
#include <math.h>

/* =========================================================================
 * Surface batching
 *
 * For efficiency, surfaces using the same shader are batched together
 * to minimize GL state changes. The sort key combines:
 *   - Shader index (primary sort)
 *   - Fog index
 *   - Entity number
 *   - Distance (for transparent surfaces)
 * ========================================================================= */

#define MAX_SORT_SURFACES   65536

typedef struct {
    int         sortKey;        /* packed sort value */
    int         surfaceIndex;   /* index into BSP surface array */
    int         entityNum;      /* -1 for world */
    float       distance;       /* distance from camera (for alpha sort) */
} sortSurface_t;

static sortSurface_t    sortSurfaces[MAX_SORT_SURFACES];
static int              numSortSurfaces;

/* =========================================================================
 * Surface sort key construction
 * ========================================================================= */

static int R_MakeSortKey(int shaderIndex, int fogIndex, int entityNum) {
    return (shaderIndex << 16) | (fogIndex << 12) | (entityNum & 0xFFF);
}

/* =========================================================================
 * Planar surface rendering
 * ========================================================================= */

void R_DrawPlanarSurface(const dsurface_t *surf, const drawVert_t *verts,
                          const int *indexes) {
    if (!surf || surf->numVerts == 0 || surf->numIndexes == 0) return;

    const drawVert_t *surfVerts = verts + surf->firstVert;
    const int *surfIndexes = indexes + surf->firstIndex;

    /* Submit triangles using immediate mode (fixed-function pipeline).
     * This matches the original FAKK2 renderer's approach.
     * A future optimization would batch into VBOs. */
    glBegin(GL_TRIANGLES);
    for (int i = 0; i < surf->numIndexes; i++) {
        const drawVert_t *v = &surfVerts[surfIndexes[i]];
        glColor4ubv(v->color);
        glTexCoord2fv(v->st);
        glNormal3fv(v->normal);
        glVertex3fv(v->xyz);
    }
    glEnd();
}

/* =========================================================================
 * Patch (Bezier) surface tessellation and rendering
 *
 * Q3/FAKK2 patches are defined by control point grids.
 * They're tessellated into triangle meshes at load time based on
 * the subdivision level (r_subdivisions cvar).
 * ========================================================================= */

typedef struct {
    int         numVerts;
    int         numIndexes;
    drawVert_t  *verts;
    int         *indexes;
    float       lodRadius;
} patchSurface_t;

void R_DrawPatchSurface(const dsurface_t *surf, const drawVert_t *verts,
                         const int *indexes) {
    if (!surf || surf->numVerts == 0) return;

    /* Patches use patchWidth x patchHeight control points.
     * Full Bezier tessellation requires generating a subdivided mesh.
     * For now, render the raw control point triangles if indexes exist. */
    if (surf->numIndexes > 0) {
        R_DrawPlanarSurface(surf, verts, indexes);
    }
}

/* =========================================================================
 * Triangle soup rendering (arbitrary mesh, used for misc_model)
 * ========================================================================= */

void R_DrawTriangleSoup(const dsurface_t *surf, const drawVert_t *verts,
                         const int *indexes) {
    if (!surf || surf->numVerts == 0) return;

    /* Same as planar but no assumption about planarity.
     * Uses the same vertex/index submission path. */
    R_DrawPlanarSurface(surf, verts, indexes);
}

/* =========================================================================
 * Terrain surface rendering (FAKK2 addition)
 *
 * Terrain surfaces use dynamic LOD based on distance from the camera.
 * The subdivision level is stored per-surface in dsurface_t.subdivisions.
 * ========================================================================= */

void R_DrawTerrainSurface(const dsurface_t *surf, const drawVert_t *verts,
                            const int *indexes) {
    if (!surf || surf->numVerts == 0) return;

    /* TODO: Implement terrain LOD and rendering
     * Terrain uses a heightfield-like approach with adaptive subdivision. */
    R_DrawPlanarSurface(surf, verts, indexes);
}

/* =========================================================================
 * Flare rendering
 *
 * Lens flare sprites attached to light sources.
 * FAKK2 uses these extensively for torches and magical effects.
 * ========================================================================= */

void R_DrawFlare(const dsurface_t *surf) {
    if (!surf) return;

    /* TODO: Render screen-space lens flare
     * 1. Project flare origin to screen space
     * 2. Check visibility (occlusion query or trace)
     * 3. Draw blended quad at screen position
     * 4. Scale based on distance */
    (void)surf;
}

/* =========================================================================
 * World surface rendering dispatch
 *
 * Called from R_RenderScene to draw all visible BSP surfaces.
 * Surfaces are sorted by shader to minimize state changes.
 * ========================================================================= */

/* =========================================================================
 * BSP world data accessors (from r_bsp.c)
 * ========================================================================= */

typedef struct {
    char        name[MAX_QPATH];

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

    int         numModels;
    dmodel_t    *models;

    int         numSurfaces;
    dsurface_t  *surfaces;

    int         numVerts;
    drawVert_t  *verts;

    int         numIndexes;
    int         *indexes;

    int         numLightmaps;
    byte        *lightmapData;

    dvis_t      *vis;
    int         visLen;

    qboolean    loaded;
} bspWorldRef_t;

extern qboolean R_WorldLoaded(void);

/* Get BSP world data pointers from r_bsp.c */
static const bspWorldRef_t *R_GetWorldData(void) {
    /* Access via extern -- the actual struct in r_bsp.c has more fields,
     * but the layout of these first fields matches */
    extern void *R_GetBSPWorldPtr(void);
    return (const bspWorldRef_t *)R_GetBSPWorldPtr();
}

/* =========================================================================
 * Frustum culling (simplified box test)
 * ========================================================================= */

static float r_frustumPlanes[6][4];  /* 6 planes: left, right, top, bottom, near, far */
static int r_numFrustumPlanes;

void R_SetupFrustum(const refdef_t *fd) {
    /* Extract view-projection frustum planes for culling.
     * For simplicity, we use the vieworg + viewaxis + fov to build planes. */
    (void)fd;
    r_numFrustumPlanes = 0;  /* disable frustum culling for now -- draw everything */
}

static qboolean R_CullBox(const int *mins, const int *maxs) {
    /* When frustum culling is disabled, nothing is culled */
    if (r_numFrustumPlanes == 0) return qfalse;
    (void)mins; (void)maxs;
    return qfalse;
}

/* =========================================================================
 * World surface rendering
 * ========================================================================= */

void R_DrawWorldSurfaces(void) {
    if (!R_WorldLoaded()) return;

    const bspWorldRef_t *world = R_GetWorldData();
    if (!world || !world->loaded) return;

    numSortSurfaces = 0;

    /* Walk all surfaces in the BSP and render based on type.
     * A full implementation would use BSP tree + PVS for visibility culling.
     * For now, iterate all surfaces of model 0 (worldspawn). */
    if (!world->models || world->numModels < 1) return;

    const dmodel_t *worldModel = &world->models[0];
    int firstSurf = worldModel->firstSurface;
    int numSurfs = worldModel->numSurfaces;

    /* Set default rendering state for world */
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_2D);

    /* Default white color for unlit surfaces */
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    for (int i = 0; i < numSurfs; i++) {
        int surfIdx = firstSurf + i;
        if (surfIdx < 0 || surfIdx >= world->numSurfaces) continue;

        const dsurface_t *surf = &world->surfaces[surfIdx];

        /* Skip nodraw surfaces */
        if (surf->shaderNum >= 0 && surf->shaderNum < world->numShaders) {
            if (world->shaders[surf->shaderNum].surfaceFlags & SURF_NODRAW) {
                continue;
            }
        }

        /* Dispatch by surface type */
        switch (surf->surfaceType) {
            case MST_PLANAR:
                R_DrawPlanarSurface(surf, world->verts, world->indexes);
                break;
            case MST_PATCH:
                R_DrawPatchSurface(surf, world->verts, world->indexes);
                break;
            case MST_TRIANGLE_SOUP:
                R_DrawTriangleSoup(surf, world->verts, world->indexes);
                break;
            case MST_TERRAIN:
                R_DrawTerrainSurface(surf, world->verts, world->indexes);
                break;
            case MST_FLARE:
                R_DrawFlare(surf);
                break;
            default:
                break;
        }
    }
}

/* =========================================================================
 * Entity surface rendering
 *
 * Draws TIKI skeletal models and brush entity models.
 * ========================================================================= */

void R_DrawEntitySurfaces(refEntity_t *entities, int numEntities) {
    const bspWorldRef_t *world = R_GetWorldData();

    for (int i = 0; i < numEntities; i++) {
        refEntity_t *ent = &entities[i];
        if (!ent->hModel) continue;

        /* Save GL matrix state */
        glPushMatrix();

        /* Apply entity transform */
        glTranslatef(ent->origin[0], ent->origin[1], ent->origin[2]);

        float scale = ent->scale > 0.0f ? ent->scale : 1.0f;
        if (scale != 1.0f) {
            glScalef(scale, scale, scale);
        }

        /* Apply entity rotation if axis is non-zero */
        if (ent->axis[0][0] != 0.0f || ent->axis[1][1] != 0.0f || ent->axis[2][2] != 0.0f) {
            float mat[16];
            mat[0]  = ent->axis[0][0]; mat[4]  = ent->axis[1][0]; mat[8]  = ent->axis[2][0]; mat[12] = 0;
            mat[1]  = ent->axis[0][1]; mat[5]  = ent->axis[1][1]; mat[9]  = ent->axis[2][1]; mat[13] = 0;
            mat[2]  = ent->axis[0][2]; mat[6]  = ent->axis[1][2]; mat[10] = ent->axis[2][2]; mat[14] = 0;
            mat[3]  = 0;              mat[7]  = 0;              mat[11] = 0;              mat[15] = 1;
            glMultMatrixf(mat);
        }

        /* Check if this is a BSP inline model (brush entity) */
        int modelIndex = ent->hModel;
        if (world && world->loaded && modelIndex > 0 && modelIndex < world->numModels) {
            /* Brush entity -- draw its surfaces from BSP */
            const dmodel_t *model = &world->models[modelIndex];
            glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

            for (int j = 0; j < model->numSurfaces; j++) {
                int surfIdx = model->firstSurface + j;
                if (surfIdx < 0 || surfIdx >= world->numSurfaces) continue;

                const dsurface_t *surf = &world->surfaces[surfIdx];
                R_DrawPlanarSurface(surf, world->verts, world->indexes);
            }
        }
        /* TIKI model rendering would go here once skeleton animation is complete */

        glPopMatrix();
    }
}

/* =========================================================================
 * Mark (decal) surface rendering
 *
 * Marks are projected onto world surfaces for bullet impacts,
 * blood splatter, scorch marks, etc.
 * ========================================================================= */

typedef struct {
    int     numPoints;
    vec3_t  points[64];
    float   texcoords[64][2];
    int     shaderHandle;
    byte    color[4];
    float   fadeTime;
} markSurface_t;

#define MAX_MARKS   256
static markSurface_t    r_marks[MAX_MARKS];
static int              r_numMarks;

void R_AddMark(int shaderHandle, int numPoints, const vec3_t *points,
               const float *texcoords, const byte *color) {
    if (r_numMarks >= MAX_MARKS) return;

    markSurface_t *mark = &r_marks[r_numMarks++];
    mark->shaderHandle = shaderHandle;
    mark->numPoints = numPoints > 64 ? 64 : numPoints;
    memcpy(mark->color, color, 4);

    for (int i = 0; i < mark->numPoints; i++) {
        VectorCopy(points[i], mark->points[i]);
        mark->texcoords[i][0] = texcoords[i * 2];
        mark->texcoords[i][1] = texcoords[i * 2 + 1];
    }
}

void R_DrawMarks(void) {
    if (r_numMarks == 0) return;

    /* Marks are rendered as alpha-blended polygons on world surfaces */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);

    for (int i = 0; i < r_numMarks; i++) {
        markSurface_t *mark = &r_marks[i];
        if (mark->numPoints < 3) continue;

        glColor4ubv(mark->color);

        glBegin(GL_TRIANGLE_FAN);
        for (int j = 0; j < mark->numPoints; j++) {
            glTexCoord2fv(mark->texcoords[j]);
            glVertex3fv(mark->points[j]);
        }
        glEnd();
    }

    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
}

void R_ClearMarks(void) {
    r_numMarks = 0;
}

/* =========================================================================
 * Initialization
 * ========================================================================= */

void R_InitSurfaces(void) {
    numSortSurfaces = 0;
    r_numMarks = 0;
    Com_Printf("Surface renderer initialized\n");
}

void R_ShutdownSurfaces(void) {
    numSortSurfaces = 0;
    r_numMarks = 0;
}
