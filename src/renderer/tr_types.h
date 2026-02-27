/*
 * tr_types.h -- Renderer types shared with game modules
 *
 * These structures are part of the engine-game interface.
 * Based on the FAKK2 SDK's tr_types.h (346 lines in cgame/).
 */

#ifndef TR_TYPES_H
#define TR_TYPES_H

#include "../common/fakk_types.h"

/* =========================================================================
 * Render entity -- passed from game to renderer for drawing
 * ========================================================================= */

#define RF_MINLIGHT         0x0001  /* always have some light */
#define RF_THIRD_PERSON     0x0002  /* don't draw through eyes, drawn in mirrors */
#define RF_FIRST_PERSON     0x0004  /* only draw through eyes */
#define RF_DEPTHHACK        0x0008  /* hack depth range for viewmodel */
#define RF_NOSHADOW         0x0040  /* don't add stencil shadows */
#define RF_LIGHTING_ORIGIN  0x0080  /* use light origin instead of entity origin */
#define RF_SHADOW_PLANE     0x0100  /* use shadow plane */
#define RF_WRAP_FRAMES      0x0200  /* wrap animation frames */
#define RF_ADDITIVE_DLIGHT  0x0400  /* UberTools: additive dynamic light */
#define RF_SKYENTITY        0x0800  /* UberTools: render in sky portal */
#define RF_FULLBRIGHT       0x1000  /* UberTools: ignore lighting */

typedef struct {
    qhandle_t   hModel;             /* model handle */

    vec3_t      lightingOrigin;
    float       shadowPlane;

    vec3_t      axis[3];            /* rotation matrix */
    qboolean    nonNormalizedAxes;

    vec3_t      origin;
    int         frame;

    vec3_t      oldorigin;
    int         oldframe;
    float       backlerp;           /* 0.0 = current, 1.0 = old */

    int         skinNum;
    qhandle_t   customSkin;
    qhandle_t   customShader;

    byte        shaderRGBA[4];
    float       shaderTexCoord[2];
    float       shaderTime;

    int         renderfx;

    /* TIKI/UberTools additions */
    float       scale;
    float       shader_data[2];
    int         *frameInfo;         /* TIKI animation frame info */
    int         bone_tag[5];
    vec3_t      bone_angles[5];
    int         surfaces[TIKI_MAX_SURFACES];
    dtiki_t     tiki;
} refEntity_t;

/* =========================================================================
 * Render definition -- per-frame rendering parameters
 * ========================================================================= */

#define MAX_RENDER_STRINGS  8
#define MAX_RENDER_STRING_LENGTH 32

typedef struct {
    int         x, y, width, height;    /* viewport */
    float       fov_x, fov_y;

    vec3_t      vieworg;
    vec3_t      viewaxis[3];

    int         time;                    /* time in milliseconds for shader effects */

    int         rdflags;

    byte        areamask[32];            /* PVS area visibility */
    qboolean    areamaskModified;

    float       blend[4];               /* screen blend (pain flash, underwater, etc.) */

    char        text[MAX_RENDER_STRINGS][MAX_RENDER_STRING_LENGTH];

    /* UberTools additions */
    float       sky_alpha;
    vec3_t      sky_origin;
    vec3_t      sky_axis[3];

    int         num_entities;
    refEntity_t *entities;
} refdef_t;

/* =========================================================================
 * OpenGL config -- queried by game modules
 * ========================================================================= */

typedef struct {
    char        renderer_string[1024];
    char        vendor_string[1024];
    char        version_string[1024];
    char        extensions_string[8192];

    int         maxTextureSize;
    int         maxActiveTextures;

    int         colorBits;
    int         depthBits;
    int         stencilBits;

    int         vidWidth;
    int         vidHeight;

    float       windowAspect;

    qboolean    deviceSupportsGamma;
    qboolean    isFullscreen;
} glconfig_t;

/* =========================================================================
 * Polygon vertex for decals/marks
 * ========================================================================= */

typedef struct {
    vec3_t      xyz;
    float       st[2];              /* texture coordinates */
    byte        modulate[4];        /* color modulation */
} polyVert_t;

#endif /* TR_TYPES_H */
