/*
 * tiki_skel.c -- Skeletal model and animation binary loaders
 *
 * Loads .skb (skeletal model) and .ska (skeletal animation) binary files.
 *
 * .skb format (from SDK qfiles.h):
 *   skelHeader_t: ident "SKL ", version 3, name, bone/surface counts + offsets
 *   skelBoneName_t[]: parent index, flags, name (64 chars)
 *   skelSurface_t[]: linked list of surfaces, each with:
 *     skelTriangle_t[]: 3 vertex indices per triangle
 *     skelVertex_t[]: normal, texcoords, variable bone weights
 *     int collapse[]: LOD collapse map
 *
 * .ska format:
 *   skelAnimHeader_t: ident "SKAN", version 3, frame/bone counts, timing
 *   skelAnimFrame_t[]: per-frame bounds + compressed bone transforms
 *     skelBone_t[]: quaternion + offset compressed to 16-bit shorts
 *
 * Bone decompression:
 *   quat[i] = shortQuat[i] * TIKI_BONE_QUAT_MULTIPLIER_RECIPROCAL
 *   offset[i] = shortOffset[i] * TIKI_BONE_OFFSET_MULTIPLIER_RECIPROCAL
 *
 * TIKI_SKEL_MAXBONES = 100 per skeleton
 */

#include "tiki.h"
#include "../common/qcommon.h"
#include "../common/qfiles.h"

#include <string.h>

/* =========================================================================
 * Runtime skeleton representation
 * ========================================================================= */

typedef struct {
    char            name[MAX_QPATH];
    int             numBones;
    skelBoneName_t  bones[TIKI_SKEL_MAXBONES];

    int             numSurfaces;
    /* Surface data kept as raw loaded buffer for mesh rendering */
    byte            *surfaceData;
    int             surfaceDataSize;

    qboolean        loaded;
} skeleton_t;

/* =========================================================================
 * Runtime animation representation
 * ========================================================================= */

typedef struct {
    char    name[MAX_QPATH];
    int     type;
    int     numFrames;
    int     numBones;
    float   totalTime;
    float   frameTime;
    vec3_t  totalDelta;

    /* Raw frame data buffer (variable-sized frames with compressed bones) */
    byte    *frameData;
    int     frameDataSize;
    int     frameStride;        /* bytes per frame */

    qboolean loaded;
} skelAnim_t;

/* =========================================================================
 * Caches
 * ========================================================================= */

#define MAX_SKELETONS   128
#define MAX_SKEL_ANIMS  512

static skeleton_t   skel_cache[MAX_SKELETONS];
static int          skel_count;

static skelAnim_t   anim_cache[MAX_SKEL_ANIMS];
static int          anim_count;

/* =========================================================================
 * Endian helpers (FAKK2 data is little-endian, same as x86)
 * ========================================================================= */

static int LittleLong(int val) { return val; }
static float LittleFloat(float val) { return val; }
static short LittleShort(short val) { return val; }

/* =========================================================================
 * TIKI_LoadSkeleton -- load a .skb skeletal model file
 * ========================================================================= */

