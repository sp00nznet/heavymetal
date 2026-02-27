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
#include "r_gl.h"
#include <string.h>
#include <math.h>

static glconfig_t glconfig;

/* Expose for r_gl.c */
glconfig_t *R_GetGlconfigPtr(void) { return &glconfig; }

/* =========================================================================
 * Renderer initialization
 * ========================================================================= */

static void R_InitGL(void) {
    Com_Printf("--- R_InitGL ---\n");

    /* Load GL extension functions */
    R_LoadGLFunctions();

    /* Query GL info */
    const char *glVendor = (const char *)glGetString(GL_VENDOR);
    const char *glRenderer = (const char *)glGetString(GL_RENDERER);
    const char *glVersion = (const char *)glGetString(GL_VERSION);

    memset(&glconfig, 0, sizeof(glconfig));
    Q_strncpyz(glconfig.renderer_string, glRenderer ? glRenderer : "Unknown",
               sizeof(glconfig.renderer_string));
    Q_strncpyz(glconfig.vendor_string, glVendor ? glVendor : "Unknown",
               sizeof(glconfig.vendor_string));
    Q_strncpyz(glconfig.version_string, glVersion ? glVersion : "Unknown",
               sizeof(glconfig.version_string));

    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &glconfig.maxTextureSize);

    glconfig.maxActiveTextures = 32;
    glconfig.colorBits = 32;
    glconfig.depthBits = 24;
    glconfig.stencilBits = 8;
    glconfig.vidWidth = 1280;
    glconfig.vidHeight = 720;
    glconfig.windowAspect = 16.0f / 9.0f;

    Com_Printf("GL Vendor:   %s\n", glconfig.vendor_string);
    Com_Printf("GL Renderer: %s\n", glconfig.renderer_string);
    Com_Printf("GL Version:  %s\n", glconfig.version_string);
    Com_Printf("Max texture: %d\n", glconfig.maxTextureSize);

    /* Set initial GL state */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);  /* Q3 convention: front-face culling */
    glEnable(GL_TEXTURE_2D);

    Com_Printf("Renderer: OpenGL initialized\n");
}

/* Forward declarations from renderer subsystems */
extern void R_InitShaders(void);
extern void R_ShutdownShaders(void);
extern void R_InitModels(void);
extern void R_ShutdownModels(void);
extern void R_InitSurfaces(void);
extern void R_ShutdownSurfaces(void);
extern void R_InitLighting(void);
extern void R_ShutdownLighting(void);
extern void R_InitSky(void);
extern void R_ShutdownSky(void);

void R_Init(void) {
    Com_Printf("--- R_Init ---\n");
    R_InitGL();
    R_InitShaders();
    R_InitModels();
    R_InitSurfaces();
    R_InitLighting();
    R_InitSky();
}

void R_Shutdown(void) {
    Com_Printf("--- R_Shutdown ---\n");
    R_ShutdownSky();
    R_ShutdownLighting();
    R_ShutdownSurfaces();
    R_ShutdownModels();
    R_ShutdownShaders();
    GLimp_Shutdown();
}

/* =========================================================================
 * Frame management
 * ========================================================================= */

