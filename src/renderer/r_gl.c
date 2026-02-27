/*
 * r_gl.c -- OpenGL function loading and rendering primitives
 *
 * Loads GL extension functions via SDL2 and provides core rendering
 * operations: buffer management, 2D drawing, viewport setup.
 *
 * The original FAKK2 renderer uses fixed-function OpenGL with
 * multitexture extensions. This recomp loads the same set of
 * functions for compatibility, plus modern extensions for
 * potential enhancements.
 */

#include "../common/qcommon.h"
#include "tr_types.h"
#include "r_gl.h"
#include <string.h>
#include <math.h>

/* =========================================================================
 * Function pointer storage
 * ========================================================================= */

PFNGLACTIVETEXTUREPROC           qglActiveTexture;
PFNGLCLIENTACTIVETEXTUREPROC     qglClientActiveTexture;
PFNGLGENBUFFERSPROC              qglGenBuffers;
PFNGLDELETEBUFFERSPROC           qglDeleteBuffers;
PFNGLBINDBUFFERPROC              qglBindBuffer;
PFNGLBUFFERDATAPROC              qglBufferData;
PFNGLCREATESHADERPROC            qglCreateShader;
PFNGLDELETESHADERPROC            qglDeleteShader;
PFNGLSHADERSOURCEPROC            qglShaderSource;
PFNGLCOMPILESHADERPROC           qglCompileShader;
PFNGLCREATEPROGRAMPROC           qglCreateProgram;
PFNGLDELETEPROGRAMPROC           qglDeleteProgram;
PFNGLATTACHSHADERPROC            qglAttachShader;
PFNGLLINKPROGRAMPROC             qglLinkProgram;
PFNGLUSEPROGRAMPROC              qglUseProgram;
PFNGLGETUNIFORMLOCATIONPROC      qglGetUniformLocation;
PFNGLUNIFORM1IPROC              qglUniform1i;
PFNGLUNIFORM1FPROC              qglUniform1f;
PFNGLUNIFORM3FPROC              qglUniform3f;
PFNGLUNIFORM4FPROC              qglUniform4f;
PFNGLUNIFORMMATRIX4FVPROC       qglUniformMatrix4fv;
PFNGLGETSHADERIVPROC            qglGetShaderiv;
PFNGLGETSHADERINFOLOGPROC       qglGetShaderInfoLog;
PFNGLGETPROGRAMIVPROC           qglGetProgramiv;
PFNGLGETPROGRAMINFOLOGPROC      qglGetProgramInfoLog;
PFNGLGENVERTEXARRAYSPROC        qglGenVertexArrays;
PFNGLDELETEVERTEXARRAYSPROC     qglDeleteVertexArrays;
PFNGLBINDVERTEXARRAYPROC        qglBindVertexArray;
PFNGLENABLEVERTEXATTRIBARRAYPROC qglEnableVertexAttribArray;
PFNGLVERTEXATTRIBPOINTERPROC    qglVertexAttribPointer;

/* =========================================================================
 * GL function loading
 * ========================================================================= */