skeleton_t *TIKI_LoadSkeleton(const char *filename) {
    /* Check cache */
    for (int i = 0; i < skel_count; i++) {
        if (skel_cache[i].loaded && !Q_stricmp(skel_cache[i].name, filename))
            return &skel_cache[i];
    }

    if (skel_count >= MAX_SKELETONS) {
        Com_Printf("TIKI_LoadSkeleton: cache full\n");
        return NULL;
    }

    void *buffer;
    long len = FS_ReadFile(filename, &buffer);
    if (len < 0 || !buffer) {
        Com_DPrintf("TIKI_LoadSkeleton: couldn't load '%s'\n", filename);
        return NULL;
    }

    const byte *data = (const byte *)buffer;
    const skelHeader_t *header = (const skelHeader_t *)data;

    /* Validate header */
    int ident = LittleLong(header->ident);
    int version = LittleLong(header->version);

    if (ident != TIKI_SKEL_IDENT) {
        Com_Printf("TIKI_LoadSkeleton: '%s' wrong ident (0x%08X, expected 0x%08X)\n",
                   filename, ident, TIKI_SKEL_IDENT);
        FS_FreeFile(buffer);
        return NULL;
    }

    if (version != TIKI_SKEL_VERSION) {
        Com_Printf("TIKI_LoadSkeleton: '%s' wrong version (%d, expected %d)\n",
                   filename, version, TIKI_SKEL_VERSION);
        FS_FreeFile(buffer);
        return NULL;
    }

    int numBones = LittleLong(header->numBones);
    int numSurfaces = LittleLong(header->numSurfaces);
    int ofsBones = LittleLong(header->ofsBones);
    int ofsSurfaces = LittleLong(header->ofsSurfaces);
    int ofsEnd = LittleLong(header->ofsEnd);

    if (numBones > TIKI_SKEL_MAXBONES) {
        Com_Printf("TIKI_LoadSkeleton: '%s' too many bones (%d, max %d)\n",
                   filename, numBones, TIKI_SKEL_MAXBONES);
        FS_FreeFile(buffer);
        return NULL;
    }

    /* Populate skeleton */
    skeleton_t *skel = &skel_cache[skel_count];
    memset(skel, 0, sizeof(*skel));
    Q_strncpyz(skel->name, filename, sizeof(skel->name));
    skel->numBones = numBones;
    skel->numSurfaces = numSurfaces;

    /* Read bone names */
    const skelBoneName_t *srcBones = (const skelBoneName_t *)(data + ofsBones);
    for (int i = 0; i < numBones; i++) {
        skel->bones[i].parent = LittleLong(srcBones[i].parent);
        skel->bones[i].flags = LittleLong(srcBones[i].flags);
        Q_strncpyz(skel->bones[i].name, srcBones[i].name,
                   sizeof(skel->bones[i].name));
    }

    /* Keep raw surface data for mesh rendering later */
    if (ofsSurfaces > 0 && ofsEnd > ofsSurfaces) {
        skel->surfaceDataSize = ofsEnd - ofsSurfaces;
        skel->surfaceData = (byte *)Z_TagMalloc(skel->surfaceDataSize, TAG_TIKI);
        memcpy(skel->surfaceData, data + ofsSurfaces, skel->surfaceDataSize);
    }

    skel->loaded = qtrue;
    skel_count++;

    Com_DPrintf("TIKI_LoadSkeleton: '%s' -- %d bones, %d surfaces\n",
               filename, numBones, numSurfaces);

    /* Print bone hierarchy */
    for (int i = 0; i < numBones; i++) {
        Com_DPrintf("  bone[%d]: '%s' parent=%d flags=0x%x\n",
                   i, skel->bones[i].name, skel->bones[i].parent,
                   skel->bones[i].flags);
    }

    FS_FreeFile(buffer);
    return skel;
}

/* =========================================================================
 * TIKI_LoadSkelAnim -- load a .ska skeletal animation file
 * ========================================================================= */

skelAnim_t *TIKI_LoadSkelAnim(const char *filename) {
    /* Check cache */
    for (int i = 0; i < anim_count; i++) {
        if (anim_cache[i].loaded && !Q_stricmp(anim_cache[i].name, filename))
            return &anim_cache[i];
    }

    if (anim_count >= MAX_SKEL_ANIMS) {
        Com_Printf("TIKI_LoadSkelAnim: cache full\n");
        return NULL;
    }

    void *buffer;
    long len = FS_ReadFile(filename, &buffer);
    if (len < 0 || !buffer) {
        Com_DPrintf("TIKI_LoadSkelAnim: couldn't load '%s'\n", filename);
        return NULL;
    }

    const byte *data = (const byte *)buffer;
    const skelAnimHeader_t *header = (const skelAnimHeader_t *)data;

    int ident = LittleLong(header->ident);
    int version = LittleLong(header->version);

    if (ident != TIKI_SKEL_ANIM_IDENT) {
        Com_Printf("TIKI_LoadSkelAnim: '%s' wrong ident (0x%08X, expected 0x%08X)\n",
                   filename, ident, TIKI_SKEL_ANIM_IDENT);
        FS_FreeFile(buffer);
        return NULL;
    }

    if (version != TIKI_SKEL_VERSION) {
        Com_Printf("TIKI_LoadSkelAnim: '%s' wrong version (%d, expected %d)\n",
                   filename, version, TIKI_SKEL_VERSION);
        FS_FreeFile(buffer);
        return NULL;
    }

    int numFrames = LittleLong(header->numFrames);
    int numBones = LittleLong(header->numBones);
    int ofsFrames = LittleLong(header->ofsFrames);

    if (numBones > TIKI_SKEL_MAXBONES) {
        Com_Printf("TIKI_LoadSkelAnim: '%s' too many bones (%d)\n",
                   filename, numBones);
        FS_FreeFile(buffer);
        return NULL;
    }

    /* Populate animation */
    skelAnim_t *anim = &anim_cache[anim_count];
    memset(anim, 0, sizeof(*anim));
    Q_strncpyz(anim->name, filename, sizeof(anim->name));
    anim->type = LittleLong(header->type);
    anim->numFrames = numFrames;
    anim->numBones = numBones;
    anim->totalTime = LittleFloat(header->totalTime);
    anim->frameTime = LittleFloat(header->frameTime);
    anim->totalDelta[0] = LittleFloat(header->totalDelta[0]);
    anim->totalDelta[1] = LittleFloat(header->totalDelta[1]);
    anim->totalDelta[2] = LittleFloat(header->totalDelta[2]);

    /* Frame stride: bounds(24) + radius(4) + delta(12) + bones(numBones * 16) */
    anim->frameStride = sizeof(vec3_t) * 2 + sizeof(float) + sizeof(vec3_t)
                        + numBones * sizeof(skelBone_t);

    /* Copy frame data */
    int frameDataSize = numFrames * anim->frameStride;
    if (ofsFrames > 0 && frameDataSize > 0) {
        anim->frameData = (byte *)Z_TagMalloc(frameDataSize, TAG_TIKI);
        memcpy(anim->frameData, data + ofsFrames, frameDataSize);
        anim->frameDataSize = frameDataSize;
    }

    anim->loaded = qtrue;
    anim_count++;

    Com_DPrintf("TIKI_LoadSkelAnim: '%s' -- %d frames, %d bones, %.2fs total\n",
               filename, numFrames, numBones, anim->totalTime);

    FS_FreeFile(buffer);
    return anim;
}

