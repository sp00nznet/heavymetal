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
void    S_StartLocalSoundName(const char *sound_name);

/* Looping sounds */
void    S_ClearLoopingSounds(void);
void    S_AddLoopingSound(const vec3_t origin, const vec3_t velocity,
                          sfxHandle_t sfx, float volume, float minDist);

/* Sound registration */
void    S_BeginRegistration(void);
sfxHandle_t S_RegisterSound(const char *name);
void    S_EndRegistration(void);

/* Entity update */
void    S_UpdateEntity(int entityNum, const vec3_t origin, const vec3_t velocity,
                       qboolean useListener);
void    S_Respatialize(int entityNum, vec3_t origin, vec3_t axis[3]);

/* Music system */
void    S_StartMusic(const char *intro, const char *loop);
void    S_StopMusic(void);
void    S_SetMusicMood(int mood, float volume);

/* FAKK2 music system interface (used by cgame import) */
void    MUSIC_NewSoundtrack(const char *name);
void    MUSIC_UpdateMood(int current_mood, int fallback_mood);
void    MUSIC_UpdateVolume(float volume, float fade_time);

/* Reverb and global volume */
void    S_SetReverb(int reverb_type, float reverb_level);
void    S_SetGlobalAmbientVolumeLevel(float volume);

/* Lip sync (Babble system) */
float   S_GetLipLength(const char *name);
byte    *S_GetLipAmplitudes(const char *name, int *number_of_amplitudes);

/* Sound queries for game DLL */
float   S_SoundLength(const char *path);
byte    *S_SoundAmplitudes(const char *name, int *number_of_amplitudes);

/* Listener (camera/player position) */
void    S_UpdateListener(const vec3_t origin, const vec3_t forward,
                         const vec3_t right, const vec3_t up);

/* =========================================================================
 * SDL2 backend (snd_sdl.c)
 * ========================================================================= */

qboolean        SND_Init(void);
void            SND_Shutdown(void);
sfxHandle_t     SND_RegisterSound(const char *name);
void            SND_StartSound(const vec3_t origin, int entityNum, int channel,
                               sfxHandle_t sfx, float volume, float minDist, float pitch);
void            SND_StopSound(int entityNum, int channel);
void            SND_UpdateListener(const vec3_t origin, const vec3_t forward,
                                   const vec3_t right, const vec3_t up);
float           SND_SoundLength(sfxHandle_t sfx);
void            SND_SetVolume(float master, float music);
void            SND_StopAllSounds(void);

#ifdef __cplusplus
}
#endif

#endif /* SND_LOCAL_H */
