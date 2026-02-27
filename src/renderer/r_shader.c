/*
 * r_shader.c -- Shader system
 *
 * FAKK2 extends Q3's shader system with:
 *   - Additional surface parameters for UberTools materials
 *   - Lens flare support
 *   - Sky portal shaders
 *   - Enhanced blending modes
 *   - Per-surface subdivision (MST_TERRAIN)
 *
 * Shaders are defined in .shader text files within PK3 archives.
 * pak0.pk3 contains 41 .shader files defining materials for all
 * surfaces in the game.
 *
 * Shader format example:
 *   textures/jungle/vines01
 *   {
 *       surfaceparm nonsolid
 *       surfaceparm trans
 *       cull none
 *       {
 *           map textures/jungle/vines01.tga
 *           blendfunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
 *           alphaFunc GT0
 *           rgbGen identity
 *       }
 *   }
 */

#include "../common/qcommon.h"
#include "../common/qfiles.h"
#include "tr_types.h"
#include <string.h>
#include <math.h>

/* =========================================================================
 * Shader definitions
 * ========================================================================= */

#define MAX_SHADERS         4096
#define MAX_SHADER_STAGES   8
#define MAX_SHADER_TEXT     (256 * 1024)  /* shader file text pool */

/* Blend modes */
typedef enum {
    GLS_SRCBLEND_ZERO           = 0x0001,
    GLS_SRCBLEND_ONE            = 0x0002,
    GLS_SRCBLEND_SRC_ALPHA      = 0x0004,
    GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA = 0x0008,
    GLS_SRCBLEND_DST_COLOR      = 0x0010,
    GLS_DSTBLEND_ZERO           = 0x0100,
    GLS_DSTBLEND_ONE            = 0x0200,
    GLS_DSTBLEND_SRC_ALPHA      = 0x0400,
    GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA = 0x0800,
    GLS_DSTBLEND_SRC_COLOR      = 0x1000,
    GLS_DEPTHWRITE              = 0x00010000,
    GLS_DEPTHTEST_DISABLE       = 0x00020000,
    GLS_ATEST_GT0               = 0x00100000,
    GLS_ATEST_LT128             = 0x00200000,
    GLS_ATEST_GE128             = 0x00400000,
} glState_t;

/* Texture coordinate generation */
typedef enum {
    TCGEN_IDENTITY,
    TCGEN_LIGHTMAP,
    TCGEN_TEXTURE,
    TCGEN_ENVIRONMENT_MAPPED,
    TCGEN_VECTOR
} texCoordGen_t;

/* RGB/Alpha generation */
typedef enum {
    RGBGEN_IDENTITY,
    RGBGEN_IDENTITY_LIGHTING,
    RGBGEN_VERTEX,
    RGBGEN_EXACT_VERTEX,
    RGBGEN_ENTITY,
    RGBGEN_ONE_MINUS_ENTITY,
    RGBGEN_WAVE,
    RGBGEN_LIGHTING_DIFFUSE,
    RGBGEN_CONST
} rgbGen_t;

typedef enum {
    ALPHAGEN_IDENTITY,
    ALPHAGEN_VERTEX,
    ALPHAGEN_ENTITY,
    ALPHAGEN_ONE_MINUS_ENTITY,
    ALPHAGEN_WAVE,
    ALPHAGEN_PORTAL,
    ALPHAGEN_CONST,
    ALPHAGEN_DOT,
    ALPHAGEN_ONE_MINUS_DOT
} alphaGen_t;

/* Wave functions */
typedef enum {
    GF_SIN,
    GF_SQUARE,
    GF_TRIANGLE,
    GF_SAWTOOTH,
    GF_INVERSE_SAWTOOTH,
    GF_NOISE
} genFunc_t;

typedef struct {
    genFunc_t   func;
    float       base;
    float       amplitude;
    float       phase;
    float       frequency;
} waveForm_t;

/* A single shader stage (pass) */
typedef struct {
    qboolean    active;

    /* Texture */
    qhandle_t   image;          /* GL texture handle */
    char        imageName[MAX_QPATH];
    qboolean    isLightmap;

    /* Blending */
    int         stateBits;

    /* Texture coordinate generation */
    texCoordGen_t tcGen;

    /* Color generation */
    rgbGen_t    rgbGen;
    alphaGen_t  alphaGen;
    waveForm_t  rgbWave;
    waveForm_t  alphaWave;
    float       constantColor[4];

    /* Texture animation */
    int         numAnimFrames;
    float       animFrequency;

    /* Texture coordinate transforms */
    qboolean    tcModActive;
} shaderStage_t;

