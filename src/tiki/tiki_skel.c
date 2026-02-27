/*
 * tiki_skel.c -- Skeleton and skeletal model loading
 *
 * Loads .skb (skeletal model) and .ska (skeletal animation) binary files.
 *
 * .skb format (skeletal model):
 *   - Bone hierarchy (parent indices, bind pose transforms)
 *   - Mesh data (vertices with bone weights, triangles, surfaces)
 *   - LOD information
 *
 * .ska format (skeletal animation):
 *   - Per-frame bone transforms (compressed quaternion + position)
 *   - Frame timing and delta movement
 *   - Animation bounds per frame
 *
 * .tan format (non-skeletal model):
 *   - TAN ident (0x54414E20 "TAN ")
 *   - Version 2
 *   - Vertex morph targets per frame
 *   - Simpler than skeletal but less memory efficient for complex models
 *
 * Binary format details from SDK qfiles.h:
 *   TIKI_MAX_FRAMES = 2048
 *   TIKI_MAX_VERTS = 1200
 *   TIKI_MAX_TRIANGLES = 4096
 *   TIKI_MAX_TAGS = 16
 *   TIKI_MAX_SURFACES = 32
 *
 * tikiFrame_t per frame:
 *   vec3_t bounds[2]   -- AABB min/max
 *   vec3_t scale        -- vertex scale factor
 *   vec3_t offset       -- vertex offset
 *   vec3_t delta        -- root motion delta
 *   float  radius       -- bounding sphere
 *   float  frametime    -- frame duration
 */

#include "tiki.h"
#include "../common/qcommon.h"
#include "../common/qfiles.h"

#include <string.h>

/* =========================================================================
 * Skeleton data (runtime representation)
 * ========================================================================= */

#define MAX_SKEL_BONES  TIKI_MAX_BONES

typedef struct {
    char    name[MAX_QPATH];
    int     parent;             /* -1 for root bone */
    vec3_t  basePosition;       /* bind pose position */
    vec4_t  baseRotation;       /* bind pose rotation (quaternion) */
} skel_bone_t;

typedef struct {
    char        name[MAX_QPATH];
    int         num_bones;
    skel_bone_t bones[MAX_SKEL_BONES];
} skeleton_t;

/* Skeleton cache */
#define MAX_SKELETONS   128
static skeleton_t   *skel_cache[MAX_SKELETONS];
static int          skel_count;

/* =========================================================================
 * Skeleton loading from .skb binary
 * ========================================================================= */

skeleton_t *TIKI_LoadSkeleton(const char *filename) {
    void *buffer;
    long len;

    /* Check cache first */
    for (int i = 0; i < skel_count; i++) {
        if (skel_cache[i] && !Q_stricmp(skel_cache[i]->name, filename))
            return skel_cache[i];
    }

    Com_DPrintf("TIKI_LoadSkeleton: %s\n", filename);

    len = FS_ReadFile(filename, &buffer);
    if (len < 0 || !buffer) {
        Com_Printf("TIKI_LoadSkeleton: couldn't load '%s'\n", filename);
        return NULL;
    }

    /* TODO: Parse .skb binary format
     *
     * The .skb format contains:
     * 1. Header (ident, version, num_surfaces, num_bones, bone_ofs, surface_ofs)
     * 2. Bone array (name[64], parent_index, bind_pose_matrix)
     * 3. Surface array (name, num_verts, num_triangles, vertex_data, index_data)
     *    Each vertex has: position, normal, texture coords, bone weights
     *
     * For now, create a placeholder skeleton until we reverse the binary format.
     */

    if (skel_count >= MAX_SKELETONS) {
        Com_Printf("TIKI_LoadSkeleton: cache full\n");
        FS_FreeFile(buffer);
        return NULL;
    }

    skeleton_t *skel = (skeleton_t *)Z_TagMalloc(sizeof(skeleton_t), TAG_TIKI);
    memset(skel, 0, sizeof(*skel));
    Q_strncpyz(skel->name, filename, sizeof(skel->name));

    /* Placeholder: single root bone */
    skel->num_bones = 1;
    Q_strncpyz(skel->bones[0].name, "origin", sizeof(skel->bones[0].name));
    skel->bones[0].parent = -1;

    skel_cache[skel_count++] = skel;

    FS_FreeFile(buffer);
    return skel;
}

/* =========================================================================
 * Skeleton queries
 * ========================================================================= */

int TIKI_SkeletonNumBones(const char *skelname) {
    skeleton_t *skel = TIKI_LoadSkeleton(skelname);
    return skel ? skel->num_bones : 0;
}

int TIKI_SkeletonBoneIndex(const char *skelname, const char *bonename) {
    skeleton_t *skel = TIKI_LoadSkeleton(skelname);
    if (!skel) return -1;

    for (int i = 0; i < skel->num_bones; i++) {
        if (!Q_stricmp(skel->bones[i].name, bonename))
            return i;
    }
    return -1;
}

const char *TIKI_SkeletonBoneName(const char *skelname, int boneindex) {
    skeleton_t *skel = TIKI_LoadSkeleton(skelname);
    if (!skel || boneindex < 0 || boneindex >= skel->num_bones)
        return "";
    return skel->bones[boneindex].name;
}

/* =========================================================================
 * Cleanup
 * ========================================================================= */

void TIKI_FreeSkeletons(void) {
    for (int i = 0; i < skel_count; i++) {
        if (skel_cache[i]) {
            Z_Free(skel_cache[i]);
            skel_cache[i] = NULL;
        }
    }
    skel_count = 0;
}
