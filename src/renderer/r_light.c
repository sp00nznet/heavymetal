/*
 * r_light.c -- Lighting system
 *
 * FAKK2 has true dynamic lighting (unlike stock Q3 which faked it).
 * Features:
 *   - Real dynamic lights (point, spot)
 *   - Lightmap atlas for static world lighting (128x128 blocks in BSP)
 *   - Per-vertex lighting for entities
 *   - Light style animation (flickering torches, pulsing crystals, etc.)
 *   - FAKK2 entity lights (LUMP_ENTLIGHTS) for designer-placed lights
 *   - Per-shader lighting properties (LUMP_LIGHTDEFS)
 *
 * Light styles use the Q3 convention:
 *   'a' = 0.0 brightness, 'z' = full brightness
 *   Style strings cycle through characters (e.g., "mmnmmommommnonmmonqnmmo"
 *   is the classic flickering torchlight).
 *
 * Dynamic lights are submitted per-frame via R_AddLightToScene().
 * The renderer uses them to modify surface lighting in real time.
 */

#include "../common/qcommon.h"
#include "../common/qfiles.h"
#include "tr_types.h"
#include "r_gl.h"
#include <string.h>
#include <math.h>

/* =========================================================================
 * Dynamic light tracking
 * ========================================================================= */

#define MAX_DLIGHTS     32

typedef struct {
    vec3_t      origin;
    float       intensity;
    float       color[3];       /* RGB, 0.0-1.0 */
    int         type;           /* 0 = point, 1 = spot */
    qboolean    additive;
} dlight_t;

static dlight_t r_dlights[MAX_DLIGHTS];
static int      r_numDlights;

void R_AddLightToScene(vec3_t origin, float intensity,
                        float r, float g, float b, int type) {
    if (r_numDlights >= MAX_DLIGHTS) return;

    dlight_t *dl = &r_dlights[r_numDlights++];
    VectorCopy(origin, dl->origin);
    dl->intensity = intensity;
    dl->color[0] = r;
    dl->color[1] = g;
    dl->color[2] = b;
    dl->type = type;
    dl->additive = qfalse;
}

void R_ClearLights(void) {
    r_numDlights = 0;
}

/* =========================================================================
 * Light style animation
 *
 * 64 light styles, each a string of brightness values.
 * Used for flickering torches, strobing warning lights, etc.
 * ========================================================================= */

#define MAX_LIGHTSTYLES     64
#define MAX_LIGHTSTYLE_LEN  64

typedef struct {
    char    pattern[MAX_LIGHTSTYLE_LEN];
    int     length;
    float   value;              /* current brightness 0.0-1.0 */
} lightstyle_t;

static lightstyle_t r_lightstyles[MAX_LIGHTSTYLES];

void R_SetLightStyle(int style, const char *data) {
    if (style < 0 || style >= MAX_LIGHTSTYLES) return;

    lightstyle_t *ls = &r_lightstyles[style];
    Q_strncpyz(ls->pattern, data ? data : "m", sizeof(ls->pattern));
    ls->length = (int)strlen(ls->pattern);
    if (ls->length < 1) {
        ls->pattern[0] = 'm';
        ls->length = 1;
    }
    ls->value = 1.0f;
}

void R_UpdateLightStyles(int time) {
    for (int i = 0; i < MAX_LIGHTSTYLES; i++) {
        lightstyle_t *ls = &r_lightstyles[i];
        if (ls->length <= 1) {
            ls->value = (ls->pattern[0] - 'a') / 25.0f;
            continue;
        }

        /* Animate through pattern */
        int idx = (time / 100) % ls->length;
        ls->value = (ls->pattern[idx] - 'a') / 25.0f;
        if (ls->value < 0.0f) ls->value = 0.0f;
        if (ls->value > 1.0f) ls->value = 1.0f;
    }
}

float R_GetLightStyleValue(int style) {
    if (style < 0 || style >= MAX_LIGHTSTYLES) return 1.0f;
    return r_lightstyles[style].value;
}

/* =========================================================================
 * Lightmap atlas
 *
 * BSP lightmaps are stored as 128x128 pixel blocks in LUMP_LIGHTMAPS.
 * Each surface references a lightmap by index and UV offset.
 * At load time, these are packed into GL textures (lightmap atlas pages).
 * ========================================================================= */

#define LIGHTMAP_SIZE           128
#define LIGHTMAP_BYTES_PER_PIXEL 3
#define MAX_LIGHTMAP_PAGES      64

typedef struct {
    int     numPages;
    int     pageWidth;
    int     pageHeight;
    GLuint  textures[MAX_LIGHTMAP_PAGES];
} lightmapAtlas_t;

static lightmapAtlas_t r_lightmapAtlas;

