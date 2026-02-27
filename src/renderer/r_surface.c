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
#include <string.h>

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
    if (!surf || surf->numVerts == 0) return;

    /* TODO: Submit vertices and indices to OpenGL
     *
     * The actual GL calls will look like:
     *   glBindVertexArray(worldVAO)
     *   glDrawElements(GL_TRIANGLES, surf->numIndexes, GL_UNSIGNED_INT,
     *                  offset_to_surface_indices)
     *
     * For now, this is a no-op until the GL backend is wired up.
     */
    (void)verts;
    (void)indexes;
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
     * Tessellation creates a subdivided mesh.
     * TODO: Implement Bezier subdivision and rendering. */
    (void)verts;
    (void)indexes;
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

void R_DrawWorldSurfaces(void) {
    extern qboolean R_WorldLoaded(void);
    if (!R_WorldLoaded()) return;

    numSortSurfaces = 0;

    /* TODO: Full world rendering pipeline:
     *
     * 1. Walk BSP tree with frustum and PVS culling
     * 2. Collect visible surfaces into sortSurfaces[]
     * 3. Sort by shader (opaque first, then transparent by distance)
     * 4. For each shader batch:
     *    a. Bind shader state
     *    b. For each stage:
     *       - Set texture
     *       - Set blend mode
     *       - Set texture coordinate generation
     *       - Set color generation
     *    c. Draw all surfaces in batch
     *
     * The BSP tree walk uses the visibility data (PVS) loaded
     * from LUMP_VISIBILITY to quickly reject entire leaf clusters
     * that are known to be invisible from the current camera position.
     */
}

/* =========================================================================
 * Entity surface rendering
 *
 * Draws TIKI skeletal models and brush entity models.
 * ========================================================================= */

void R_DrawEntitySurfaces(refEntity_t *entities, int numEntities) {
    for (int i = 0; i < numEntities; i++) {
        refEntity_t *ent = &entities[i];
        if (!ent->hModel) continue;

        /* TODO: Based on model type:
         * MOD_TIKI: Animate skeleton, skin mesh, draw surfaces
         * MOD_BRUSH: Draw BSP inline model surfaces
         * MOD_SPRITE: Draw screen-aligned quad
         */
        (void)ent;
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
    /* TODO: Draw all active marks as textured polygons on world surfaces */
    (void)r_marks;
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
