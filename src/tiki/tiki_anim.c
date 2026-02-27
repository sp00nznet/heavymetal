/*
 * tiki_anim.c -- TIKI animation runtime
 *
 * Handles animation playback, blending, bone transforms, and frame
 * command dispatch. FAKK2's animation system supports:
 *   - 16 simultaneous animation channels (frameInfo[16] in entityState_t)
 *   - Weighted blending between animations
 *   - Crossfade transitions with configurable blend times
 *   - Frame-synchronized commands (sounds, effects, weapon events)
 *   - Up to 128 bones per skeleton
 *   - Delta-driven movement (root motion from animation data)
 *
 * Animation binary formats:
 *   .ska -- Skeletal animation (compressed bone transforms per frame)
 *   .tan -- Non-skeletal "TAN" animation (vertex morph targets)
 *
 * Frame commands are dispatched through tiki_cmd_t to the game DLL
 * via game_import_t.Frame_Commands().
 */

#include "tiki.h"
#include "../common/qcommon.h"
#include "../common/qfiles.h"
#include <string.h>
#include <math.h>

/* Accessors from tiki_skel.c -- use real loaded .ska data */
extern int      TIKI_SkelAnimNumFrames(const char *filename);
extern float    TIKI_SkelAnimTotalTime(const char *filename);
extern float    TIKI_SkelAnimFrameTime(const char *filename);
extern void     TIKI_SkelAnimTotalDelta(const char *filename, vec3_t delta);
extern void     TIKI_SkelAnimFrameDelta(const char *filename, int framenum, vec3_t delta);
extern void     TIKI_SkelAnimFrameBounds(const char *filename, int framenum, vec3_t mins, vec3_t maxs);
extern float    TIKI_SkelAnimFrameRadius(const char *filename, int framenum);
extern qboolean TIKI_SkelAnimGetBoneTransform(const char *filename, int framenum,
                                               int bonenum, float *quat, float *offset);

/* Helper to get animation filename for a model+animnum */
static const char *TIKI_GetAnimFilename(dtiki_t handle, int animnum) {
    tiki_model_t *m = TIKI_GetModel(handle);
    if (!m || animnum < 0 || animnum >= m->num_anims) return NULL;
    return m->anims[animnum].filename;
}

/* =========================================================================
 * Animation delta queries
 *
 * Delta-driven animations contain root motion data. The game uses this
 * to move the entity based on the animation rather than physics.
 * ========================================================================= */

void TIKI_AnimDelta(dtiki_t handle, int animnum, vec3_t delta) {
    const char *fn = TIKI_GetAnimFilename(handle, animnum);
    if (fn) {
        TIKI_SkelAnimTotalDelta(fn, delta);
    } else {
        VectorClear(delta);
    }
}

void TIKI_AnimAbsoluteDelta(dtiki_t handle, int animnum, vec3_t delta) {
    /* For absolute delta, we use the total delta from the animation header */
    TIKI_AnimDelta(handle, animnum, delta);
}

/* =========================================================================
 * Frame delta and timing
 * ========================================================================= */

void TIKI_FrameDelta(dtiki_t handle, int animnum, int framenum, vec3_t delta) {
    const char *fn = TIKI_GetAnimFilename(handle, animnum);
    if (fn) {
        TIKI_SkelAnimFrameDelta(fn, framenum, delta);
    } else {
        VectorClear(delta);
    }
}

float TIKI_FrameTime(dtiki_t handle, int animnum, int framenum) {
    const char *fn = TIKI_GetAnimFilename(handle, animnum);
    if (fn) {
        float ft = TIKI_SkelAnimFrameTime(fn);
        return ft > 0.0f ? ft : 0.0f;
    }
    (void)framenum;
    return 0.0f;
}

void TIKI_FrameBounds(dtiki_t handle, int animnum, int framenum,
                      float scale, vec3_t mins, vec3_t maxs) {
    const char *fn = TIKI_GetAnimFilename(handle, animnum);
    if (fn) {
        TIKI_SkelAnimFrameBounds(fn, framenum, mins, maxs);
        VectorScale(mins, scale, mins);
        VectorScale(maxs, scale, maxs);
    } else {
        VectorSet(mins, -16 * scale, -16 * scale, 0);
        VectorSet(maxs, 16 * scale, 16 * scale, 72 * scale);
    }
}

