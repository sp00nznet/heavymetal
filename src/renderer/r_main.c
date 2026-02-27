/*
 * r_main.c -- Renderer core
 *
 * FAKK2's renderer is a modified id Tech 3 renderer with:
 *   - TIKI skeletal model rendering (replacing Q3's vertex animation)
 *   - Enhanced shader system (lens flares, sky portals)
 *   - True dynamic lighting
 *   - Ghost particle rendering integration
 *   - LOD support for TIKI models
 *   - Weapon trail "swipe" effects
 *   - Mark (decal) system for bullet impacts and blood
 *
 * The original dynamically loads OpenGL via LoadLibraryA/GetProcAddress.
 * The recomp uses SDL2's GL loader for portability.
 *
 * OpenGL was the only rendering path -- no Direct3D support in original.
 */

#include "../common/qcommon.h"
#include "tr_types.h"
#include <string.h>
#include <math.h>

static glconfig_t glconfig;

/* =========================================================================
 * Renderer initialization
 * ========================================================================= */

static void R_InitGL(void) {
    Com_Printf("--- R_InitGL ---\n");
    /* TODO: Load OpenGL functions via GLimp_GetProcAddress */

    memset(&glconfig, 0, sizeof(glconfig));
    Q_strncpyz(glconfig.renderer_string, "OpenGL 4.x (recomp)", sizeof(glconfig.renderer_string));
    Q_strncpyz(glconfig.vendor_string, "FAKK2 Recomp", sizeof(glconfig.vendor_string));
    Q_strncpyz(glconfig.version_string, "4.6", sizeof(glconfig.version_string));
    glconfig.maxTextureSize = 4096;
    glconfig.maxActiveTextures = 32;
    glconfig.colorBits = 32;
    glconfig.depthBits = 24;
    glconfig.stencilBits = 8;
    glconfig.vidWidth = 1280;
    glconfig.vidHeight = 720;
    glconfig.windowAspect = 16.0f / 9.0f;

    Com_Printf("Renderer: OpenGL initialized\n");
}

void R_Init(void) {
    Com_Printf("--- R_Init ---\n");
    R_InitGL();
}

void R_Shutdown(void) {
    Com_Printf("Renderer shutdown\n");
}

/* =========================================================================
 * Frame management
 * ========================================================================= */

void R_BeginFrame(void) {
    /* TODO: Clear buffers, set viewport */
}

void R_EndFrame(void) {
    /* TODO: Swap buffers */
}

/* =========================================================================
 * Scene management
 * ========================================================================= */

#define MAX_SCENE_ENTITIES  4096
#define MAX_SCENE_POLYS     4096
#define MAX_SCENE_LIGHTS    256

static refEntity_t  scene_entities[MAX_SCENE_ENTITIES];
static int          scene_numEntities;

void R_ClearScene(void) {
    scene_numEntities = 0;
}

void R_AddRefEntityToScene(refEntity_t *ent) {
    if (!ent) return;
    if (scene_numEntities >= MAX_SCENE_ENTITIES) return;
    scene_entities[scene_numEntities++] = *ent;
}

void R_AddRefSpriteToScene(refEntity_t *ent) {
    /* Sprites use the same entity pipeline with a special render path */
    R_AddRefEntityToScene(ent);
}

void R_AddLightToScene(vec3_t origin, float intensity, float r, float g, float b, int type) {
    (void)origin; (void)intensity; (void)r; (void)g; (void)b; (void)type;
    /* TODO: Add dynamic light to scene */
}

void R_AddPolyToScene(qhandle_t hShader, int numVerts, const polyVert_t *verts, int renderfx) {
    (void)hShader; (void)numVerts; (void)verts; (void)renderfx;
    /* TODO: Add polygon to scene (used for marks/decals and effects) */
}

void R_RenderScene(const refdef_t *fd) {
    (void)fd;
    /* TODO: Full scene render:
     *   1. Setup view matrix from fd->vieworg/viewaxis
     *   2. Cull BSP (PVS + frustum)
     *   3. Draw world surfaces
     *   4. Draw entities (TIKI models, brush models)
     *   5. Draw Ghost particles
     *   6. Draw sky portal
     *   7. Post-process (screen blend)
     */
}

refEntity_t *R_GetRenderEntity(int entityNumber) {
    if (entityNumber < 0 || entityNumber >= scene_numEntities) return NULL;
    return &scene_entities[entityNumber];
}

/* =========================================================================
 * Model registration and queries
 * ========================================================================= */

