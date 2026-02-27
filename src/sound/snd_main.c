/*
 * snd_main.c -- Sound system core
 *
 * Replaces Miles Sound System with SDL2 audio.
 */

#include "snd_local.h"
#include "../common/qcommon.h"

static qboolean snd_initialized = qfalse;

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

void S_StartSound(const vec3_t origin, int entityNum, int channel,
                  sfxHandle_t sfx, float volume, float minDist, float pitch) {
    (void)origin; (void)entityNum; (void)channel;
    (void)sfx; (void)volume; (void)minDist; (void)pitch;
}

void S_StopSound(int entityNum, int channel) {
    (void)entityNum; (void)channel;
}

void S_StartLocalSound(sfxHandle_t sfx, int channelNum) {
    (void)sfx; (void)channelNum;
}

sfxHandle_t S_RegisterSound(const char *name) {
    Com_DPrintf("S_RegisterSound: %s (stub)\n", name);
    return 0;
}

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

void S_UpdateListener(const vec3_t origin, const vec3_t forward,
                      const vec3_t right, const vec3_t up) {
    (void)origin; (void)forward; (void)right; (void)up;
}
