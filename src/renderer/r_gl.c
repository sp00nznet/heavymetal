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
 * Bitmap font rendering
 *
 * Procedurally generates a 128x128 GL texture containing a 5x7 dot-matrix
 * font for ASCII 32-127 (16 chars per row, 6 rows). Each glyph is stored
 * in a compact bitmask table and rasterized into the texture at init time.
 * ========================================================================= */

static GLuint r_fontTexture = 0;
#define FONT_CHAR_W 8
#define FONT_CHAR_H 14
#define FONT_COLS   16
#define FONT_ROWS   6
#define FONT_TEX_W  128
#define FONT_TEX_H  128

/* 5x7 bitmap font data for ASCII 32-127. Each character is 7 bytes
 * (one per row), with bits 0-4 representing columns left-to-right. */
static const unsigned char fontBitmaps[96][7] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* space */
    {0x04,0x04,0x04,0x04,0x00,0x04,0x00}, /* ! */
    {0x0A,0x0A,0x00,0x00,0x00,0x00,0x00}, /* " */
    {0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0x00}, /* # */
    {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}, /* $ */
    {0x19,0x1A,0x02,0x04,0x0B,0x13,0x00}, /* % */
    {0x08,0x14,0x08,0x15,0x12,0x0D,0x00}, /* & */
    {0x04,0x04,0x00,0x00,0x00,0x00,0x00}, /* ' */
    {0x02,0x04,0x08,0x08,0x08,0x04,0x02}, /* ( */
    {0x08,0x04,0x02,0x02,0x02,0x04,0x08}, /* ) */
    {0x00,0x0A,0x04,0x1F,0x04,0x0A,0x00}, /* * */
    {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, /* + */
    {0x00,0x00,0x00,0x00,0x04,0x04,0x08}, /* , */
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, /* - */
    {0x00,0x00,0x00,0x00,0x00,0x04,0x00}, /* . */
    {0x01,0x02,0x02,0x04,0x08,0x08,0x10}, /* / */
    {0x0E,0x11,0x13,0x15,0x19,0x0E,0x00}, /* 0 */
    {0x04,0x0C,0x04,0x04,0x04,0x0E,0x00}, /* 1 */
    {0x0E,0x11,0x01,0x06,0x08,0x1F,0x00}, /* 2 */
    {0x0E,0x11,0x02,0x01,0x11,0x0E,0x00}, /* 3 */
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x00}, /* 4 */
    {0x1F,0x10,0x1E,0x01,0x11,0x0E,0x00}, /* 5 */
    {0x06,0x08,0x1E,0x11,0x11,0x0E,0x00}, /* 6 */
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x00}, /* 7 */
    {0x0E,0x11,0x0E,0x11,0x11,0x0E,0x00}, /* 8 */
    {0x0E,0x11,0x0F,0x01,0x02,0x0C,0x00}, /* 9 */
    {0x00,0x04,0x00,0x00,0x04,0x00,0x00}, /* : */
    {0x00,0x04,0x00,0x00,0x04,0x04,0x08}, /* ; */
    {0x02,0x04,0x08,0x08,0x04,0x02,0x00}, /* < */
    {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}, /* = */
    {0x08,0x04,0x02,0x02,0x04,0x08,0x00}, /* > */
    {0x0E,0x11,0x02,0x04,0x00,0x04,0x00}, /* ? */
    {0x0E,0x11,0x17,0x15,0x17,0x10,0x0E}, /* @ */
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x00}, /* A */
    {0x1E,0x11,0x1E,0x11,0x11,0x1E,0x00}, /* B */
    {0x0E,0x11,0x10,0x10,0x11,0x0E,0x00}, /* C */
    {0x1C,0x12,0x11,0x11,0x12,0x1C,0x00}, /* D */
    {0x1F,0x10,0x1E,0x10,0x10,0x1F,0x00}, /* E */
    {0x1F,0x10,0x1E,0x10,0x10,0x10,0x00}, /* F */
    {0x0E,0x11,0x10,0x13,0x11,0x0E,0x00}, /* G */
    {0x11,0x11,0x1F,0x11,0x11,0x11,0x00}, /* H */
    {0x0E,0x04,0x04,0x04,0x04,0x0E,0x00}, /* I */
    {0x07,0x02,0x02,0x02,0x12,0x0C,0x00}, /* J */
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, /* K */
    {0x10,0x10,0x10,0x10,0x10,0x1F,0x00}, /* L */
    {0x11,0x1B,0x15,0x11,0x11,0x11,0x00}, /* M */
    {0x11,0x19,0x15,0x13,0x11,0x11,0x00}, /* N */
    {0x0E,0x11,0x11,0x11,0x11,0x0E,0x00}, /* O */
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x00}, /* P */
    {0x0E,0x11,0x11,0x15,0x12,0x0D,0x00}, /* Q */
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x00}, /* R */
    {0x0E,0x11,0x0C,0x02,0x11,0x0E,0x00}, /* S */
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x00}, /* T */
    {0x11,0x11,0x11,0x11,0x11,0x0E,0x00}, /* U */
    {0x11,0x11,0x11,0x11,0x0A,0x04,0x00}, /* V */
    {0x11,0x11,0x11,0x15,0x1B,0x11,0x00}, /* W */
    {0x11,0x0A,0x04,0x04,0x0A,0x11,0x00}, /* X */
    {0x11,0x0A,0x04,0x04,0x04,0x04,0x00}, /* Y */
    {0x1F,0x01,0x02,0x04,0x08,0x1F,0x00}, /* Z */
    {0x0E,0x08,0x08,0x08,0x08,0x0E,0x00}, /* [ */
    {0x10,0x08,0x08,0x04,0x02,0x02,0x01}, /* \ */
    {0x0E,0x02,0x02,0x02,0x02,0x0E,0x00}, /* ] */
    {0x04,0x0A,0x11,0x00,0x00,0x00,0x00}, /* ^ */
    {0x00,0x00,0x00,0x00,0x00,0x1F,0x00}, /* _ */
    {0x08,0x04,0x00,0x00,0x00,0x00,0x00}, /* ` */
    {0x00,0x0E,0x01,0x0F,0x11,0x0F,0x00}, /* a */
    {0x10,0x10,0x1E,0x11,0x11,0x1E,0x00}, /* b */
    {0x00,0x0E,0x11,0x10,0x11,0x0E,0x00}, /* c */
    {0x01,0x01,0x0F,0x11,0x11,0x0F,0x00}, /* d */
    {0x00,0x0E,0x11,0x1F,0x10,0x0E,0x00}, /* e */
    {0x06,0x08,0x1C,0x08,0x08,0x08,0x00}, /* f */
    {0x00,0x0F,0x11,0x0F,0x01,0x0E,0x00}, /* g */
    {0x10,0x10,0x1E,0x11,0x11,0x11,0x00}, /* h */
    {0x04,0x00,0x0C,0x04,0x04,0x0E,0x00}, /* i */
    {0x02,0x00,0x06,0x02,0x02,0x12,0x0C}, /* j */
    {0x10,0x12,0x14,0x18,0x14,0x12,0x00}, /* k */
    {0x0C,0x04,0x04,0x04,0x04,0x0E,0x00}, /* l */
    {0x00,0x1A,0x15,0x15,0x11,0x11,0x00}, /* m */
    {0x00,0x1E,0x11,0x11,0x11,0x11,0x00}, /* n */
    {0x00,0x0E,0x11,0x11,0x11,0x0E,0x00}, /* o */
    {0x00,0x1E,0x11,0x1E,0x10,0x10,0x00}, /* p */
    {0x00,0x0F,0x11,0x0F,0x01,0x01,0x00}, /* q */
    {0x00,0x16,0x19,0x10,0x10,0x10,0x00}, /* r */
    {0x00,0x0F,0x10,0x0E,0x01,0x1E,0x00}, /* s */
    {0x08,0x1C,0x08,0x08,0x08,0x06,0x00}, /* t */
    {0x00,0x11,0x11,0x11,0x11,0x0F,0x00}, /* u */
    {0x00,0x11,0x11,0x11,0x0A,0x04,0x00}, /* v */
    {0x00,0x11,0x11,0x15,0x15,0x0A,0x00}, /* w */
    {0x00,0x11,0x0A,0x04,0x0A,0x11,0x00}, /* x */
    {0x00,0x11,0x11,0x0F,0x01,0x0E,0x00}, /* y */
    {0x00,0x1F,0x02,0x04,0x08,0x1F,0x00}, /* z */
    {0x02,0x04,0x0C,0x04,0x04,0x02,0x00}, /* { */
    {0x04,0x04,0x04,0x04,0x04,0x04,0x00}, /* | */
    {0x08,0x04,0x06,0x04,0x04,0x08,0x00}, /* } */
    {0x00,0x00,0x08,0x15,0x02,0x00,0x00}, /* ~ */
};