void R_BeginRegistration(void) {
    /* Called at level start */
    Com_DPrintf("R_BeginRegistration\n");
}

void R_EndRegistration(void) {
    /* Called after all models are precached */
    Com_DPrintf("R_EndRegistration\n");
}

qhandle_t R_RegisterModel(const char *name) {
    Com_DPrintf("R_RegisterModel: %s (stub)\n", name);
    /* TODO: Load model and return handle */
    return 0;
}

qhandle_t R_RegisterShader(const char *name) {
    Com_DPrintf("R_RegisterShader: %s (stub)\n", name);
    return 0;
}

qhandle_t R_RegisterShaderNoMip(const char *name) {
    Com_DPrintf("R_RegisterShaderNoMip: %s (stub)\n", name);
    return 0;
}

qhandle_t R_RegisterSkin(const char *name) {
    Com_DPrintf("R_RegisterSkin: %s (stub)\n", name);
    return 0;
}

void R_LoadWorldMap(const char *mapname) {
    Com_DPrintf("R_LoadWorldMap: %s (stub)\n", mapname);
    R_LoadBSP(mapname);
}

void R_ModelBounds(clipHandle_t model, vec3_t mins, vec3_t maxs) {
    (void)model;
    VectorSet(mins, -16, -16, 0);
    VectorSet(maxs, 16, 16, 72);
}

float R_ModelRadius(clipHandle_t model) {
    (void)model;
    return 36.0f;
}

/* =========================================================================
 * 2D drawing (HUD, menus)
 * ========================================================================= */

void R_SetColor(const float *rgba) {
    (void)rgba;
    /* TODO: Set current drawing color */
}

void R_DrawStretchPic(float x, float y, float w, float h,
                      float s1, float t1, float s2, float t2,
                      qhandle_t hShader) {
    (void)x; (void)y; (void)w; (void)h;
    (void)s1; (void)t1; (void)s2; (void)t2;
    (void)hShader;
    /* TODO: Draw a textured quad in 2D screen space */
}

void R_DrawBox(float x, float y, float w, float h) {
    (void)x; (void)y; (void)w; (void)h;
    /* TODO: Draw solid-colored box */
}

/* =========================================================================
 * Shader queries
 * ========================================================================= */

int R_GetShaderWidth(qhandle_t shader) {
    (void)shader;
    return 256; /* default */
}

int R_GetShaderHeight(qhandle_t shader) {
    (void)shader;
    return 256; /* default */
}

/* =========================================================================
 * Debug rendering
 * ========================================================================= */

void R_DebugLine(vec3_t start, vec3_t end, float r, float g, float b, float alpha) {
    (void)start; (void)end; (void)r; (void)g; (void)b; (void)alpha;
    /* TODO: Draw debug line for development visualization */
}

/* =========================================================================
 * Weapon trail swipe effects
 *
 * FAKK2's melee weapons (swords, etc.) leave visible trail effects.
 * The swipe system collects edge points over time and renders
 * them as a fading ribbon.
 * ========================================================================= */

static qboolean swipe_active = qfalse;

void R_SwipeBegin(float thistime, float life, qhandle_t shader) {
    (void)thistime; (void)life; (void)shader;
    swipe_active = qtrue;
}

void R_SwipePoint(vec3_t p1, vec3_t p2, float time) {
    (void)p1; (void)p2; (void)time;
    /* TODO: Add swipe edge pair */
}

void R_SwipeEnd(void) {
    swipe_active = qfalse;
    /* TODO: Finalize and submit swipe for rendering */
}

/* =========================================================================
 * Noise function (procedural effects)
 * ========================================================================= */

float R_Noise(float x, float y, float z, float t) {
    /* Simple pseudo-noise for shader effects */
    float val = sinf(x * 1.3f + t) * cosf(y * 0.9f + t * 0.7f)
              + sinf(z * 1.7f + t * 1.1f);
    return val * 0.5f;
}

/* =========================================================================
 * GL config query (called by cgame)
 * ========================================================================= */

void R_GetGlconfig(glconfig_t *config) {
    if (config) *config = glconfig;
}

/* =========================================================================
 * Camera offset (FAKK2 third-person camera system)
 * ========================================================================= */

static float camera_offset_data[3] = { 0, 0, 0 };
static qboolean camera_lookactive = qfalse;
static qboolean camera_resetview = qfalse;

float *R_GetCameraOffset(qboolean *lookactive, qboolean *resetview) {
    if (lookactive) *lookactive = camera_lookactive;
    if (resetview) *resetview = camera_resetview;
    return camera_offset_data;
}