/* Cull mode */
typedef enum {
    CULL_FRONT,
    CULL_BACK,
    CULL_NONE
} cullType_t;

/* A complete shader */
typedef struct shader_s {
    char            name[MAX_QPATH];
    int             index;          /* unique shader index */
    int             sortOrder;      /* rendering sort key */

    qboolean        defaultShader;  /* auto-generated from texture name */
    qboolean        isSky;
    qboolean        isPortal;

    cullType_t      cullType;
    qboolean        polygonOffset;

    int             surfaceFlags;
    int             contentFlags;

    int             numStages;
    shaderStage_t   stages[MAX_SHADER_STAGES];

    /* Sky shader data */
    char            skyboxImages[6][MAX_QPATH];
    float           skyCloudHeight;

    /* Fog */
    qboolean        fogPass;
    float           fogColor[4];

    /* FAKK2 extensions */
    qboolean        lensFlare;
    float           lensFlareColor[3];
    float           lensFlareSize;
    int             subdivisions;

    struct shader_s *next;      /* hash chain */
} shader_t;

/* =========================================================================
 * Shader registry
 * ========================================================================= */

#define SHADER_HASH_SIZE    1024

static shader_t    r_shaders[MAX_SHADERS];
static int         r_numShaders;
static shader_t    *shaderHash[SHADER_HASH_SIZE];

static int R_ShaderHash(const char *name) {
    int hash = 0;
    while (*name) {
        char c = (char)tolower((unsigned char)*name);
        if (c == '\\') c = '/';
        hash = hash * 33 + c;
        name++;
    }
    return hash & (SHADER_HASH_SIZE - 1);
}

/* =========================================================================
 * Default shader (auto-generated from texture name)
 * ========================================================================= */

static shader_t *R_CreateDefaultShader(const char *name) {
    if (r_numShaders >= MAX_SHADERS) {
        Com_Printf("R_CreateDefaultShader: MAX_SHADERS hit\n");
        return &r_shaders[0];
    }

    shader_t *sh = &r_shaders[r_numShaders];
    memset(sh, 0, sizeof(*sh));
    Q_strncpyz(sh->name, name, sizeof(sh->name));
    sh->index = r_numShaders;
    sh->defaultShader = qtrue;
    sh->cullType = CULL_FRONT;

    /* Single diffuse pass */
    sh->numStages = 1;
    sh->stages[0].active = qtrue;
    Q_strncpyz(sh->stages[0].imageName, name, sizeof(sh->stages[0].imageName));
    sh->stages[0].stateBits = GLS_DEPTHWRITE;
    sh->stages[0].tcGen = TCGEN_TEXTURE;
    sh->stages[0].rgbGen = RGBGEN_IDENTITY_LIGHTING;
    sh->stages[0].alphaGen = ALPHAGEN_IDENTITY;

    /* Hash */
    int hash = R_ShaderHash(name);
    sh->next = shaderHash[hash];
    shaderHash[hash] = sh;

    r_numShaders++;
    return sh;
}

/* =========================================================================
 * Shader text parsing
 * ========================================================================= */