float TIKI_FrameRadius(dtiki_t handle, int animnum, int framenum) {
    const char *fn = TIKI_GetAnimFilename(handle, animnum);
    if (fn) {
        return TIKI_SkelAnimFrameRadius(fn, framenum);
    }
    return 0.0f;
}

/* =========================================================================
 * Frame command dispatch
 *
 * Called each frame during animation playback to check if any frame
 * commands should fire. Commands are converted to tiki_cmd_t format
 * and passed to the game DLL.
 * ========================================================================= */

qboolean TIKI_FrameCommands(dtiki_t handle, int animnum, int framenum,
                            tiki_cmd_t *tiki_cmd) {
    tiki_model_t *m = TIKI_GetModel(handle);
    if (!m || !tiki_cmd) return qfalse;
    if (animnum < 0 || animnum >= m->num_anims) return qfalse;

    tiki_cmd->num_cmds = 0;
    tiki_animdef_t *anim = &m->anims[animnum];

    /* Check server commands for this frame */
    for (int i = 0; i < anim->num_server_cmds && tiki_cmd->num_cmds < 128; i++) {
        tiki_frame_cmd_t *fc = &anim->server_cmds[i];

        qboolean fire = qfalse;
        if (fc->frame_num == framenum)
            fire = qtrue;
        else if (fc->frame_num == TIKI_FRAME_ENTRY && framenum == 0)
            fire = qtrue;
        else if (fc->frame_num == TIKI_FRAME_EVERY)
            fire = qtrue;
        /* TIKI_FRAME_EXIT and TIKI_FRAME_LAST handled by caller */

        if (fire && fc->num_args > 0) {
            int idx = tiki_cmd->num_cmds;
            tiki_cmd->cmds[idx].argc = fc->num_args;
            tiki_cmd->cmds[idx].argv = fc->args;
            tiki_cmd->num_cmds++;
        }
    }

    return (tiki_cmd->num_cmds > 0);
}

/* =========================================================================
 * Tag (bone) orientation query
 *
 * Returns the world-space position and orientation of a bone/tag
 * at a specific animation frame. Used for attachment points (weapons,
 * effects, etc.) and IK.
 * ========================================================================= */

orientation_t TIKI_TagOrientation(dtiki_t handle, int anim, int frame,
                                  int tagnum, float scale,
                                  int *bone_tag, vec4_t *bone_quat) {
    orientation_t orient;

    /* Default to identity */
    VectorClear(orient.origin);
    VectorSet(orient.axis[0], 1, 0, 0);
    VectorSet(orient.axis[1], 0, 1, 0);
    VectorSet(orient.axis[2], 0, 0, 1);

    const char *fn = TIKI_GetAnimFilename(handle, anim);
    if (!fn || tagnum < 0) return orient;

    float quat[4], offset[3];
    if (TIKI_SkelAnimGetBoneTransform(fn, frame, tagnum, quat, offset)) {
        /* Apply scale to offset */
        orient.origin[0] = offset[0] * scale;
        orient.origin[1] = offset[1] * scale;
        orient.origin[2] = offset[2] * scale;

        /* Convert quaternion to axis matrix */
        float xx = quat[0] * quat[0];
        float yy = quat[1] * quat[1];
        float zz = quat[2] * quat[2];
        float xy = quat[0] * quat[1];
        float xz = quat[0] * quat[2];
        float yz = quat[1] * quat[2];
        float wx = quat[3] * quat[0];
        float wy = quat[3] * quat[1];
        float wz = quat[3] * quat[2];

        orient.axis[0][0] = 1.0f - 2.0f * (yy + zz);
        orient.axis[0][1] = 2.0f * (xy + wz);
        orient.axis[0][2] = 2.0f * (xz - wy);

        orient.axis[1][0] = 2.0f * (xy - wz);
        orient.axis[1][1] = 1.0f - 2.0f * (xx + zz);
        orient.axis[1][2] = 2.0f * (yz + wx);

        orient.axis[2][0] = 2.0f * (xz + wy);
        orient.axis[2][1] = 2.0f * (yz - wx);
        orient.axis[2][2] = 1.0f - 2.0f * (xx + yy);

        /* Return bone data if requested */
        if (bone_tag) *bone_tag = tagnum;
        if (bone_quat) {
            (*bone_quat)[0] = quat[0];
            (*bone_quat)[1] = quat[1];
            (*bone_quat)[2] = quat[2];
            (*bone_quat)[3] = quat[3];
        }
    }

    return orient;
}
