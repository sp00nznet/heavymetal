/*
 * r_sky.c -- Sky rendering and sky portals
 *
 * FAKK2's enhanced sky system supports:
 *   - Standard Q3 skybox rendering (6-sided cube of textures)
 *   - Sky portals (render a separate scene visible through sky surfaces)
 *   - Alpha-blended sky layers (cloud planes)
 *   - Per-level sky configuration via skyparms shader directive
 *
 * Sky portals are used in levels like the swamp and creature garden
 * to show distant scenery that's actually a separate mini-scene
 * rendered from a different viewpoint.
 *
 * Skybox texture naming convention:
 *   textures/skies/<name>_rt  (right, +X)
 *   textures/skies/<name>_lf  (left, -X)
 *   textures/skies/<name>_up  (up, +Z)
 *   textures/skies/<name>_dn  (down, -Z)
 *   textures/skies/<name>_bk  (back, +Y)
 *   textures/skies/<name>_ft  (front, -Y)
 */

#include "../common/qcommon.h"
#include "tr_types.h"
#include <string.h>
#include <math.h>

/* =========================================================================
 * Sky state
 * ========================================================================= */

typedef struct {
    qboolean    loaded;
    char        images[6][MAX_QPATH];   /* skybox face textures */
    float       cloudHeight;
    qboolean    hasCloudLayer;

    /* Sky portal */
    qboolean    portalActive;
    vec3_t      portalOrigin;
    vec3_t      portalAxis[3];
    float       portalAlpha;
} skyState_t;

static skyState_t r_sky;

/* =========================================================================
 * Skybox geometry
 *
 * The skybox is rendered as a unit cube centered on the camera.
 * Depth writing is disabled so it always appears behind everything.
 * ========================================================================= */

/* Skybox face vertices (unit cube) */
static const float skyVerts[6][4][3] = {
    /* Right (+X) */
    {{ 1, -1,  1}, { 1, -1, -1}, { 1,  1, -1}, { 1,  1,  1}},
    /* Left (-X) */
    {{-1,  1,  1}, {-1,  1, -1}, {-1, -1, -1}, {-1, -1,  1}},
    /* Up (+Z) */
    {{-1,  1,  1}, { 1,  1,  1}, { 1,  1, -1}, {-1,  1, -1}},
    /* Down (-Z) */
    {{-1, -1, -1}, { 1, -1, -1}, { 1, -1,  1}, {-1, -1,  1}},
    /* Back (+Y) */
    {{ 1,  1,  1}, { 1,  1, -1}, {-1,  1, -1}, {-1,  1,  1}},
    /* Front (-Y) */
    {{-1, -1,  1}, {-1, -1, -1}, { 1, -1, -1}, { 1, -1,  1}}
};

/* =========================================================================
 * Sky configuration
 * ========================================================================= */

void R_SetSkyBox(const char *basename) {
    if (!basename || !basename[0]) {
        r_sky.loaded = qfalse;
        return;
    }

    static const char *suffixes[6] = { "_rt", "_lf", "_up", "_dn", "_bk", "_ft" };

    for (int i = 0; i < 6; i++) {
        snprintf(r_sky.images[i], MAX_QPATH, "%s%s", basename, suffixes[i]);
        /* TODO: Load texture via R_FindShader or direct TGA/JPG loading */
    }

    r_sky.loaded = qtrue;
    Com_Printf("Skybox loaded: %s\n", basename);
}

void R_SetSkyCloudHeight(float height) {
    r_sky.cloudHeight = height;
    r_sky.hasCloudLayer = (height > 0.0f);
}

/* =========================================================================
 * Sky portal
 *
 * A sky portal renders a separate scene visible through sky surfaces.
 * The portal origin and axis define where the "camera" is placed in the
 * portal world. The portal alpha controls transparency blending.
 * ========================================================================= */

void R_SetSkyPortal(qboolean active, const vec3_t origin,
                     const vec3_t axis[3], float alpha) {
    r_sky.portalActive = active;
    if (active && origin) {
        VectorCopy(origin, r_sky.portalOrigin);
        if (axis) {
            for (int i = 0; i < 3; i++)
                VectorCopy(axis[i], r_sky.portalAxis[i]);
        }
        r_sky.portalAlpha = alpha;
    }
}

/* =========================================================================
 * Sky rendering
 * ========================================================================= */

void R_DrawSky(const vec3_t viewOrigin) {
    if (!r_sky.loaded) return;

    /* TODO: Full skybox rendering:
     *
     * 1. Disable depth writing
     * 2. Translate to camera position (sky moves with camera)
     * 3. For each of the 6 faces:
     *    a. Bind face texture
     *    b. Draw quad from skyVerts[face]
     * 4. Re-enable depth writing
     *
     * If sky portal is active:
     *   1. Save current view state
     *   2. Set view to portal origin/axis
     *   3. Render the portal scene
     *   4. Restore view state
     *   5. Composite with alpha blending
     *
     * If cloud layer is active:
     *   1. Draw alpha-blended cloud plane at cloudHeight
     *   2. Scroll texture based on time
     */
    (void)viewOrigin;
    (void)skyVerts;
}

/* =========================================================================
 * Sky surface detection
 * ========================================================================= */

qboolean R_IsSkyShader(int shaderIndex) {
    /* Check if the shader has the sky flag set.
     * Called during BSP rendering to identify sky surfaces. */
    extern void *R_GetShaderByHandle(int h);
    /* For now, return false; will be wired when shader system is connected */
    (void)shaderIndex;
    return qfalse;
}

/* =========================================================================
 * Initialization
 * ========================================================================= */

void R_InitSky(void) {
    memset(&r_sky, 0, sizeof(r_sky));
    Com_Printf("Sky renderer initialized\n");
}

void R_ShutdownSky(void) {
    memset(&r_sky, 0, sizeof(r_sky));
}
