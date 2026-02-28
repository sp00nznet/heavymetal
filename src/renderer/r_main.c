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

    extern void R_InitFont(void);
    R_InitFont();
}

void R_Shutdown(void) {
    Com_Printf("--- R_Shutdown ---\n");

    extern void R_ShutdownFont(void);
    R_ShutdownFont();

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
#define MAX_SCENE_POLY_VERTS 16384
#define MAX_SCENE_LIGHTS    256

static refEntity_t  scene_entities[MAX_SCENE_ENTITIES];
static int          scene_numEntities;

/* Polygon batches for effects/decals submitted via R_AddPolyToScene */
typedef struct {
    qhandle_t   shader;
    int         firstVert;
    int         numVerts;
    int         renderfx;
} scenePoly_t;

static scenePoly_t  scene_polys[MAX_SCENE_POLYS];
static int          scene_numPolys;
static polyVert_t   scene_polyVerts[MAX_SCENE_POLY_VERTS];
static int          scene_numPolyVerts;

void R_ClearScene(void) {
    scene_numEntities = 0;
    scene_numPolys = 0;
    scene_numPolyVerts = 0;
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
    if (!verts || numVerts < 3) return;
    if (scene_numPolys >= MAX_SCENE_POLYS) return;
    if (scene_numPolyVerts + numVerts > MAX_SCENE_POLY_VERTS) return;

    scenePoly_t *poly = &scene_polys[scene_numPolys++];
    poly->shader = hShader;
    poly->firstVert = scene_numPolyVerts;
    poly->numVerts = numVerts;
    poly->renderfx = renderfx;

    memcpy(&scene_polyVerts[scene_numPolyVerts], verts,
           numVerts * sizeof(polyVert_t));
    scene_numPolyVerts += numVerts;
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

    /* Setup frustum culling planes */
    extern void R_SetupFrustum(const refdef_t *fd);
    R_SetupFrustum(fd);

    /* Draw sky */
    extern void R_DrawSky(const vec3_t viewOrigin);
    R_DrawSky(fd->vieworg);

    /* Draw world surfaces */
    extern void R_DrawWorldSurfaces(void);
    R_DrawWorldSurfaces();

    /* Draw entities */
    extern void R_DrawEntitySurfaces(refEntity_t *entities, int numEntities);
    R_DrawEntitySurfaces(scene_entities, scene_numEntities);

    /* Draw scene polygons (effects, decals submitted via R_AddPolyToScene) */
    if (scene_numPolys > 0) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);

        for (int pi = 0; pi < scene_numPolys; pi++) {
            scenePoly_t *poly = &scene_polys[pi];
            /* TODO: Bind poly->shader texture when shader system is complete */

            glBegin(GL_TRIANGLE_FAN);
            for (int vi = 0; vi < poly->numVerts; vi++) {
                polyVert_t *v = &scene_polyVerts[poly->firstVert + vi];
                glColor4ubv(v->modulate);
                glTexCoord2fv(v->st);
                glVertex3fv(v->xyz);
            }
            glEnd();
        }

        glDepthMask(GL_TRUE);
        glEnable(GL_CULL_FACE);
    }

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

    /* Bind shader's first-stage texture if available */
    if (hShader > 0) {
        extern void *R_GetShaderByHandle(int h);
        /* The shader has a GL texture in stages[0].image if loaded.
         * For now, we can get the image handle from the shader system. */
        typedef struct {
            char        name[MAX_QPATH];
            int         index;
            int         sortOrder;
            qboolean    defaultShader;
            qboolean    isSky;
            qboolean    isPortal;
            int         cullType;
            qboolean    polygonOffset;
            int         surfaceFlags;
            int         contentFlags;
            int         numStages;
            struct {
                qboolean    active;
                qhandle_t   image;
            } stages[8];
        } shaderRef_t;
        shaderRef_t *sh = (shaderRef_t *)R_GetShaderByHandle(hShader);
        if (sh && sh->numStages > 0 && sh->stages[0].image > 0) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, sh->stages[0].image);
        }
    }

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

/* =========================================================================
 * Weapon swipe implementation
 *
 * The swipe system collects pairs of edge points (p1, p2) submitted
 * over successive frames. These form a ribbon that fades over its
 * lifetime. Each pair defines a cross-section of the trail.
 *
 * Rendering: Construct a triangle strip from consecutive edge pairs.
 * Alpha fades from 1.0 at the newest point to 0.0 at the oldest.
 * ========================================================================= */

#define MAX_SWIPE_POINTS    32

typedef struct {
    vec3_t  p1;         /* top edge */
    vec3_t  p2;         /* bottom edge */
    float   time;       /* timestamp when submitted */
} swipePoint_t;

static struct {
    qboolean        active;
    float           startTime;
    float           life;       /* seconds until full fade */
    qhandle_t       shader;
    swipePoint_t    points[MAX_SWIPE_POINTS];
    int             numPoints;
} swipe;

void R_SwipeBegin(float thistime, float life, qhandle_t shader) {
    swipe.active = qtrue;
    swipe.startTime = thistime;
    swipe.life = life > 0.0f ? life : 0.5f;
    swipe.shader = shader;
    swipe.numPoints = 0;
}

void R_SwipePoint(vec3_t p1, vec3_t p2, float time) {
    if (!swipe.active) return;
    if (swipe.numPoints >= MAX_SWIPE_POINTS) return;

    swipePoint_t *sp = &swipe.points[swipe.numPoints++];
    VectorCopy(p1, sp->p1);
    VectorCopy(p2, sp->p2);
    sp->time = time;
}

void R_SwipeEnd(void) {
    if (!swipe.active || swipe.numPoints < 2) {
        swipe.active = qfalse;
        swipe.numPoints = 0;
        return;
    }

    /* Render the swipe as a fading triangle strip */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_TEXTURE_2D);
    /* TODO: Bind swipe.shader when shader system is complete */

    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i < swipe.numPoints; i++) {
        swipePoint_t *sp = &swipe.points[i];

        /* Alpha fades from 1.0 (newest) to 0.0 (oldest) */
        float frac = (float)i / (float)(swipe.numPoints - 1);
        float alpha = 1.0f - frac;

        /* Also fade based on time if beyond lifetime */
        float age = swipe.points[swipe.numPoints - 1].time - sp->time;
        if (age > swipe.life) alpha = 0.0f;
        else if (swipe.life > 0.0f) alpha *= 1.0f - (age / swipe.life);

        /* White with computed alpha -- shader would override color */
        glColor4f(1.0f, 0.9f, 0.7f, alpha);

        /* Texture coordinate: U from 0 to 1 along the strip, V = 0/1 for edges */
        float u = frac;
        glTexCoord2f(u, 0.0f);
        glVertex3fv(sp->p1);
        glTexCoord2f(u, 1.0f);
        glVertex3fv(sp->p2);
    }
    glEnd();

    glEnable(GL_TEXTURE_2D);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);

    swipe.active = qfalse;
    swipe.numPoints = 0;
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
