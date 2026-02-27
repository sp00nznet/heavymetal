/*
 * snd_local.h -- Sound system internal header
 *
 * The original FAKK2 used RAD Game Tools' Miles Sound System,
 * a commercial third-party audio library. The recomp replaces this
 * with SDL2 audio (and potentially OpenAL for 3D audio).
 *
 * FAKK2's audio features:
 *   - 3D positional audio
 *   - Dynamic music system with intensity/mood triggers
 *   - Ambient sound sets per level
 *   - Dialogue playback with Babble lip-sync
 *   - Looping sounds on entities
 *   - Music crossfading
 */

#ifndef SND_LOCAL_H
#define SND_LOCAL_H

#include "../common/fakk_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Sound system interface (called from engine) */
void    S_Init(void);
void    S_Shutdown(void);
void    S_Update(void);

/* Sound playback */
void    S_StartSound(const vec3_t origin, int entityNum, int channel,
                     sfxHandle_t sfx, float volume, float minDist, float pitch);
void    S_StopSound(int entityNum, int channel);
void    S_StartLocalSound(sfxHandle_t sfx, int channelNum);

/* Sound registration */
sfxHandle_t S_RegisterSound(const char *name);

/* Music system */
void    S_StartMusic(const char *intro, const char *loop);
void    S_StopMusic(void);
void    S_SetMusicMood(int mood, float volume);

/* Listener (camera/player position) */
void    S_UpdateListener(const vec3_t origin, const vec3_t forward,
                         const vec3_t right, const vec3_t up);

#ifdef __cplusplus
}
#endif

#endif /* SND_LOCAL_H */
