/*
 * cm_local.h -- Collision model internal header
 *
 * FAKK2 collision uses BSP-based clipping (inherited from id Tech 3).
 * The collision model is loaded from the same BSP file as the renderer
 * but keeps its own copy of planes, brushes, and brush sides for
 * independent trace operations.
 *
 * Key differences from Q3:
 *   - cylinder trace support (for third-person character collision)
 *   - FAKK2 content flags (CONTENTS_WEAPONCLIP, etc.)
 *   - UberTools surface material flags (wood, glass, rock, etc.)
 */

#ifndef CM_LOCAL_H
#define CM_LOCAL_H

#include "../common/qcommon.h"
#include "../common/qfiles.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Collision model limits
 * ========================================================================= */

#define MAX_SUBMODELS       256
#define MAX_CM_PLANES       65536
#define MAX_CM_BRUSHSIDES   65536
#define MAX_CM_BRUSHES      8192
#define MAX_CM_LEAFS        65536
#define MAX_CM_LEAFBRUSHES  65536
#define MAX_CM_NODES        65536
#define MAX_CM_SHADERS      1024
#define MAX_CM_AREAS        256
#define MAX_CM_AREAPORTALS  1024
#define MAX_CM_PATCHES      4096

#define BOX_MODEL_HANDLE    255     /* special handle for CM_TempBoxModel */

/* =========================================================================
 * Internal collision structures
 * ========================================================================= */

typedef struct {
    float   normal[3];
    float   dist;
    byte    type;           /* axial plane type (0-2) or 3 for non-axial */
    byte    signbits;       /* sign of each normal component */
    byte    pad[2];
} cplane_t;

typedef struct {
    cplane_t    *plane;
    int         surfaceFlags;
    int         shaderNum;
} cbrushside_t;

typedef struct {
    int         shaderNum;
    int         contentFlags;
    int         surfaceFlags;
} cshader_t;

typedef struct {
    int         numSides;
    cbrushside_t *sides;
    int         contentFlags;
    qboolean    checkcount;     /* avoid checking same brush twice per trace */
} cbrush_t;

typedef struct {
    int         cluster;
    int         area;
    int         firstLeafBrush;
    int         numLeafBrushes;
} cleaf_t;

typedef struct {
    int         planeNum;
    int         children[2];    /* negative = -(leaf+1) */
} cnode_t;

typedef struct {
    float       mins[3];
    float       maxs[3];
    int         firstBrush;
    int         numBrushes;
    cleaf_t     leaf;           /* for inline models */
} cmodel_t;

typedef struct {
    int         floodnum;
    int         floodvalid;
} carea_t;

/* =========================================================================
 * Collision map state
 * ========================================================================= */

typedef struct {
    char        name[MAX_QPATH];
    qboolean    loaded;

    int         numShaders;
    cshader_t   shaders[MAX_CM_SHADERS];

    int         numPlanes;
    cplane_t    *planes;

    int         numNodes;
    cnode_t     *nodes;

    int         numLeafs;
    cleaf_t     *leafs;

    int         numLeafBrushes;
    int         *leafBrushes;

    int         numBrushSides;
    cbrushside_t *brushSides;

    int         numBrushes;
    cbrush_t    *brushes;

    int         numSubModels;
    cmodel_t    subModels[MAX_SUBMODELS];

    int         numClusters;
    int         clusterBytes;
    byte        *visibility;

    int         numAreas;
    carea_t     areas[MAX_CM_AREAS];
    qboolean    areaPortals[MAX_CM_AREAS][MAX_CM_AREAS];

    int         numEntityChars;
    char        *entityString;

    int         checkcount;     /* for avoiding double-checking brushes */
} clipMap_t;

/* =========================================================================
 * Public collision model API
 * ========================================================================= */

void        CM_Init(void);
void        CM_Shutdown(void);
void        CM_LoadMap(const char *name);
void        CM_ClearMap(void);

int         CM_NumInlineModels(void);
clipHandle_t CM_InlineModel(int index);
clipHandle_t CM_TempBoxModel(const vec3_t mins, const vec3_t maxs, int contents);

int         CM_PointContents(const vec3_t p, clipHandle_t model);
int         CM_TransformedPointContents(const vec3_t p, clipHandle_t model,
                                        const vec3_t origin, const vec3_t angles);

void        CM_BoxTrace(trace_t *results, const vec3_t start, const vec3_t end,
                        const vec3_t mins, const vec3_t maxs, clipHandle_t model,
                        int brushmask, qboolean cylinder);
void        CM_TransformedBoxTrace(trace_t *results, const vec3_t start, const vec3_t end,
                                   const vec3_t mins, const vec3_t maxs, clipHandle_t model,
                                   int brushmask, const vec3_t origin, const vec3_t angles,
                                   qboolean cylinder);

int         CM_MarkFragments(int numPoints, const vec3_t *points, const vec3_t projection,
                             int maxPoints, vec3_t pointBuffer, int maxFragments,
                             markFragment_t *fragmentBuffer);

qboolean    CM_InPVS(vec3_t p1, vec3_t p2);
qboolean    CM_InPVSIgnorePortals(vec3_t p1, vec3_t p2);
void        CM_AdjustAreaPortalState(int area1, int area2, qboolean open);
qboolean    CM_AreasConnected(int area1, int area2);

int         CM_PointLeafnum(const vec3_t p);
int         CM_LeafCluster(int leafnum);
int         CM_LeafArea(int leafnum);

const char  *CM_EntityString(void);

#ifdef __cplusplus
}
#endif

#endif /* CM_LOCAL_H */