/* =========================================================================
 * Bone decompression -- expand 16-bit compressed bone data to floats
 * ========================================================================= */

void TIKI_DecompressBone(const skelBone_t *compressed,
                         float *quat, float *offset) {
    quat[0] = (float)LittleShort(compressed->shortQuat[0])
              * TIKI_BONE_QUAT_MULTIPLIER_RECIPROCAL;
    quat[1] = (float)LittleShort(compressed->shortQuat[1])
              * TIKI_BONE_QUAT_MULTIPLIER_RECIPROCAL;
    quat[2] = (float)LittleShort(compressed->shortQuat[2])
              * TIKI_BONE_QUAT_MULTIPLIER_RECIPROCAL;
    quat[3] = (float)LittleShort(compressed->shortQuat[3])
              * TIKI_BONE_QUAT_MULTIPLIER_RECIPROCAL;

    offset[0] = (float)LittleShort(compressed->shortOffset[0])
                * TIKI_BONE_OFFSET_MULTIPLIER_RECIPROCAL;
    offset[1] = (float)LittleShort(compressed->shortOffset[1])
                * TIKI_BONE_OFFSET_MULTIPLIER_RECIPROCAL;
    offset[2] = (float)LittleShort(compressed->shortOffset[2])
                * TIKI_BONE_OFFSET_MULTIPLIER_RECIPROCAL;
}

/* =========================================================================
 * Get a specific animation frame's bone transforms
 * ========================================================================= */

const skelAnimFrame_t *TIKI_GetAnimFrame(const skelAnim_t *anim, int framenum) {
    if (!anim || !anim->frameData) return NULL;
    if (framenum < 0 || framenum >= anim->numFrames) return NULL;
    return (const skelAnimFrame_t *)(anim->frameData + framenum * anim->frameStride);
}

/* =========================================================================
 * Surface iteration helpers for the renderer
 * ========================================================================= */

const skelSurface_t *TIKI_GetFirstSurface(const skeleton_t *skel) {
    if (!skel || !skel->surfaceData) return NULL;
    return (const skelSurface_t *)skel->surfaceData;
}

const skelSurface_t *TIKI_GetNextSurface(const skelSurface_t *surf) {
    if (!surf) return NULL;
    int ofsEnd = LittleLong(surf->ofsEnd);
    if (ofsEnd <= 0) return NULL;
    return (const skelSurface_t *)((const byte *)surf + ofsEnd);
}

int TIKI_GetSurfaceTriangleCount(const skelSurface_t *surf) {
    return surf ? LittleLong(surf->numTriangles) : 0;
}

