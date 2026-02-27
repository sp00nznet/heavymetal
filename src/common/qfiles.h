/*
 * qfiles.h -- Binary file format definitions
 *
 * Defines the on-disk structures for FAKK2's binary file formats:
 *   - BSP world maps (magic "FAKK", version 12, 20 lumps)
 *   - TIKI binary model data (skeletons, animations)
 *
 * Based on the FAKK2 SDK source/qcommon/qfiles.h.
 * FAKK2's BSP format extends Q3 BSP with 3 additional lumps for
 * enhanced lighting (ENTLIGHTS, ENTLIGHTSVIS, LIGHTDEFS).
 */

#ifndef QFILES_H
#define QFILES_H

#include "fakk_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * BSP file format -- FAKK2 world maps
 *
 * Header: "FAKK" (0x464B414B) + version 12 + checksum + 20 lump entries
 * Extended from Q3 BSP (17 lumps) with entity lights and light definitions.
 * ========================================================================= */

#define BSP_IDENT           0x464B414B  /* "FAKK" in little-endian */
#define BSP_FAKK_VERSION    12

/* Lump IDs -- 20 lumps total (Q3 has 17) */
#define LUMP_SHADERS        0
#define LUMP_PLANES         1
#define LUMP_LIGHTMAPS      2
#define LUMP_SURFACES       3
#define LUMP_DRAWVERTS      4
#define LUMP_DRAWINDEXES    5
#define LUMP_LEAFBRUSHES    6
#define LUMP_LEAFSURFACES   7
#define LUMP_LEAFS          8
#define LUMP_NODES          9
#define LUMP_BRUSHSIDES     10
#define LUMP_BRUSHES        11
#define LUMP_FOGS           12
#define LUMP_MODELS         13
#define LUMP_ENTITIES       14
#define LUMP_VISIBILITY     15
#define LUMP_LIGHTGRID      16
/* FAKK2 extensions beyond Q3 */
#define LUMP_ENTLIGHTS      17
#define LUMP_ENTLIGHTSVIS   18
#define LUMP_LIGHTDEFS      19
#define HEADER_LUMPS        20

/* Lump directory entry */
typedef struct {
    int     fileofs;
    int     filelen;
} lump_t;

/* BSP file header */
typedef struct {
    int     ident;              /* BSP_IDENT (0x464B414B) */
    int     version;            /* BSP_FAKK_VERSION (12) */
    int     checksum;           /* map checksum for multiplayer validation */
    lump_t  lumps[HEADER_LUMPS];
} dheader_t;

/* =========================================================================
 * BSP data structures
 * ========================================================================= */

/* Inline model (worldspawn + brush entities) */
typedef struct {
    float   mins[3], maxs[3];
    int     firstSurface;
    int     numSurfaces;
    int     firstBrush;
    int     numBrushes;
} dmodel_t;

/* Shader reference (76 bytes -- larger than Q3's 72 due to subdivisions) */
typedef struct {
    char    shader[MAX_QPATH];      /* shader name */
    int     surfaceFlags;
    int     contentFlags;
    int     subdivisions;           /* FAKK2 addition: terrain subdivision */
} dshader_t;

/* Plane */
typedef struct {
    float   normal[3];
    float   dist;
} dplane_t;

/* BSP node */
typedef struct {
    int     planeNum;
    int     children[2];            /* negative = -(leafnum + 1) */
    int     mins[3];
    int     maxs[3];
} dnode_t;

/* BSP leaf */
typedef struct {
    int     cluster;                /* -1 = opaque cluster (no vis) */
    int     area;
    int     mins[3];
    int     maxs[3];
    int     firstLeafSurface;
    int     numLeafSurfaces;
    int     firstLeafBrush;
    int     numLeafBrushes;
} dleaf_t;

/* Brush side */
typedef struct {
    int     planeNum;
    int     shaderNum;
} dbrushside_t;

/* Brush */
typedef struct {
    int     firstSide;
    int     numSides;
    int     shaderNum;
} dbrush_t;

/* Fog volume */
typedef struct {
    char    shader[MAX_QPATH];
    int     brushNum;
    int     visibleSide;
} dfog_t;

/* Draw vertex */
typedef struct {
    vec3_t  xyz;
    float   st[2];                  /* texture coordinates */
    float   lightmap[2];            /* lightmap coordinates */
    vec3_t  normal;
    byte    color[4];               /* vertex color RGBA */
} drawVert_t;

/* Surface types */
typedef enum {
    MST_BAD,
    MST_PLANAR,
    MST_PATCH,
    MST_TRIANGLE_SOUP,
    MST_FLARE,
    MST_TERRAIN                     /* FAKK2 addition */
} mapSurfaceType_t;

/* Draw surface */
typedef struct {
    int     shaderNum;
    int     fogNum;
    int     surfaceType;            /* mapSurfaceType_t */

    int     firstVert;
    int     numVerts;

    int     firstIndex;
    int     numIndexes;

    int     lightmapNum;
    int     lightmapX, lightmapY;
    int     lightmapWidth, lightmapHeight;

    vec3_t  lightmapOrigin;
    vec3_t  lightmapVecs[3];        /* world-space s/t/normal vectors */

    int     patchWidth;
    int     patchHeight;

    float   subdivisions;           /* FAKK2: per-surface subdivision level */
} dsurface_t;

/* Visibility data header */
typedef struct {
    int     numClusters;
    int     clusterBytes;
    /* byte data[numClusters * clusterBytes] follows */
} dvis_t;

/* =========================================================================
 * FAKK2 light definitions (lumps 17-19)
 * ========================================================================= */

/* Entity light (LUMP_ENTLIGHTS) */
typedef struct {
    vec3_t  origin;
    float   intensity;
    vec3_t  color;
    int     style;
} dentlight_t;

/* Light definition (LUMP_LIGHTDEFS) -- per-shader lighting properties */
typedef struct {
    int     lightIntensity;
    int     lightAngle;
    int     lightmapResolution;
    qboolean twoSided;
    qboolean lightLinear;
    vec3_t  lightColor;
    float   lightFalloff;
    float   backsplashFraction;
    float   backsplashDistance;
    float   lightSubdivide;
    qboolean autosprite;
} dlightdef_t;

/* =========================================================================
 * TIKI binary format idents
 * ========================================================================= */

#define TIKI_IDENT          0x49 /* 'I' -- part of "TIKI" text header */
#define TIKI_ANIM_IDENT     0x54414E20  /* "TAN " */
#define TIKI_ANIM_VERSION   2
#define TIKI_MAX_FRAMES     2048
#define TIKI_MAX_TRIANGLES  4096
#define TIKI_MAX_VERTS      1200
#define TIKI_MAX_TAGS       16

/* =========================================================================
 * Skeletal model binary structures (.skb files)
 * ========================================================================= */

typedef struct {
    vec3_t  bounds[2];          /* frame AABB */
    vec3_t  scale;              /* multiply by this */
    vec3_t  offset;             /* and add by this */
    vec3_t  delta;              /* movement delta */
    float   radius;
    float   frametime;
} tikiFrame_t;

#ifdef __cplusplus
}
#endif

#endif /* QFILES_H */