void R_LoadLightmaps(const byte *data, int dataLen) {
    if (!data || dataLen <= 0) {
        r_lightmapAtlas.numPages = 0;
        return;
    }

    int blockSize = LIGHTMAP_SIZE * LIGHTMAP_SIZE * LIGHTMAP_BYTES_PER_PIXEL;
    int numBlocks = dataLen / blockSize;

    Com_Printf("Loading %d lightmap blocks (%d bytes each)\n",
               numBlocks, blockSize);

    if (numBlocks > MAX_LIGHTMAP_PAGES) {
        Com_Printf("WARNING: %d lightmap blocks exceeds max %d\n",
                   numBlocks, MAX_LIGHTMAP_PAGES);
        numBlocks = MAX_LIGHTMAP_PAGES;
    }

    r_lightmapAtlas.numPages = numBlocks;
    r_lightmapAtlas.pageWidth = LIGHTMAP_SIZE;
    r_lightmapAtlas.pageHeight = LIGHTMAP_SIZE;

    /* Upload lightmap data to GL textures */
    glGenTextures(numBlocks, r_lightmapAtlas.textures);

    for (int i = 0; i < numBlocks; i++) {
        const byte *blockData = data + i * blockSize;

        glBindTexture(GL_TEXTURE_2D, r_lightmapAtlas.textures[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, LIGHTMAP_SIZE, LIGHTMAP_SIZE, 0,
                     GL_RGB, GL_UNSIGNED_BYTE, blockData);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    Com_Printf("Uploaded %d lightmap textures\n", numBlocks);
}

/* =========================================================================
 * Light grid (volumetric ambient lighting)
 *
 * The light grid provides ambient light color at any point in the world.
 * Used for lighting entities that aren't on a lightmapped surface.
 * Grid cells store directional light data compressed into bytes.
 * ========================================================================= */

/* =========================================================================
 * Light grid sampling
 *
 * Q3/FAKK2 light grid format: each cell is 8 bytes:
 *   [0-2] ambient RGB (bytes)
 *   [3-5] directed RGB (bytes)
 *   [6]   latitude (0-255 maps to 0-360 degrees)
 *   [7]   longitude (0-255 maps to 0-180 degrees)
 *
 * The grid covers the entire world bounding box with cells spaced
 * at gridSize intervals (typically 64x64x128). Grid bounds and origin
 * are derived from the world model's bounding box.
 * ========================================================================= */

#define LIGHTGRID_CELL_SIZE     8
#define LIGHTGRID_STEP_X        64
#define LIGHTGRID_STEP_Y        64
#define LIGHTGRID_STEP_Z        128

/* Grid bounds computed from BSP world model */
static vec3_t   r_gridMins;
static vec3_t   r_gridSize = { LIGHTGRID_STEP_X, LIGHTGRID_STEP_Y, LIGHTGRID_STEP_Z };
static int      r_gridBounds[3];
static byte     *r_gridData;
static int      r_gridDataLen;

void R_SetupLightGrid(const float *worldMins, const float *worldMaxs,
                       byte *gridData, int gridLen) {
    VectorCopy(worldMins, r_gridMins);
    r_gridData = gridData;
    r_gridDataLen = gridLen;

    for (int i = 0; i < 3; i++) {
        r_gridBounds[i] = (int)((worldMaxs[i] - worldMins[i]) / r_gridSize[i]) + 1;
        if (r_gridBounds[i] < 1) r_gridBounds[i] = 1;
    }
}

static qboolean R_SampleLightGrid(int ix, int iy, int iz,
                                    vec3_t ambient, vec3_t directed, vec3_t dir) {
    if (!r_gridData || r_gridDataLen <= 0) return qfalse;
    if (ix < 0 || iy < 0 || iz < 0) return qfalse;
    if (ix >= r_gridBounds[0] || iy >= r_gridBounds[1] || iz >= r_gridBounds[2])
        return qfalse;

    int offset = ((iz * r_gridBounds[1] + iy) * r_gridBounds[0] + ix) * LIGHTGRID_CELL_SIZE;
    if (offset + LIGHTGRID_CELL_SIZE > r_gridDataLen) return qfalse;

    const byte *cell = r_gridData + offset;

    /* Ambient and directed RGB (0-255 -> 0.0-1.0) */
    ambient[0]  = cell[0] / 255.0f;
    ambient[1]  = cell[1] / 255.0f;
    ambient[2]  = cell[2] / 255.0f;
    directed[0] = cell[3] / 255.0f;
    directed[1] = cell[4] / 255.0f;
    directed[2] = cell[5] / 255.0f;

    /* Decode light direction from spherical coordinates */
    float lat = cell[6] * (2.0f * 3.14159265f / 255.0f);
    float lng = cell[7] * (3.14159265f / 255.0f);

    dir[0] = cosf(lat) * sinf(lng);
    dir[1] = sinf(lat) * sinf(lng);
    dir[2] = cosf(lng);

    return qtrue;
}

void R_LightForPoint(const vec3_t point, vec3_t ambientLight,
                      vec3_t directedLight, vec3_t lightDir) {
    /* Default values in case grid is not loaded */
    VectorSet(ambientLight, 0.3f, 0.3f, 0.3f);
    VectorSet(directedLight, 0.7f, 0.7f, 0.7f);
    VectorSet(lightDir, -0.5f, -0.5f, -1.0f);

    if (!r_gridData || r_gridDataLen <= 0) {
        VectorNormalize(lightDir);
        return;
    }

    /* Convert world position to grid coordinates */
    float fx = (point[0] - r_gridMins[0]) / r_gridSize[0];
    float fy = (point[1] - r_gridMins[1]) / r_gridSize[1];
    float fz = (point[2] - r_gridMins[2]) / r_gridSize[2];

    int ix = (int)fx;
    int iy = (int)fy;
    int iz = (int)fz;

    /* Fractional parts for interpolation */
    float fracX = fx - (float)ix;
    float fracY = fy - (float)iy;
    float fracZ = fz - (float)iz;

    /* Trilinear interpolation of 8 surrounding cells */
    vec3_t totalAmbient = {0, 0, 0};
    vec3_t totalDirected = {0, 0, 0};
    vec3_t totalDir = {0, 0, 0};
    float totalWeight = 0.0f;

    for (int dz = 0; dz < 2; dz++) {
        for (int dy = 0; dy < 2; dy++) {
            for (int dx = 0; dx < 2; dx++) {
                vec3_t a, d, dir;
                if (!R_SampleLightGrid(ix + dx, iy + dy, iz + dz, a, d, dir))
                    continue;

                float weight = (dx ? fracX : (1.0f - fracX))
                             * (dy ? fracY : (1.0f - fracY))
                             * (dz ? fracZ : (1.0f - fracZ));

                VectorMA(totalAmbient, weight, a, totalAmbient);
                VectorMA(totalDirected, weight, d, totalDirected);
                VectorMA(totalDir, weight, dir, totalDir);
                totalWeight += weight;
            }
        }
    }

    if (totalWeight > 0.0f) {
        float invWeight = 1.0f / totalWeight;
        VectorScale(totalAmbient, invWeight, ambientLight);
        VectorScale(totalDirected, invWeight, directedLight);
        VectorScale(totalDir, invWeight, lightDir);
    }

    VectorNormalize(lightDir);
}

/* =========================================================================
 * Entity lighting
 *
 * Computes lighting for a refEntity_t from the light grid and
 * dynamic lights. Used by TIKI model rendering.
 * ========================================================================= */

void R_SetupEntityLighting(refEntity_t *ent) {
    if (!ent) return;

    /* Use entity origin or lightingOrigin */
    vec3_t lightOrigin;
    if (ent->renderfx & RF_LIGHTING_ORIGIN) {
        VectorCopy(ent->lightingOrigin, lightOrigin);
    } else {
        VectorCopy(ent->origin, lightOrigin);
    }

    /* Sample light grid */
    vec3_t ambient, directed, lightDir;
    R_LightForPoint(lightOrigin, ambient, directed, lightDir);

    /* Add contribution from dynamic lights */
    for (int i = 0; i < r_numDlights; i++) {
        dlight_t *dl = &r_dlights[i];
        vec3_t diff;
        VectorSubtract(dl->origin, lightOrigin, diff);
        float dist = VectorLength(diff);

        if (dist >= dl->intensity) continue;

        float attenuation = 1.0f - (dist / dl->intensity);
        ambient[0] += dl->color[0] * attenuation * 0.5f;
        ambient[1] += dl->color[1] * attenuation * 0.5f;
        ambient[2] += dl->color[2] * attenuation * 0.5f;
    }

    /* Clamp */
    for (int i = 0; i < 3; i++) {
        if (ambient[i] > 1.0f) ambient[i] = 1.0f;
        if (directed[i] > 1.0f) directed[i] = 1.0f;
    }

    /* Store in entity shaderRGBA for vertex coloring */
    ent->shaderRGBA[0] = (byte)(ambient[0] * 255.0f);
    ent->shaderRGBA[1] = (byte)(ambient[1] * 255.0f);
    ent->shaderRGBA[2] = (byte)(ambient[2] * 255.0f);
    ent->shaderRGBA[3] = 255;
}

/* =========================================================================
 * Initialization
 * ========================================================================= */

void R_InitLighting(void) {
    Com_Printf("--- R_InitLighting ---\n");

    r_numDlights = 0;
    memset(&r_lightmapAtlas, 0, sizeof(r_lightmapAtlas));

    /* Set all light styles to default (full bright) */
    for (int i = 0; i < MAX_LIGHTSTYLES; i++) {
        r_lightstyles[i].pattern[0] = 'm';
        r_lightstyles[i].length = 1;
        r_lightstyles[i].value = 0.5f;  /* 'm' = middle brightness */
    }

    /* Style 0 is always full bright */
    R_SetLightStyle(0, "m");

    Com_Printf("Lighting initialized\n");
}

GLuint R_GetLightmapTexture(int index) {
    if (index < 0 || index >= r_lightmapAtlas.numPages) return 0;
    return r_lightmapAtlas.textures[index];
}

void R_ShutdownLighting(void) {
    r_numDlights = 0;
    if (r_lightmapAtlas.numPages > 0) {
        glDeleteTextures(r_lightmapAtlas.numPages, r_lightmapAtlas.textures);
        memset(&r_lightmapAtlas, 0, sizeof(r_lightmapAtlas));
    }
}