void R_InitFont(void) {
    /* Generate a 128x128 RGBA texture atlas with all ASCII glyphs */
    byte *pixels = (byte *)Z_TagMalloc(FONT_TEX_W * FONT_TEX_H * 4, TAG_RENDERER);
    memset(pixels, 0, FONT_TEX_W * FONT_TEX_H * 4);

    for (int ch = 0; ch < 96; ch++) {
        int col = ch % FONT_COLS;
        int row = ch / FONT_COLS;
        int baseX = col * FONT_CHAR_W;
        int baseY = row * (FONT_CHAR_H + 2);  /* +2 for spacing */

        for (int py = 0; py < 7; py++) {
            unsigned char bits = fontBitmaps[ch][py];
            for (int px = 0; px < 5; px++) {
                if (bits & (1 << (4 - px))) {
                    int tx = baseX + px + 1;
                    int ty = baseY + py * 2 + 1;  /* scale 7 rows into ~14px height */
                    if (tx < FONT_TEX_W && ty < FONT_TEX_H) {
                        int idx = (ty * FONT_TEX_W + tx) * 4;
                        pixels[idx + 0] = 255;
                        pixels[idx + 1] = 255;
                        pixels[idx + 2] = 255;
                        pixels[idx + 3] = 255;
                        /* Also fill the row below for 2x vertical scale */
                        int idx2 = ((ty + 1) * FONT_TEX_W + tx) * 4;
                        if (ty + 1 < FONT_TEX_H) {
                            pixels[idx2 + 0] = 255;
                            pixels[idx2 + 1] = 255;
                            pixels[idx2 + 2] = 255;
                            pixels[idx2 + 3] = 255;
                        }
                    }
                }
            }
        }
    }

    glGenTextures(1, &r_fontTexture);
    glBindTexture(GL_TEXTURE_2D, r_fontTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, FONT_TEX_W, FONT_TEX_H, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    Z_Free(pixels);
    Com_Printf("Font texture generated (%dx%d)\n", FONT_TEX_W, FONT_TEX_H);
}

void R_ShutdownFont(void) {
    if (r_fontTexture) {
        glDeleteTextures(1, &r_fontTexture);
        r_fontTexture = 0;
    }
}

void R_DrawChar(float x, float y, int ch, float scale,
                float r, float g, float b, float a) {
    if (ch < 32 || ch > 127) return;
    if (!r_fontTexture) return;

    int idx = ch - 32;
    int col = idx % FONT_COLS;
    int row = idx / FONT_COLS;

    float u0 = (float)(col * FONT_CHAR_W) / (float)FONT_TEX_W;
    float v0 = (float)(row * (FONT_CHAR_H + 2)) / (float)FONT_TEX_H;
    float u1 = u0 + (float)FONT_CHAR_W / (float)FONT_TEX_W;
    float v1 = v0 + (float)FONT_CHAR_H / (float)FONT_TEX_H;

    float cw = FONT_CHAR_W * scale;
    float ch_h = FONT_CHAR_H * scale;

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, r_fontTexture);
    glColor4f(r, g, b, a);

    glBegin(GL_QUADS);
    glTexCoord2f(u0, v0); glVertex2f(x, y);
    glTexCoord2f(u1, v0); glVertex2f(x + cw, y);
    glTexCoord2f(u1, v1); glVertex2f(x + cw, y + ch_h);
    glTexCoord2f(u0, v1); glVertex2f(x, y + ch_h);
    glEnd();
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