static void R_ParseShaderStage(shaderStage_t *stage, char **text) {
    memset(stage, 0, sizeof(*stage));
    stage->active = qtrue;
    stage->tcGen = TCGEN_TEXTURE;
    stage->rgbGen = RGBGEN_IDENTITY;
    stage->alphaGen = ALPHAGEN_IDENTITY;

    while (1) {
        char *token = COM_ParseExt(text, qtrue);
        if (!token[0] || token[0] == '}') break;

        if (!Q_stricmp(token, "map")) {
            token = COM_ParseExt(text, qfalse);
            if (!Q_stricmp(token, "$lightmap")) {
                stage->isLightmap = qtrue;
                stage->tcGen = TCGEN_LIGHTMAP;
            } else {
                Q_strncpyz(stage->imageName, token, sizeof(stage->imageName));
            }
        }
        else if (!Q_stricmp(token, "blendfunc")) {
            token = COM_ParseExt(text, qfalse);
            if (!Q_stricmp(token, "add")) {
                stage->stateBits = GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE;
            } else if (!Q_stricmp(token, "filter")) {
                stage->stateBits = GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO;
            } else if (!Q_stricmp(token, "blend")) {
                stage->stateBits = GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;
            } else {
                /* Parse GL_SRC_xxx GL_DST_xxx pair */
                int src = 0, dst = 0;
                if (!Q_stricmp(token, "GL_ONE")) src = GLS_SRCBLEND_ONE;
                else if (!Q_stricmp(token, "GL_ZERO")) src = GLS_SRCBLEND_ZERO;
                else if (!Q_stricmp(token, "GL_SRC_ALPHA")) src = GLS_SRCBLEND_SRC_ALPHA;
                else if (!Q_stricmp(token, "GL_ONE_MINUS_SRC_ALPHA")) src = GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA;
                else if (!Q_stricmp(token, "GL_DST_COLOR")) src = GLS_SRCBLEND_DST_COLOR;

                token = COM_ParseExt(text, qfalse);
                if (!Q_stricmp(token, "GL_ONE")) dst = GLS_DSTBLEND_ONE;
                else if (!Q_stricmp(token, "GL_ZERO")) dst = GLS_DSTBLEND_ZERO;
                else if (!Q_stricmp(token, "GL_SRC_ALPHA")) dst = GLS_DSTBLEND_SRC_ALPHA;
                else if (!Q_stricmp(token, "GL_ONE_MINUS_SRC_ALPHA")) dst = GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;
                else if (!Q_stricmp(token, "GL_SRC_COLOR")) dst = GLS_DSTBLEND_SRC_COLOR;

                stage->stateBits = src | dst;
            }
        }
        else if (!Q_stricmp(token, "alphaFunc")) {
            token = COM_ParseExt(text, qfalse);
            if (!Q_stricmp(token, "GT0")) stage->stateBits |= GLS_ATEST_GT0;
            else if (!Q_stricmp(token, "LT128")) stage->stateBits |= GLS_ATEST_LT128;
            else if (!Q_stricmp(token, "GE128")) stage->stateBits |= GLS_ATEST_GE128;
        }
        else if (!Q_stricmp(token, "rgbGen")) {
            token = COM_ParseExt(text, qfalse);
            if (!Q_stricmp(token, "identity")) stage->rgbGen = RGBGEN_IDENTITY;
            else if (!Q_stricmp(token, "identityLighting")) stage->rgbGen = RGBGEN_IDENTITY_LIGHTING;
            else if (!Q_stricmp(token, "vertex")) stage->rgbGen = RGBGEN_VERTEX;
            else if (!Q_stricmp(token, "exactVertex")) stage->rgbGen = RGBGEN_EXACT_VERTEX;
            else if (!Q_stricmp(token, "entity")) stage->rgbGen = RGBGEN_ENTITY;
            else if (!Q_stricmp(token, "lightingDiffuse")) stage->rgbGen = RGBGEN_LIGHTING_DIFFUSE;
        }
        else if (!Q_stricmp(token, "alphaGen")) {
            token = COM_ParseExt(text, qfalse);
            if (!Q_stricmp(token, "vertex")) stage->alphaGen = ALPHAGEN_VERTEX;
            else if (!Q_stricmp(token, "entity")) stage->alphaGen = ALPHAGEN_ENTITY;
            else if (!Q_stricmp(token, "portal")) stage->alphaGen = ALPHAGEN_PORTAL;
            else if (!Q_stricmp(token, "dot")) stage->alphaGen = ALPHAGEN_DOT;
            else if (!Q_stricmp(token, "oneMinusDot")) stage->alphaGen = ALPHAGEN_ONE_MINUS_DOT;
        }
        else if (!Q_stricmp(token, "tcGen")) {
            token = COM_ParseExt(text, qfalse);
            if (!Q_stricmp(token, "environment")) stage->tcGen = TCGEN_ENVIRONMENT_MAPPED;
            else if (!Q_stricmp(token, "lightmap")) stage->tcGen = TCGEN_LIGHTMAP;
        }
        else if (!Q_stricmp(token, "depthWrite")) {
            stage->stateBits |= GLS_DEPTHWRITE;
        }
    }
}

