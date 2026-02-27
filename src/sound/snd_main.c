/*
 * snd_main.c -- Sound system core
 *
 * Replaces Miles Sound System with SDL2 audio.
 * Provides all sound functions needed by both game_import_t and
 * clientGameImport_t function pointer tables.
 */

#include "snd_local.h"
#include "../common/qcommon.h"
#include <string.h>

static qboolean snd_initialized = qfalse;

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

void S_Init(void) {
    Com_Printf("--- S_Init ---\n");
    Com_Printf("Sound: SDL2 audio (replacing Miles Sound System)\n");
    snd_initialized = qtrue;
}

void S_Shutdown(void) {
    if (!snd_initialized) return;
    Com_Printf("Sound system shutdown\n");
    snd_initialized = qfalse;
}

void S_Update(void) {
    if (!snd_initialized) return;
    /* TODO: Mix and output audio */
}

/* =========================================================================
 * Sound playback
 * ========================================================================= */

void S_StartSound(const vec3_t origin, int entityNum, int channel,
                  sfxHandle_t sfx, float volume, float minDist, float pitch) {
    (void)origin; (void)entityNum; (void)channel;
    (void)sfx; (void)volume; (void)minDist; (void)pitch;
    /* TODO: Start a 3D positioned sound */
}

void S_StopSound(int entityNum, int channel) {
    (void)entityNum; (void)channel;
}

void S_StartLocalSound(sfxHandle_t sfx, int channelNum) {
    (void)sfx; (void)channelNum;
}

void S_StartLocalSoundName(const char *sound_name) {
    (void)sound_name;
    /* Used by cgame import's S_StartLocalSound(const char *) */
}

/* =========================================================================
 * Looping sounds
 * ========================================================================= */

void S_ClearLoopingSounds(void) {
    /* TODO: Clear all active looping sounds */
}

void S_AddLoopingSound(const vec3_t origin, const vec3_t velocity,
                       sfxHandle_t sfx, float volume, float minDist) {
    (void)origin; (void)velocity; (void)sfx; (void)volume; (void)minDist;
}

/* =========================================================================
 * Registration
 * ========================================================================= */

void S_BeginRegistration(void) {
    /* Called at level start to begin sound precaching */
}

sfxHandle_t S_RegisterSound(const char *name) {
    Com_DPrintf("S_RegisterSound: %s (stub)\n", name);
    return 0;
}

void S_EndRegistration(void) {
    /* Called after all sounds are precached */
}

/* =========================================================================
 * Entity updates and spatialization
 * ========================================================================= */

void S_UpdateEntity(int entityNum, const vec3_t origin, const vec3_t velocity,
                    qboolean useListener) {
    (void)entityNum; (void)origin; (void)velocity; (void)useListener;
}

void S_Respatialize(int entityNum, vec3_t origin, vec3_t axis[3]) {
    (void)entityNum; (void)origin; (void)axis;
    /* Update the listener position for 3D audio */
}

/* =========================================================================
 * Music system
 * ========================================================================= */

void S_StartMusic(const char *intro, const char *loop) {
    Com_DPrintf("S_StartMusic: %s / %s (stub)\n",
                intro ? intro : "(none)", loop ? loop : "(none)");
}

void S_StopMusic(void) {
    /* TODO */
}

void S_SetMusicMood(int mood, float volume) {
    (void)mood; (void)volume;
}

void MUSIC_NewSoundtrack(const char *name) {
    Com_DPrintf("MUSIC_NewSoundtrack: %s (stub)\n", name ? name : "(null)");
}

void MUSIC_UpdateMood(int current_mood, int fallback_mood) {
    (void)current_mood; (void)fallback_mood;
}

void MUSIC_UpdateVolume(float volume, float fade_time) {
    (void)volume; (void)fade_time;
}

/* =========================================================================
 * Reverb and ambient volume
 * ========================================================================= */

void S_SetReverb(int reverb_type, float reverb_level) {
    (void)reverb_type; (void)reverb_level;
}

void S_SetGlobalAmbientVolumeLevel(float volume) {
    (void)volume;
}

/* =========================================================================
 * Lip sync (Babble system)
 *
 * FAKK2 uses amplitude data from sound files to drive character
 * lip animation during dialogue playback.
 * ========================================================================= */

float S_GetLipLength(const char *name) {
    (void)name;
    return 0.0f;
}

byte *S_GetLipAmplitudes(const char *name, int *number_of_amplitudes) {
    (void)name;
    if (number_of_amplitudes) *number_of_amplitudes = 0;
    return NULL;
}

/* =========================================================================
 * Sound queries for game DLL
 * ========================================================================= */

float S_SoundLength(const char *path) {
    (void)path;
    return 0.0f;
}

byte *S_SoundAmplitudes(const char *name, int *number_of_amplitudes) {
    (void)name;
    if (number_of_amplitudes) *number_of_amplitudes = 0;
    return NULL;
}

/* =========================================================================
 * Listener
 * ========================================================================= */

void S_UpdateListener(const vec3_t origin, const vec3_t forward,
                      const vec3_t right, const vec3_t up) {
    (void)origin; (void)forward; (void)right; (void)up;
}
