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
#include "r_gl.h"
#include <string.h>
#include <math.h>

/* =========================================================================
 * Sky state
 * ========================================================================= */

typedef struct {
    qboolean    loaded;
    char        images[6][MAX_QPATH];   /* skybox face textures */
    GLuint      textures[6];            /* GL texture IDs for each face */
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

/* =========================================================================
 * TGA texture loading for skybox faces
 *
 * Loads uncompressed or RLE-compressed TGA files from the filesystem.
 * Returns RGBA pixel data allocated with Z_TagMalloc. Caller must free.
 * ========================================================================= */

static byte *R_LoadTGA(const char *name, int *width, int *height) {
    void *buffer;
    long len;

    /* Try .tga first, then .jpg placeholder */
    len = FS_ReadFile(name, &buffer);
    if (len <= 18 || !buffer) return NULL;

    const byte *data = (const byte *)buffer;

    /* Parse TGA header */
    int idLength    = data[0];
    int colorMapType = data[1];
    int imageType   = data[2];
    int w           = data[12] | (data[13] << 8);
    int h           = data[14] | (data[15] << 8);
    int bpp         = data[16];
    int descriptor  = data[17];

    (void)colorMapType;

    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) {
        FS_FreeFile(buffer);
        return NULL;
    }

    /* Only support uncompressed RGB/RGBA (type 2) or RLE (type 10) */
    if (imageType != 2 && imageType != 10) {
        Com_DPrintf("R_LoadTGA: unsupported type %d in '%s'\n", imageType, name);
        FS_FreeFile(buffer);
        return NULL;
    }

    int bytesPerPixel = bpp / 8;
    if (bytesPerPixel != 3 && bytesPerPixel != 4) {
        FS_FreeFile(buffer);
        return NULL;
    }

    *width = w;
    *height = h;

    byte *pixels = (byte *)Z_TagMalloc(w * h * 4, TAG_RENDERER);
    const byte *src = data + 18 + idLength;

    if (imageType == 2) {
        /* Uncompressed */
        for (int row = 0; row < h; row++) {
            /* TGA is bottom-up by default unless bit 5 of descriptor is set */
            int destRow = (descriptor & 0x20) ? row : (h - 1 - row);
            byte *dest = pixels + destRow * w * 4;

            for (int col = 0; col < w; col++) {
                dest[0] = src[2]; /* R (TGA stores BGR) */
                dest[1] = src[1]; /* G */
                dest[2] = src[0]; /* B */
                dest[3] = (bytesPerPixel == 4) ? src[3] : 255;
                src += bytesPerPixel;
                dest += 4;
            }
        }
    } else {
        /* RLE compressed */
        int pixelCount = w * h;
        int currentPixel = 0;

        while (currentPixel < pixelCount) {
            byte header = *src++;
            int count = (header & 0x7F) + 1;

            if (header & 0x80) {
                /* RLE packet */
                byte b = src[0], g = src[1], r = src[2];
                byte a = (bytesPerPixel == 4) ? src[3] : 255;
                src += bytesPerPixel;

                for (int i = 0; i < count && currentPixel < pixelCount; i++, currentPixel++) {
                    int row = currentPixel / w;
                    int destRow = (descriptor & 0x20) ? row : (h - 1 - row);
                    int col = currentPixel % w;
                    byte *dest = pixels + (destRow * w + col) * 4;
                    dest[0] = r; dest[1] = g; dest[2] = b; dest[3] = a;
                }
            } else {
                /* Raw packet */
                for (int i = 0; i < count && currentPixel < pixelCount; i++, currentPixel++) {
                    int row = currentPixel / w;
                    int destRow = (descriptor & 0x20) ? row : (h - 1 - row);
                    int col = currentPixel % w;
                    byte *dest = pixels + (destRow * w + col) * 4;
                    dest[0] = src[2]; dest[1] = src[1]; dest[2] = src[0];
                    dest[3] = (bytesPerPixel == 4) ? src[3] : 255;
                    src += bytesPerPixel;
                }
            }
        }
    }

    FS_FreeFile(buffer);
    return pixels;
}

void R_SetSkyBox(const char *basename) {
    if (!basename || !basename[0]) {
        r_sky.loaded = qfalse;
        return;
    }

    static const char *suffixes[6] = { "_rt", "_lf", "_up", "_dn", "_bk", "_ft" };

    /* Free old textures if any */
    for (int i = 0; i < 6; i++) {
        if (r_sky.textures[i]) {
            glDeleteTextures(1, &r_sky.textures[i]);
            r_sky.textures[i] = 0;
        }
    }

    int loadedCount = 0;
    for (int i = 0; i < 6; i++) {
        snprintf(r_sky.images[i], MAX_QPATH, "%s%s", basename, suffixes[i]);

        /* Try loading as TGA */
        char path[MAX_QPATH];
        int w, h;
        byte *pixels = NULL;

        snprintf(path, sizeof(path), "%s%s.tga", basename, suffixes[i]);
        pixels = R_LoadTGA(path, &w, &h);

        if (!pixels) {
            /* Try without extension */
            snprintf(path, sizeof(path), "%s%s", basename, suffixes[i]);
            pixels = R_LoadTGA(path, &w, &h);
        }

        if (pixels) {
            glGenTextures(1, &r_sky.textures[i]);
            glBindTexture(GL_TEXTURE_2D, r_sky.textures[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            Z_Free(pixels);
            loadedCount++;
        } else {
            r_sky.textures[i] = 0;
            Com_DPrintf("R_SetSkyBox: missing face '%s'\n", path);
        }
    }

    glBindTexture(GL_TEXTURE_2D, 0);

    r_sky.loaded = (loadedCount > 0);
    Com_Printf("Skybox loaded: %s (%d/6 faces)\n", basename, loadedCount);
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

/* Skybox face UVs -- standard mapping for each face quad */
static const float skyTexCoords[4][2] = {
    { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f }
};

void R_DrawSky(const vec3_t viewOrigin) {
    if (!r_sky.loaded) return;

    /* Save GL state */
    glPushMatrix();

    /* Translate to viewer position -- sky always surrounds the camera */
    glTranslatef(viewOrigin[0], viewOrigin[1], viewOrigin[2]);

    /* Scale up the unit cube so the skybox is large */
    float skyDist = 4096.0f;
    glScalef(skyDist, skyDist, skyDist);

    /* Disable depth writing so sky is always behind everything */
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);

    /* Full brightness */
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glEnable(GL_TEXTURE_2D);

    /* Draw each of the 6 faces */
    for (int face = 0; face < 6; face++) {
        if (!r_sky.textures[face]) continue;

        glBindTexture(GL_TEXTURE_2D, r_sky.textures[face]);

        glBegin(GL_QUADS);
        for (int v = 0; v < 4; v++) {
            glTexCoord2f(skyTexCoords[v][0], skyTexCoords[v][1]);
            glVertex3f(skyVerts[face][v][0],
                       skyVerts[face][v][1],
                       skyVerts[face][v][2]);
        }
        glEnd();
    }

    /* Restore GL state */
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glPopMatrix();
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
    for (int i = 0; i < 6; i++) {
        if (r_sky.textures[i]) {
            glDeleteTextures(1, &r_sky.textures[i]);
        }
    }
    memset(&r_sky, 0, sizeof(r_sky));
}