static shader_t *R_ParseShader(const char *name, char **text) {
    if (r_numShaders >= MAX_SHADERS) {
        Com_Printf("R_ParseShader: MAX_SHADERS\n");
        return &r_shaders[0];
    }

    shader_t *sh = &r_shaders[r_numShaders];
    memset(sh, 0, sizeof(*sh));
    Q_strncpyz(sh->name, name, sizeof(sh->name));
    sh->index = r_numShaders;
    sh->cullType = CULL_FRONT;

    while (1) {
        char *token = COM_ParseExt(text, qtrue);
        if (!token[0] || token[0] == '}') break;

        /* Stage (opening brace) */
        if (token[0] == '{') {
            if (sh->numStages < MAX_SHADER_STAGES) {
                R_ParseShaderStage(&sh->stages[sh->numStages], text);
                sh->numStages++;
            } else {
                /* Skip stage */
                int depth = 1;
                while (depth > 0) {
                    token = COM_ParseExt(text, qtrue);
                    if (token[0] == '{') depth++;
                    else if (token[0] == '}') depth--;
                    else if (!token[0]) break;
                }
            }
        }
        /* General directives */
        else if (!Q_stricmp(token, "cull")) {
            token = COM_ParseExt(text, qfalse);
            if (!Q_stricmp(token, "none") || !Q_stricmp(token, "twosided") ||
                !Q_stricmp(token, "disable")) {
                sh->cullType = CULL_NONE;
            } else if (!Q_stricmp(token, "front")) {
                sh->cullType = CULL_FRONT;
            } else if (!Q_stricmp(token, "back") || !Q_stricmp(token, "backside") ||
                       !Q_stricmp(token, "backsided")) {
                sh->cullType = CULL_BACK;
            }
        }
        else if (!Q_stricmp(token, "surfaceparm")) {
            token = COM_ParseExt(text, qfalse);
            if (!Q_stricmp(token, "sky")) sh->isSky = qtrue;
            else if (!Q_stricmp(token, "nodraw")) sh->surfaceFlags |= SURF_NODRAW;
            else if (!Q_stricmp(token, "nonsolid")) sh->surfaceFlags |= SURF_NONSOLID;
            else if (!Q_stricmp(token, "noimpact")) sh->surfaceFlags |= SURF_NOIMPACT;
            else if (!Q_stricmp(token, "nomarks")) sh->surfaceFlags |= SURF_NOMARKS;
            else if (!Q_stricmp(token, "nolightmap")) sh->surfaceFlags |= SURF_NOLIGHTMAP;
            /* UberTools surface materials */
            else if (!Q_stricmp(token, "wood")) sh->surfaceFlags |= SURF_WOOD;
            else if (!Q_stricmp(token, "rock")) sh->surfaceFlags |= SURF_ROCK;
            else if (!Q_stricmp(token, "glass")) sh->surfaceFlags |= SURF_GLASS;
            else if (!Q_stricmp(token, "metal")) sh->surfaceFlags |= SURF_METALSTEPS;
            else if (!Q_stricmp(token, "dirt")) sh->surfaceFlags |= SURF_DIRT;
            else if (!Q_stricmp(token, "grass")) sh->surfaceFlags |= SURF_GRASS;
            else if (!Q_stricmp(token, "sand")) sh->surfaceFlags |= SURF_SAND;
            else if (!Q_stricmp(token, "gravel")) sh->surfaceFlags |= SURF_GRAVEL;
            else if (!Q_stricmp(token, "mud")) sh->surfaceFlags |= SURF_MUD;
            else if (!Q_stricmp(token, "paper")) sh->surfaceFlags |= SURF_PAPER;
        }
        else if (!Q_stricmp(token, "skyparms")) {
            sh->isSky = qtrue;
            token = COM_ParseExt(text, qfalse);  /* skybox base name or "-" */
            if (Q_stricmp(token, "-")) {
                char base[MAX_QPATH];
                Q_strncpyz(base, token, sizeof(base));
                static const char *suf[6] = { "_rt", "_lf", "_up", "_dn", "_bk", "_ft" };
                for (int i = 0; i < 6; i++) {
                    snprintf(sh->skyboxImages[i], MAX_QPATH, "%s%s", base, suf[i]);
                }
            }
            token = COM_ParseExt(text, qfalse);  /* cloud height */
            sh->skyCloudHeight = (float)atof(token);
            COM_ParseExt(text, qfalse);           /* inner box (usually "-") */
        }
        else if (!Q_stricmp(token, "polygonOffset")) {
            sh->polygonOffset = qtrue;
        }
        else if (!Q_stricmp(token, "portal")) {
            sh->isPortal = qtrue;
            sh->sortOrder = 1;
        }
        else if (!Q_stricmp(token, "sort")) {
            token = COM_ParseExt(text, qfalse);
            sh->sortOrder = atoi(token);
        }
        /* FAKK2 extensions */
        else if (!Q_stricmp(token, "lensflare")) {
            sh->lensFlare = qtrue;
        }
        else if (!Q_stricmp(token, "subdivisions")) {
            token = COM_ParseExt(text, qfalse);
            sh->subdivisions = atoi(token);
        }
    }

    /* Hash */
    int hash = R_ShaderHash(name);
    sh->next = shaderHash[hash];
    shaderHash[hash] = sh;

    r_numShaders++;
    return sh;
}

