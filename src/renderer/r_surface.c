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
#include "../tiki/tiki.h"
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

/* =========================================================================
 * Bezier patch tessellation
 *
 * BSP patches store a grid of control points (patchWidth x patchHeight).
 * The grid is divided into 3x3 sub-patches, each evaluated as a
 * biquadratic Bezier surface. The tessellation level determines how
 * many intermediate vertices are generated between control points.
 * ========================================================================= */

#define PATCH_TESS_LEVEL 4  /* subdivisions per sub-patch edge */

static float BezierBasis2(int idx, float t) {
    /* Quadratic Bezier basis functions */
    switch (idx) {
        case 0: return (1-t)*(1-t);
        case 1: return 2*t*(1-t);
        case 2: return t*t;
    }
    return 0;
}

void R_DrawPatchSurface(const dsurface_t *surf, const drawVert_t *verts,
                         const int *indexes) {
    if (!surf || surf->numVerts == 0) return;
    (void)indexes; /* patches use control point grid, not explicit indices */

    int patchW = surf->patchWidth;
    int patchH = surf->patchHeight;
    if (patchW < 3 || patchH < 3) return;
    if ((patchW & 1) == 0 || (patchH & 1) == 0) return; /* must be odd */

    const drawVert_t *cp = &verts[surf->firstVert];
    int numSubPatchesX = (patchW - 1) / 2;
    int numSubPatchesY = (patchH - 1) / 2;

    int tessW = PATCH_TESS_LEVEL + 1;  /* verts per sub-patch edge */

    for (int py = 0; py < numSubPatchesY; py++) {
        for (int px = 0; px < numSubPatchesX; px++) {
            /* 3x3 control points for this sub-patch */
            int baseX = px * 2;
            int baseY = py * 2;

            drawVert_t ctrl[3][3];
            for (int cy = 0; cy < 3; cy++) {
                for (int cx = 0; cx < 3; cx++) {
                    int idx = (baseY + cy) * patchW + (baseX + cx);
                    ctrl[cy][cx] = cp[idx];
                }
            }

            /* Tessellate: evaluate biquadratic Bezier at grid points */
            drawVert_t tessVerts[9][9]; /* max (PATCH_TESS_LEVEL+1)^2 */
            for (int ty = 0; ty < tessW; ty++) {
                float v = (float)ty / (float)PATCH_TESS_LEVEL;
                float bv[3] = { BezierBasis2(0,v), BezierBasis2(1,v), BezierBasis2(2,v) };

                for (int tx = 0; tx < tessW; tx++) {
                    float u = (float)tx / (float)PATCH_TESS_LEVEL;
                    float bu[3] = { BezierBasis2(0,u), BezierBasis2(1,u), BezierBasis2(2,u) };

                    drawVert_t *out = &tessVerts[ty][tx];
                    VectorClear(out->xyz);
                    VectorClear(out->normal);
                    out->st[0] = out->st[1] = 0;
                    out->lightmap[0] = out->lightmap[1] = 0;
                    for (int k = 0; k < 4; k++) out->color[k] = 0;

                    float totalColor[4] = {0,0,0,0};

                    for (int cy = 0; cy < 3; cy++) {
                        for (int cx = 0; cx < 3; cx++) {
                            float w = bu[cx] * bv[cy];
                            const drawVert_t *c = &ctrl[cy][cx];

                            out->xyz[0]    += w * c->xyz[0];
                            out->xyz[1]    += w * c->xyz[1];
                            out->xyz[2]    += w * c->xyz[2];
                            out->normal[0] += w * c->normal[0];
                            out->normal[1] += w * c->normal[1];
                            out->normal[2] += w * c->normal[2];
                            out->st[0]     += w * c->st[0];
                            out->st[1]     += w * c->st[1];
                            out->lightmap[0] += w * c->lightmap[0];
                            out->lightmap[1] += w * c->lightmap[1];
                            totalColor[0]  += w * c->color[0];
                            totalColor[1]  += w * c->color[1];
                            totalColor[2]  += w * c->color[2];
                            totalColor[3]  += w * c->color[3];
                        }
                    }

                    VectorNormalize(out->normal);
                    for (int k = 0; k < 4; k++) {
                        int cv = (int)totalColor[k];
                        out->color[k] = (cv > 255) ? 255 : (cv < 0) ? 0 : (byte)cv;
                    }
                }
            }

            /* Render tessellated grid as triangle strip rows */
            for (int ty = 0; ty < PATCH_TESS_LEVEL; ty++) {
                glBegin(GL_TRIANGLE_STRIP);
                for (int tx = 0; tx <= PATCH_TESS_LEVEL; tx++) {
                    drawVert_t *v0 = &tessVerts[ty][tx];
                    drawVert_t *v1 = &tessVerts[ty+1][tx];

                    glTexCoord2fv(v0->st);
                    glColor4ubv(v0->color);
                    glNormal3fv(v0->normal);
                    glVertex3fv(v0->xyz);

                    glTexCoord2fv(v1->st);
                    glColor4ubv(v1->color);
                    glNormal3fv(v1->normal);
                    glVertex3fv(v1->xyz);
                }
                glEnd();
            }
        }
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
    if (!surf || surf->numVerts == 0 || surf->numIndexes == 0) return;

    /* FAKK2 terrain surfaces store a subdivision level per-surface.
     * For distance-based LOD, we could skip every other index pair at
     * far distances. For now, render the full mesh -- the BSP tree
     * frustum culling already prevents rendering of off-screen terrain.
     * A proper implementation would use the patchWidth/patchHeight grid
     * with adaptive tessellation, but the indexed triangle approach
     * matches what the engine does after initial subdivision. */
    const drawVert_t *surfVerts = verts + surf->firstVert;
    const int *surfIndexes = indexes + surf->firstIndex;

    /* Use vertex colors for terrain blending (grass/rock/dirt transitions) */
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
 * Flare rendering
 *
 * Lens flare sprites attached to light sources.
 * FAKK2 uses these extensively for torches and magical effects.
 * ========================================================================= */

void R_DrawFlare(const dsurface_t *surf) {
    if (!surf) return;

    /* Get flare origin from the lightmapOrigin field */
    const vec3_t origin = {
        surf->lightmapOrigin[0], surf->lightmapOrigin[1], surf->lightmapOrigin[2]
    };

    /* Project world position to screen using GL matrices */
    GLdouble model[16], proj[16];
    GLint viewport[4];
    glGetDoublev(GL_MODELVIEW_MATRIX, model);
    glGetDoublev(GL_PROJECTION_MATRIX, proj);
    glGetIntegerv(GL_VIEWPORT, viewport);

    /* Manual world-to-screen projection (avoids GLU dependency) */
    GLdouble sx, sy, sz;
    {
        /* Transform by modelview */
        GLdouble ex = model[0]*origin[0] + model[4]*origin[1] + model[8]*origin[2] + model[12];
        GLdouble ey = model[1]*origin[0] + model[5]*origin[1] + model[9]*origin[2] + model[13];
        GLdouble ez = model[2]*origin[0] + model[6]*origin[1] + model[10]*origin[2] + model[14];
        GLdouble ew = model[3]*origin[0] + model[7]*origin[1] + model[11]*origin[2] + model[15];
        /* Transform by projection */
        GLdouble cx = proj[0]*ex + proj[4]*ey + proj[8]*ez + proj[12]*ew;
        GLdouble cy = proj[1]*ex + proj[5]*ey + proj[9]*ez + proj[13]*ew;
        GLdouble cz = proj[2]*ex + proj[6]*ey + proj[10]*ez + proj[14]*ew;
        GLdouble cw = proj[3]*ex + proj[7]*ey + proj[11]*ez + proj[15]*ew;
        if (cw == 0.0) return;
        /* Perspective divide and viewport transform */
        GLdouble ndcx = cx / cw;
        GLdouble ndcy = cy / cw;
        GLdouble ndcz = cz / cw;
        sx = viewport[0] + (1.0 + ndcx) * viewport[2] * 0.5;
        sy = viewport[1] + (1.0 + ndcy) * viewport[3] * 0.5;
        sz = (1.0 + ndcz) * 0.5;
    }

    /* Check if on screen and in front of camera (sz in [0,1]) */
    if (sz < 0.0 || sz > 1.0) return;
    if (sx < viewport[0] || sx > viewport[0] + viewport[2]) return;
    if (sy < viewport[1] || sy > viewport[1] + viewport[3]) return;

    /* Basic occlusion: read depth at the projected pixel.
     * If the flare is behind geometry, don't draw it. */
    GLfloat depth;
    glReadPixels((int)sx, (int)sy, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
    if (sz > (double)depth + 0.001) return;  /* occluded */

    /* Calculate flare size based on distance -- closer = bigger */
    extern void *R_GetBSPWorldPtr(void);
    float flareSize = 32.0f / (float)(sz * 20.0 + 0.5);
    if (flareSize < 2.0f) flareSize = 2.0f;
    if (flareSize > 128.0f) flareSize = 128.0f;

    /* Switch to 2D mode to draw the flare quad */
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(viewport[0], viewport[0] + viewport[2],
            viewport[1], viewport[1] + viewport[3], -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);  /* additive blending for flares */
    glDisable(GL_TEXTURE_2D);

    /* Draw a bright additive quad */
    float fx = (float)sx;
    float fy = (float)sy;
    float hs = flareSize * 0.5f;

    glColor4f(1.0f, 0.9f, 0.7f, 0.6f);
    glBegin(GL_QUADS);
    glVertex2f(fx - hs, fy - hs);
    glVertex2f(fx + hs, fy - hs);
    glVertex2f(fx + hs, fy + hs);
    glVertex2f(fx - hs, fy + hs);
    glEnd();

    /* Restore state */
    glEnable(GL_TEXTURE_2D);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
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

/* Frustum planes: normal[3] + dist. Point is in front if dot(p, normal) >= dist. */
static float r_frustumPlanes[5][4];  /* left, right, bottom, top, near */
static int r_numFrustumPlanes;

void R_SetupFrustum(const refdef_t *fd) {
    if (!fd) { r_numFrustumPlanes = 0; return; }

    float fovX = fd->fov_x > 0 ? fd->fov_x : 90.0f;
    float fovY = fd->fov_y > 0 ? fd->fov_y : 73.74f;

    /* Build frustum planes from view parameters.
     * Each plane faces inward -- points inside the frustum are on the positive side. */
    vec3_t forward, right, up;
    VectorCopy(fd->viewaxis[1], forward);  /* Q3: axis[1] is forward in viewaxis */
    VectorCopy(fd->viewaxis[0], right);
    VectorCopy(fd->viewaxis[2], up);

    float angX = DEG2RAD(fovX * 0.5f);
    float angY = DEG2RAD(fovY * 0.5f);
    float cosX = cosf(angX), sinX = sinf(angX);
    float cosY = cosf(angY), sinY = sinf(angY);

    /* Left plane: rotated from forward towards -right */
    r_frustumPlanes[0][0] = forward[0] * sinX + right[0] * cosX;
    r_frustumPlanes[0][1] = forward[1] * sinX + right[1] * cosX;
    r_frustumPlanes[0][2] = forward[2] * sinX + right[2] * cosX;
    r_frustumPlanes[0][3] = DotProduct(fd->vieworg, r_frustumPlanes[0]);

    /* Right plane: rotated from forward towards +right */
    r_frustumPlanes[1][0] = forward[0] * sinX - right[0] * cosX;
    r_frustumPlanes[1][1] = forward[1] * sinX - right[1] * cosX;
    r_frustumPlanes[1][2] = forward[2] * sinX - right[2] * cosX;
    r_frustumPlanes[1][3] = DotProduct(fd->vieworg, r_frustumPlanes[1]);

    /* Bottom plane */
    r_frustumPlanes[2][0] = forward[0] * sinY + up[0] * cosY;
    r_frustumPlanes[2][1] = forward[1] * sinY + up[1] * cosY;
    r_frustumPlanes[2][2] = forward[2] * sinY + up[2] * cosY;
    r_frustumPlanes[2][3] = DotProduct(fd->vieworg, r_frustumPlanes[2]);

    /* Top plane */
    r_frustumPlanes[3][0] = forward[0] * sinY - up[0] * cosY;
    r_frustumPlanes[3][1] = forward[1] * sinY - up[1] * cosY;
    r_frustumPlanes[3][2] = forward[2] * sinY - up[2] * cosY;
    r_frustumPlanes[3][3] = DotProduct(fd->vieworg, r_frustumPlanes[3]);

    /* Near plane (small offset from vieworg along forward) */
    r_frustumPlanes[4][0] = forward[0];
    r_frustumPlanes[4][1] = forward[1];
    r_frustumPlanes[4][2] = forward[2];
    r_frustumPlanes[4][3] = DotProduct(fd->vieworg, forward) + 4.0f;

    r_numFrustumPlanes = 5;
}

/* Test AABB against frustum. Returns qtrue if the box is completely outside. */
static qboolean R_CullBox(const int *mins, const int *maxs) {
    if (r_numFrustumPlanes == 0) return qfalse;

    for (int i = 0; i < r_numFrustumPlanes; i++) {
        const float *p = r_frustumPlanes[i];

        /* Find the corner of the AABB farthest in the direction of the plane normal */
        float testX = (p[0] >= 0) ? (float)maxs[0] : (float)mins[0];
        float testY = (p[1] >= 0) ? (float)maxs[1] : (float)mins[1];
        float testZ = (p[2] >= 0) ? (float)maxs[2] : (float)mins[2];

        float d = p[0] * testX + p[1] * testY + p[2] * testZ;
        if (d < p[3]) return qtrue;  /* box is fully behind this plane */
    }
    return qfalse;
}

/* =========================================================================
 * World surface rendering
 * ========================================================================= */

/* Mark visible surfaces by walking the BSP tree with frustum culling */
static byte *r_surfaceVisible;
static int   r_surfaceVisibleSize;

static void R_MarkLeafSurfaces(const bspWorldRef_t *world, int leafIdx) {
    if (leafIdx < 0 || leafIdx >= world->numLeafs) return;
    const dleaf_t *leaf = &world->leafs[leafIdx];

    for (int i = 0; i < leaf->numLeafSurfaces; i++) {
        int idx = leaf->firstLeafSurface + i;
        if (idx < 0 || idx >= world->numLeafSurfaces) continue;
        int surfIdx = world->leafSurfaces[idx];
        if (surfIdx >= 0 && surfIdx < world->numSurfaces)
            r_surfaceVisible[surfIdx] = 1;
    }
}

static void R_RecursiveWorldNode(const bspWorldRef_t *world, int nodeNum) {
    if (nodeNum < 0) {
        /* Leaf node */
        R_MarkLeafSurfaces(world, -(nodeNum + 1));
        return;
    }

    if (nodeNum >= world->numNodes) return;
    const dnode_t *node = &world->nodes[nodeNum];

    /* Frustum cull this node's bounding box */
    if (R_CullBox(node->mins, node->maxs)) return;

    /* Recurse into both children */
    R_RecursiveWorldNode(world, node->children[0]);
    R_RecursiveWorldNode(world, node->children[1]);
}

void R_DrawWorldSurfaces(void) {
    if (!R_WorldLoaded()) return;

    const bspWorldRef_t *world = R_GetWorldData();
    if (!world || !world->loaded) return;

    numSortSurfaces = 0;

    if (!world->models || world->numModels < 1) return;

    /* Use BSP tree walking with frustum culling when nodes are available */
    qboolean useBSPWalk = (world->nodes && world->numNodes > 0 &&
                            world->leafSurfaces && world->numLeafSurfaces > 0 &&
                            r_numFrustumPlanes > 0);

    /* Ensure visibility buffer is allocated */
    if (useBSPWalk) {
        if (r_surfaceVisibleSize < world->numSurfaces) {
            if (r_surfaceVisible) Z_Free(r_surfaceVisible);
            r_surfaceVisibleSize = world->numSurfaces;
            r_surfaceVisible = (byte *)Z_TagMalloc(r_surfaceVisibleSize, TAG_RENDERER);
        }
        memset(r_surfaceVisible, 0, world->numSurfaces);
        R_RecursiveWorldNode(world, 0);
    }

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

        /* Skip surfaces not visible from frustum culling */
        if (useBSPWalk && surfIdx < r_surfaceVisibleSize && !r_surfaceVisible[surfIdx])
            continue;

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

/* =========================================================================
 * TIKI skeletal model rendering
 *
 * Renders TIKI models by skinning skeleton mesh vertices using the
 * current animation frame's bone transforms. The vertex skinning loop:
 *   1. Builds world-space bone matrix palette from animation data
 *   2. For each skeleton surface, iterates variable-sized vertices
 *   3. Accumulates weighted bone transforms per vertex
 *   4. Submits skinned positions + normals + texcoords to GL
 * ========================================================================= */

#define MAX_SKINNED_VERTS 4096

static void R_DrawTikiModel(refEntity_t *ent) {
    /* Get TIKI model data */
    extern tiki_model_t *TIKI_GetModel(dtiki_t handle);
    tiki_model_t *tikiModel = TIKI_GetModel(ent->tiki);
    if (!tikiModel || !tikiModel->skelmodel[0]) return;

    /* Build bone matrix palette from animation frame */
    extern int TIKI_BuildBoneMatrices(const char *, const char *, int,
                                       float [][3][4], int);
    float boneMatrices[100][3][4]; /* TIKI_SKEL_MAXBONES */

    const char *animFile = NULL;
    int frame = ent->frame;
    if (tikiModel->num_anims > 0 && tikiModel->anims[0].filename[0]) {
        animFile = tikiModel->anims[0].filename;
    }

    int numBones = TIKI_BuildBoneMatrices(tikiModel->skelmodel, animFile,
                                           frame, boneMatrices, 100);
    if (numBones <= 0) return;

    /* Get skeleton surface data */
    extern const byte *TIKI_GetSkelSurfaceData(const char *, int *);
    int numSurfaces;
    const byte *surfData = TIKI_GetSkelSurfaceData(tikiModel->skelmodel, &numSurfaces);
    if (!surfData) return;

    /* Temp arrays for skinned vertex output */
    static vec3_t skinPositions[MAX_SKINNED_VERTS];
    static vec3_t skinNormals[MAX_SKINNED_VERTS];
    static vec2_t skinTexCoords[MAX_SKINNED_VERTS];

    glColor4ub(ent->shaderRGBA[0], ent->shaderRGBA[1],
               ent->shaderRGBA[2], ent->shaderRGBA[3]);

    /* Iterate skeleton surfaces */
    const skelSurface_t *surf = (const skelSurface_t *)surfData;
    for (int s = 0; s < numSurfaces && surf; s++) {
        int numTris = surf->numTriangles;
        int numVerts = surf->numVerts;
        if (numTris <= 0 || numVerts <= 0) goto nextSurf;
        if (numVerts > MAX_SKINNED_VERTS) numVerts = MAX_SKINNED_VERTS;

        /* Get triangle and vertex data */
        const skelTriangle_t *tris =
            (const skelTriangle_t *)((const byte *)surf + surf->ofsTriangles);
        const byte *vPtr = (const byte *)surf + surf->ofsVerts;

        /* Skin each vertex */
        for (int v = 0; v < numVerts; v++) {
            const skelVertex_t *sv = (const skelVertex_t *)vPtr;
            int nw = sv->numWeights;

            VectorClear(skinPositions[v]);
            VectorCopy(sv->normal, skinNormals[v]);
            skinTexCoords[v][0] = sv->texCoords[0];
            skinTexCoords[v][1] = sv->texCoords[1];

            /* Accumulate weighted bone transforms */
            for (int w = 0; w < nw; w++) {
                int bi = sv->weights[w].boneIndex;
                float bw = sv->weights[w].boneWeight;
                if (bi < 0 || bi >= numBones) continue;

                const float *o = sv->weights[w].offset;
                float *m0 = boneMatrices[bi][0];
                float *m1 = boneMatrices[bi][1];
                float *m2 = boneMatrices[bi][2];

                skinPositions[v][0] += bw * (m0[0]*o[0] + m0[1]*o[1] + m0[2]*o[2] + m0[3]);
                skinPositions[v][1] += bw * (m1[0]*o[0] + m1[1]*o[1] + m1[2]*o[2] + m1[3]);
                skinPositions[v][2] += bw * (m2[0]*o[0] + m2[1]*o[1] + m2[2]*o[2] + m2[3]);
            }

            /* Advance to next variable-sized vertex */
            vPtr += sizeof(vec3_t) + sizeof(vec2_t) + sizeof(int)
                  + nw * sizeof(skelWeight_t);
        }

        /* Render triangles */
        glBegin(GL_TRIANGLES);
        for (int t = 0; t < numTris; t++) {
            for (int vi = 0; vi < 3; vi++) {
                int idx = tris[t].indexes[vi];
                if (idx < 0 || idx >= numVerts) continue;
                glTexCoord2fv(skinTexCoords[idx]);
                glNormal3fv(skinNormals[idx]);
                glVertex3fv(skinPositions[idx]);
            }
        }
        glEnd();

    nextSurf:;
        int ofsEnd = surf->ofsEnd;
        if (ofsEnd <= 0) break;
        surf = (const skelSurface_t *)((const byte *)surf + ofsEnd);
    }
}

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
        } else if (ent->tiki >= 0) {
            /* TIKI skeletal model */
            R_DrawTikiModel(ent);
        }

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
