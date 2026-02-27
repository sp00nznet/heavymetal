/*
 * r_main.c -- Renderer core
 *
 * FAKK2's renderer is a modified id Tech 3 renderer with:
 *   - TIKI skeletal model rendering (replacing Q3's vertex animation)
 *   - Enhanced shader system (lens flares, sky portals)
 *   - True dynamic lighting
 *   - Ghost particle rendering integration
 *   - LOD support for TIKI models
 *
 * The original dynamically loads OpenGL via LoadLibraryA/GetProcAddress.
 * The recomp uses SDL2's GL loader for portability.
 *
 * OpenGL was the only rendering path -- no Direct3D support in original.
 */

#include "../common/qcommon.h"
#include "tr_types.h"

static glconfig_t glconfig;

/* =========================================================================
 * Renderer initialization
 * ========================================================================= */

static void R_InitGL(void) {
    Com_Printf("--- R_InitGL ---\n");
    /* TODO: Load OpenGL functions via GLimp_GetProcAddress */
    /* TODO: Query GL caps and populate glconfig */
    Com_Printf("Renderer: OpenGL initialized\n");
}

/* =========================================================================
 * Renderer subsystem stubs
 * ========================================================================= */

/* These will be called from CL_Init */

void R_Init(void) {
    Com_Printf("--- R_Init ---\n");
    R_InitGL();

    /* TODO: Initialize renderer subsystems:
     *   - Shader system (parse .shader files from PK3)
     *   - BSP loading (FAKK BSP version 12, header "FAKK")
     *   - TIKI model renderer
     *   - Ghost particle renderer
     *   - Lightmap atlas
     *   - Font rendering
     *   - Sky portal system
     */
}

void R_Shutdown(void) {
    Com_Printf("Renderer shutdown\n");
}

void R_BeginFrame(void) {
    /* TODO: Clear buffers, set viewport */
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

void R_EndFrame(void) {
    /* TODO: Swap buffers */
    Win_SwapBuffers();
}

/* =========================================================================
 * Model registration
 * ========================================================================= */

qhandle_t R_RegisterModel(const char *name) {
    Com_DPrintf("R_RegisterModel: %s (stub)\n", name);
    return 0;
}

qhandle_t R_RegisterShader(const char *name) {
    Com_DPrintf("R_RegisterShader: %s (stub)\n", name);
    return 0;
}

qhandle_t R_RegisterSkin(const char *name) {
    Com_DPrintf("R_RegisterSkin: %s (stub)\n", name);
    return 0;
}

/* =========================================================================
 * 2D drawing (HUD, menus)
 * ========================================================================= */

void R_DrawStretchPic(float x, float y, float w, float h,
                      float s1, float t1, float s2, float t2,
                      qhandle_t hShader) {
    (void)x; (void)y; (void)w; (void)h;
    (void)s1; (void)t1; (void)s2; (void)t2;
    (void)hShader;
    /* TODO: Draw a textured quad in 2D screen space */
}

void R_SetColor(const float *rgba) {
    (void)rgba;
    /* TODO: Set current drawing color */
}