/* =========================================================================
 * Shader file loading
 *
 * All .shader files are loaded from the filesystem and parsed.
 * Each file contains multiple shader definitions.
 * ========================================================================= */

static void R_LoadShaderFile(const char *filename) {
    void *buffer;
    long len = FS_ReadFile(filename, &buffer);
    if (len <= 0 || !buffer) return;

    char *text = (char *)buffer;
    int count = 0;

    while (1) {
        char *token = COM_ParseExt(&text, qtrue);
        if (!token[0]) break;

        /* Token is the shader name */
        char shaderName[MAX_QPATH];
        Q_strncpyz(shaderName, token, sizeof(shaderName));

        /* Expect opening brace */
        token = COM_ParseExt(&text, qtrue);
        if (token[0] != '{') {
            Com_Printf("R_LoadShaderFile: expected '{' for shader '%s' in %s\n",
                        shaderName, filename);
            break;
        }

        R_ParseShader(shaderName, &text);
        count++;
    }

    FS_FreeFile(buffer);
    Com_DPrintf("  %s: %d shaders\n", filename, count);
}

static void R_LoadAllShaderFiles(void) {
    int numFiles;
    char **files = FS_ListFiles("scripts", ".shader", &numFiles);

    if (files) {
        Com_Printf("Loading %d shader files...\n", numFiles);
        for (int i = 0; i < numFiles; i++) {
            char path[MAX_QPATH];
            snprintf(path, sizeof(path), "scripts/%s", files[i]);
            R_LoadShaderFile(path);
        }
        FS_FreeFileList(files);
    }

    Com_Printf("Loaded %d shaders total\n", r_numShaders);
}

/* =========================================================================
 * Public shader API
 * ========================================================================= */

qhandle_t R_FindShader(const char *name) {
    if (!name || !name[0]) return 0;

    /* Check hash table */
    int hash = R_ShaderHash(name);
    for (shader_t *sh = shaderHash[hash]; sh; sh = sh->next) {
        if (!Q_stricmp(name, sh->name)) {
            return sh->index;
        }
    }

    /* Not found -- create default shader from texture name */
    shader_t *sh = R_CreateDefaultShader(name);
    return sh->index;
}

shader_t *R_GetShaderByHandle(qhandle_t h) {
    if (h < 0 || h >= r_numShaders) return &r_shaders[0];
    return &r_shaders[h];
}

/* =========================================================================
 * Initialization
 * ========================================================================= */

void R_InitShaders(void) {
    Com_Printf("--- R_InitShaders ---\n");

    memset(r_shaders, 0, sizeof(r_shaders));
    memset(shaderHash, 0, sizeof(shaderHash));
    r_numShaders = 0;

    /* Create shader 0 as the default/error shader */
    R_CreateDefaultShader("<default>");

    /* Load shader files from PK3 archives */
    R_LoadAllShaderFiles();
}

void R_ShutdownShaders(void) {
    memset(r_shaders, 0, sizeof(r_shaders));
    memset(shaderHash, 0, sizeof(shaderHash));
    r_numShaders = 0;
}