int TIKI_GetSurfaceVertexCount(const skelSurface_t *surf) {
    return surf ? LittleLong(surf->numVerts) : 0;
}

const skelTriangle_t *TIKI_GetSurfaceTriangles(const skelSurface_t *surf) {
    if (!surf) return NULL;
    int ofs = LittleLong(surf->ofsTriangles);
    return (const skelTriangle_t *)((const byte *)surf + ofs);
}

/* =========================================================================
 * Skeleton queries (called from tiki_main.c / tiki_anim.c)
 * ========================================================================= */

int TIKI_SkeletonNumBones(const char *skelname) {
    skeleton_t *skel = TIKI_LoadSkeleton(skelname);
    return skel ? skel->numBones : 0;
}

int TIKI_SkeletonBoneIndex(const char *skelname, const char *bonename) {
    skeleton_t *skel = TIKI_LoadSkeleton(skelname);
    if (!skel) return -1;

    for (int i = 0; i < skel->numBones; i++) {
        if (!Q_stricmp(skel->bones[i].name, bonename))
            return i;
    }
    return -1;
}

const char *TIKI_SkeletonBoneName(const char *skelname, int boneindex) {
    skeleton_t *skel = TIKI_LoadSkeleton(skelname);
    if (!skel || boneindex < 0 || boneindex >= skel->numBones)
        return "";
    return skel->bones[boneindex].name;
}

int TIKI_SkeletonNumSurfaces(const char *skelname) {
    skeleton_t *skel = TIKI_LoadSkeleton(skelname);
    return skel ? skel->numSurfaces : 0;
}

/* =========================================================================
 * Animation queries -- used by tiki_anim.c to get real data
 * ========================================================================= */

int TIKI_SkelAnimNumFrames(const char *filename) {
    skelAnim_t *anim = TIKI_LoadSkelAnim(filename);
    return anim ? anim->numFrames : 0;
}

float TIKI_SkelAnimTotalTime(const char *filename) {
    skelAnim_t *anim = TIKI_LoadSkelAnim(filename);
    return anim ? anim->totalTime : 0.0f;
}

float TIKI_SkelAnimFrameTime(const char *filename) {
    skelAnim_t *anim = TIKI_LoadSkelAnim(filename);
    return anim ? anim->frameTime : 0.0f;
}

void TIKI_SkelAnimTotalDelta(const char *filename, vec3_t delta) {
    skelAnim_t *anim = TIKI_LoadSkelAnim(filename);
    if (anim) {
        VectorCopy(anim->totalDelta, delta);
    } else {
        VectorClear(delta);
    }
}

void TIKI_SkelAnimFrameDelta(const char *filename, int framenum, vec3_t delta) {
    skelAnim_t *anim = TIKI_LoadSkelAnim(filename);
    if (!anim || !anim->frameData) {
        VectorClear(delta);
        return;
    }

    const skelAnimFrame_t *frame = TIKI_GetAnimFrame(anim, framenum);
    if (frame) {
        VectorCopy(frame->delta, delta);
    } else {
        VectorClear(delta);
    }
}

void TIKI_SkelAnimFrameBounds(const char *filename, int framenum,
                               vec3_t mins, vec3_t maxs) {
    skelAnim_t *anim = TIKI_LoadSkelAnim(filename);
    if (!anim || !anim->frameData) {
        VectorSet(mins, -16, -16, 0);
        VectorSet(maxs, 16, 16, 72);
        return;
    }

    const skelAnimFrame_t *frame = TIKI_GetAnimFrame(anim, framenum);
    if (frame) {
        VectorCopy(frame->bounds[0], mins);
        VectorCopy(frame->bounds[1], maxs);
    } else {
        VectorSet(mins, -16, -16, 0);
        VectorSet(maxs, 16, 16, 72);
    }
}

float TIKI_SkelAnimFrameRadius(const char *filename, int framenum) {
    skelAnim_t *anim = TIKI_LoadSkelAnim(filename);
    if (!anim || !anim->frameData) return 0.0f;

    const skelAnimFrame_t *frame = TIKI_GetAnimFrame(anim, framenum);
    return frame ? frame->radius : 0.0f;
}

qboolean TIKI_SkelAnimGetBoneTransform(const char *filename, int framenum,
                                         int bonenum, float *quat, float *offset) {
    skelAnim_t *anim = TIKI_LoadSkelAnim(filename);
    if (!anim || !anim->frameData) return qfalse;
    if (bonenum < 0 || bonenum >= anim->numBones) return qfalse;

    const skelAnimFrame_t *frame = TIKI_GetAnimFrame(anim, framenum);
    if (!frame) return qfalse;

    TIKI_DecompressBone(&frame->bones[bonenum], quat, offset);
    return qtrue;
}