#define LOAD_GL(name, type) \
    name = (type)GLimp_GetProcAddress(#name + 1); \
    if (!name) Com_DPrintf("GL: Missing function: %s\n", #name + 1)

qboolean R_LoadGLFunctions(void) {
    Com_Printf("Loading OpenGL functions...\n");

    /* Multitexture */
    LOAD_GL(qglActiveTexture, PFNGLACTIVETEXTUREPROC);
    LOAD_GL(qglClientActiveTexture, PFNGLCLIENTACTIVETEXTUREPROC);

    /* VBO */
    LOAD_GL(qglGenBuffers, PFNGLGENBUFFERSPROC);
    LOAD_GL(qglDeleteBuffers, PFNGLDELETEBUFFERSPROC);
    LOAD_GL(qglBindBuffer, PFNGLBINDBUFFERPROC);
    LOAD_GL(qglBufferData, PFNGLBUFFERDATAPROC);

    /* Shaders */
    LOAD_GL(qglCreateShader, PFNGLCREATESHADERPROC);
    LOAD_GL(qglDeleteShader, PFNGLDELETESHADERPROC);
    LOAD_GL(qglShaderSource, PFNGLSHADERSOURCEPROC);
    LOAD_GL(qglCompileShader, PFNGLCOMPILESHADERPROC);
    LOAD_GL(qglCreateProgram, PFNGLCREATEPROGRAMPROC);
    LOAD_GL(qglDeleteProgram, PFNGLDELETEPROGRAMPROC);
    LOAD_GL(qglAttachShader, PFNGLATTACHSHADERPROC);
    LOAD_GL(qglLinkProgram, PFNGLLINKPROGRAMPROC);
    LOAD_GL(qglUseProgram, PFNGLUSEPROGRAMPROC);
    LOAD_GL(qglGetUniformLocation, PFNGLGETUNIFORMLOCATIONPROC);
    LOAD_GL(qglUniform1i, PFNGLUNIFORM1IPROC);
    LOAD_GL(qglUniform1f, PFNGLUNIFORM1FPROC);
    LOAD_GL(qglUniform3f, PFNGLUNIFORM3FPROC);
    LOAD_GL(qglUniform4f, PFNGLUNIFORM4FPROC);
    LOAD_GL(qglUniformMatrix4fv, PFNGLUNIFORMMATRIX4FVPROC);
    LOAD_GL(qglGetShaderiv, PFNGLGETSHADERIVPROC);
    LOAD_GL(qglGetShaderInfoLog, PFNGLGETSHADERINFOLOGPROC);
    LOAD_GL(qglGetProgramiv, PFNGLGETPROGRAMIVPROC);
    LOAD_GL(qglGetProgramInfoLog, PFNGLGETPROGRAMINFOLOGPROC);

    /* VAO */
    LOAD_GL(qglGenVertexArrays, PFNGLGENVERTEXARRAYSPROC);
    LOAD_GL(qglDeleteVertexArrays, PFNGLDELETEVERTEXARRAYSPROC);
    LOAD_GL(qglBindVertexArray, PFNGLBINDVERTEXARRAYPROC);
    LOAD_GL(qglEnableVertexAttribArray, PFNGLENABLEVERTEXATTRIBARRAYPROC);
    LOAD_GL(qglVertexAttribPointer, PFNGLVERTEXATTRIBPOINTERPROC);

    /* Verify critical functions loaded */
    if (!qglActiveTexture) {
        Com_Printf("WARNING: GL multitexture not available\n");
    }

    Com_Printf("OpenGL functions loaded\n");
    return qtrue;
}

/* =========================================================================
 * 2D rendering mode
 *
 * Sets up orthographic projection for HUD/menu drawing.
 * This matches the original FAKK2 renderer's 2D mode.
 * ========================================================================= */

static int r_2dActive = 0;

void R_Set2D(void) {
    extern glconfig_t *R_GetGlconfigPtr(void);
    glconfig_t *gc = R_GetGlconfigPtr();
    int w = gc ? gc->vidWidth : 1280;
    int h = gc ? gc->vidHeight : 720;

    glViewport(0, 0, w, h);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, w, h, 0, -1, 1);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    r_2dActive = 1;
}

/* =========================================================================
 * 2D drawing primitives
 * ========================================================================= */

void R_DrawFillRect(float x, float y, float w, float h,
                    float r, float g, float b, float a) {
    glDisable(GL_TEXTURE_2D);
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();
    glEnable(GL_TEXTURE_2D);
}

/* =========================================================================
 * Basic text rendering using GL bitmap font
 *
 * Uses a simple 8x16 bitmap font for console/debug text.
 * This is a temporary solution; proper font rendering will use
 * the game's actual font textures.
 * ========================================================================= */

/* Minimal character drawing using GL lines (for bootstrap debugging) */
void R_DrawChar(float x, float y, int ch, float scale,
                float r, float g, float b, float a) {
    if (ch < 32 || ch > 126) return;

    glDisable(GL_TEXTURE_2D);
    glColor4f(r, g, b, a);

    /* Simple dot-matrix font -- 5x7 grid per character.
     * We use GL lines for maximum compatibility without needing
     * a font texture loaded. Each character is drawn as a
     * small filled rectangle (much faster than individual dots). */
    float cw = 8.0f * scale;
    float ch_h = 14.0f * scale;

    /* For now, draw each char as a small rectangle -- this is a placeholder
     * until we load the actual FAKK2 font texture from the PK3 */
    glBegin(GL_QUADS);
    glVertex2f(x + 1.0f, y + 2.0f);
    glVertex2f(x + cw - 1.0f, y + 2.0f);
    glVertex2f(x + cw - 1.0f, y + ch_h - 2.0f);
    glVertex2f(x + 1.0f, y + ch_h - 2.0f);
    glEnd();

    glEnable(GL_TEXTURE_2D);
    (void)ch; /* will be used when font texture is loaded */
}

void R_DrawString(float x, float y, const char *str,
                  float scale, float r, float g, float b, float a) {
    if (!str) return;
    float cx = x;
    float charWidth = 8.0f * scale;
    float charHeight = 14.0f * scale;

    for (const char *p = str; *p; p++) {
        if (*p == '\n') {
            cx = x;
            y += charHeight;
            continue;
        }
        R_DrawChar(cx, y, *p, scale, r, g, b, a);
        cx += charWidth;
    }
}
