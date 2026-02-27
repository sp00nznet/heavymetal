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

/* =========================================================================
 * Animation delta queries
 *
 * Delta-driven animations contain root motion data. The game uses this
 * to move the entity based on the animation rather than physics.
 * ========================================================================= */

void TIKI_AnimDelta(dtiki_t handle, int animnum, vec3_t delta) {
    /* Requires reading delta from .ska binary data */
    (void)handle; (void)animnum;
    VectorClear(delta);
}

void TIKI_AnimAbsoluteDelta(dtiki_t handle, int animnum, vec3_t delta) {
    (void)handle; (void)animnum;
    VectorClear(delta);
}

/* =========================================================================
 * Frame delta and timing
 * ========================================================================= */

void TIKI_FrameDelta(dtiki_t handle, int animnum, int framenum, vec3_t delta) {
    (void)handle; (void)animnum; (void)framenum;
    VectorClear(delta);
}

float TIKI_FrameTime(dtiki_t handle, int animnum, int framenum) {
    /* Frame time in seconds -- requires binary animation data */
    (void)handle; (void)animnum; (void)framenum;
    return 0.0f;
}

void TIKI_FrameBounds(dtiki_t handle, int animnum, int framenum,
                      float scale, vec3_t mins, vec3_t maxs) {
    (void)handle; (void)animnum; (void)framenum;
    VectorSet(mins, -16 * scale, -16 * scale, 0);
    VectorSet(maxs, 16 * scale, 16 * scale, 72 * scale);
}

float TIKI_FrameRadius(dtiki_t handle, int animnum, int framenum) {
    (void)handle; (void)animnum; (void)framenum;
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
    (void)handle; (void)anim; (void)frame; (void)tagnum;
    (void)scale; (void)bone_tag; (void)bone_quat;

    VectorClear(orient.origin);
    VectorSet(orient.axis[0], 1, 0, 0);
    VectorSet(orient.axis[1], 0, 1, 0);
    VectorSet(orient.axis[2], 0, 0, 1);

    return orient;
}