/* =========================================================================
 * Renderer helpers -- bone matrix palette and surface data access
 * ========================================================================= */

const byte *TIKI_GetSkelSurfaceData(const char *skelname, int *numSurfaces) {
    skeleton_t *skel = TIKI_LoadSkeleton(skelname);
    if (!skel || !skel->surfaceData) {
        if (numSurfaces) *numSurfaces = 0;
        return NULL;
    }
    if (numSurfaces) *numSurfaces = skel->numSurfaces;
    return skel->surfaceData;
}

int TIKI_BuildBoneMatrices(const char *skelname, const char *animfile, int framenum,
                            float boneMatrices[][3][4], int maxBones) {
    skeleton_t *skel = TIKI_LoadSkeleton(skelname);
    if (!skel) return 0;

    int numBones = skel->numBones;
    if (numBones > maxBones) numBones = maxBones;

    /* Get animation frame data */
    skelAnim_t *anim = NULL;
    const skelAnimFrame_t *frame = NULL;
    if (animfile && animfile[0]) {
        anim = TIKI_LoadSkelAnim(animfile);
        if (anim && anim->frameData) {
            frame = TIKI_GetAnimFrame(anim, framenum);
        }
    }

    for (int i = 0; i < numBones; i++) {
        float quat[4], offset[3];

        if (frame && anim && i < anim->numBones) {
            TIKI_DecompressBone(&frame->bones[i], quat, offset);
        } else {
            /* Identity bind pose */
            quat[0] = 0; quat[1] = 0; quat[2] = 0; quat[3] = 1;
            offset[0] = 0; offset[1] = 0; offset[2] = 0;
        }

        /* Quaternion to 3x4 matrix (3x3 rotation + translation) */
        float x = quat[0], y = quat[1], z = quat[2], w = quat[3];
        float xx = x*x, yy = y*y, zz = z*z;
        float xy = x*y, xz = x*z, yz = y*z;
        float wx = w*x, wy = w*y, wz = w*z;

        float local[3][4];
        local[0][0] = 1 - 2*(yy + zz);
        local[0][1] = 2*(xy - wz);
        local[0][2] = 2*(xz + wy);
        local[0][3] = offset[0];
        local[1][0] = 2*(xy + wz);
        local[1][1] = 1 - 2*(xx + zz);
        local[1][2] = 2*(yz - wx);
        local[1][3] = offset[1];
        local[2][0] = 2*(xz - wy);
        local[2][1] = 2*(yz + wx);
        local[2][2] = 1 - 2*(xx + yy);
        local[2][3] = offset[2];

        /* Apply parent bone transform (hierarchy) */
        int parent = skel->bones[i].parent;
        if (parent >= 0 && parent < i) {
            for (int r = 0; r < 3; r++) {
                float *pm = boneMatrices[parent][r];
                boneMatrices[i][r][0] = pm[0]*local[0][0] + pm[1]*local[1][0] + pm[2]*local[2][0];
                boneMatrices[i][r][1] = pm[0]*local[0][1] + pm[1]*local[1][1] + pm[2]*local[2][1];
                boneMatrices[i][r][2] = pm[0]*local[0][2] + pm[1]*local[1][2] + pm[2]*local[2][2];
                boneMatrices[i][r][3] = pm[0]*local[0][3] + pm[1]*local[1][3] + pm[2]*local[2][3] + pm[3];
            }
        } else {
            memcpy(boneMatrices[i], local, sizeof(local));
        }
    }

    return numBones;
}

/* =========================================================================
 * Cleanup
 * ========================================================================= */

void TIKI_FreeSkeletons(void) {
    for (int i = 0; i < skel_count; i++) {
        if (skel_cache[i].surfaceData) {
            Z_Free(skel_cache[i].surfaceData);
        }
    }
    memset(skel_cache, 0, sizeof(skel_cache));
    skel_count = 0;

    for (int i = 0; i < anim_count; i++) {
        if (anim_cache[i].frameData) {
            Z_Free(anim_cache[i].frameData);
        }
    }
    memset(anim_cache, 0, sizeof(anim_cache));
    anim_count = 0;
}