void R_BeginFrame(void) {
    glViewport(0, 0, glconfig.vidWidth, glconfig.vidHeight);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

void R_EndFrame(void) {
    /* Swap buffers (implemented in sys_sdl.c) */
    extern void Win_SwapBuffers(void);
    Win_SwapBuffers();
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

/* R_AddLightToScene is in r_light.c */

void R_AddPolyToScene(qhandle_t hShader, int numVerts, const polyVert_t *verts, int renderfx) {
    (void)hShader; (void)numVerts; (void)verts; (void)renderfx;
    /* TODO: Add polygon to scene (used for marks/decals and effects) */
}

void R_RenderScene(const refdef_t *fd) {
    if (!fd) return;

    /* Setup 3D projection */
    glViewport(fd->x, glconfig.vidHeight - (fd->y + fd->height),
               fd->width, fd->height);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    /* Perspective from field of view */
    float fovX = fd->fov_x;
    float fovY = fd->fov_y;
    if (fovX <= 0.0f) fovX = 90.0f;
    if (fovY <= 0.0f) fovY = 73.74f; /* ~90 fovx at 4:3 */

    float zNear = 4.0f;
    float zFar = 8192.0f;  /* FAKK2 default far plane */
    float ymax = zNear * tanf(fovY * 3.14159f / 360.0f);
    float xmax = zNear * tanf(fovX * 3.14159f / 360.0f);
    glFrustum(-xmax, xmax, -ymax, ymax, zNear, zFar);

    /* Setup view matrix from vieworg/viewaxis */
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    /* Q3/FAKK2 coordinate system: +X right, +Y forward, +Z up
     * OpenGL: +X right, +Y up, -Z forward
     * Apply rotation then translation */
    float viewMatrix[16];
    viewMatrix[0]  = fd->viewaxis[0][0];
    viewMatrix[4]  = fd->viewaxis[0][1];
    viewMatrix[8]  = fd->viewaxis[0][2];
    viewMatrix[12] = 0;

    viewMatrix[1]  = fd->viewaxis[2][0];
    viewMatrix[5]  = fd->viewaxis[2][1];
    viewMatrix[9]  = fd->viewaxis[2][2];
    viewMatrix[13] = 0;

    viewMatrix[2]  = -fd->viewaxis[1][0];
    viewMatrix[6]  = -fd->viewaxis[1][1];
    viewMatrix[10] = -fd->viewaxis[1][2];
    viewMatrix[14] = 0;

    viewMatrix[3]  = 0;
    viewMatrix[7]  = 0;
    viewMatrix[11] = 0;
    viewMatrix[15] = 1;

    glMultMatrixf(viewMatrix);
    glTranslatef(-fd->vieworg[0], -fd->vieworg[1], -fd->vieworg[2]);

    /* Enable 3D state */
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    /* Draw sky */
    extern void R_DrawSky(const vec3_t viewOrigin);
    R_DrawSky(fd->vieworg);

    /* Draw world surfaces */
    extern void R_DrawWorldSurfaces(void);
    R_DrawWorldSurfaces();

    /* Draw entities */
    extern void R_DrawEntitySurfaces(refEntity_t *entities, int numEntities);
    R_DrawEntitySurfaces(scene_entities, scene_numEntities);

    /* Draw marks/decals */
    extern void R_DrawMarks(void);
    R_DrawMarks();

    /* Apply screen blend (damage flash, underwater tint, etc.) */
    if (fd->blend[3] > 0.0f) {
        extern void R_Set2D(void);
        R_Set2D();
        extern void R_DrawFillRect(float x, float y, float w, float h,
                                   float r, float g, float b, float a);
        R_DrawFillRect(0, 0, (float)glconfig.vidWidth, (float)glconfig.vidHeight,
                       fd->blend[0], fd->blend[1], fd->blend[2], fd->blend[3]);
    }
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

/* R_RegisterModel is now in r_model.c */
/* R_RegisterSkin is now in r_model.c */

/* Shader registration wrappers -- delegates to r_shader.c */
extern qhandle_t R_FindShader(const char *name);

qhandle_t R_RegisterShader(const char *name) {
    return R_FindShader(name);
}

qhandle_t R_RegisterShaderNoMip(const char *name) {
    /* NoMip shaders skip mipmap generation (used for 2D UI elements) */
    return R_FindShader(name);
}

void R_LoadWorldMap(const char *mapname) {
    extern qboolean R_LoadBSP(const char *name);
    Com_DPrintf("R_LoadWorldMap: %s\n", mapname);
    R_LoadBSP(mapname);
}

/* R_ModelBounds and R_ModelRadius are now in r_model.c */

/* =========================================================================
 * 2D drawing (HUD, menus)
 * ========================================================================= */

static float r_currentColor[4] = { 1, 1, 1, 1 };

void R_SetColor(const float *rgba) {
    if (rgba) {
        r_currentColor[0] = rgba[0];
        r_currentColor[1] = rgba[1];
        r_currentColor[2] = rgba[2];
        r_currentColor[3] = rgba[3];
    } else {
        r_currentColor[0] = r_currentColor[1] = r_currentColor[2] = r_currentColor[3] = 1.0f;
    }
    glColor4fv(r_currentColor);
}

void R_DrawStretchPic(float x, float y, float w, float h,
                      float s1, float t1, float s2, float t2,
                      qhandle_t hShader) {
    extern void R_Set2D(void);
    R_Set2D();

    /* TODO: Bind shader texture via hShader */
    (void)hShader;

    glColor4fv(r_currentColor);
    glBegin(GL_QUADS);
    glTexCoord2f(s1, t1); glVertex2f(x, y);
    glTexCoord2f(s2, t1); glVertex2f(x + w, y);
    glTexCoord2f(s2, t2); glVertex2f(x + w, y + h);
    glTexCoord2f(s1, t2); glVertex2f(x, y + h);
    glEnd();
}

void R_DrawBox(float x, float y, float w, float h) {
    extern void R_Set2D(void);
    R_Set2D();

    glDisable(GL_TEXTURE_2D);
    glColor4fv(r_currentColor);
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();
    glEnable(GL_TEXTURE_2D);
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
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glColor4f(r, g, b, alpha);
    glBegin(GL_LINES);
    glVertex3fv(start);
    glVertex3fv(end);
    glEnd();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
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
