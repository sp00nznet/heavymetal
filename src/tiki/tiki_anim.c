/*
 * tiki_anim.c -- TIKI skeletal animation system
 *
 * Handles animation blending, bone transforms, and frame interpolation.
 * FAKK2's animation system supports:
 *   - 16 simultaneous animation channels (frameInfo[16] in entityState_t)
 *   - Weighted blending between animations
 *   - Crossfade transitions
 *   - Frame-synchronized commands (sounds, effects)
 *   - Up to 128 bones per skeleton
 */

#include "tiki.h"
#include "../common/qcommon.h"

/* TODO: Implement animation system
 *
 * Key functions needed:
 * - TIKI_Animate(): advance animation time and compute bone transforms
 * - TIKI_BlendAnims(): blend multiple animation channels
 * - TIKI_ProcessFrameCommands(): fire frame events at correct times
 * - TIKI_GetBoneTransform(): get world-space transform for a bone
 */
