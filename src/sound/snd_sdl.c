/*
 * snd_sdl.c -- SDL2 audio backend
 *
 * Provides low-level audio output via SDL2's audio API.
 * This replaces the Miles Sound System's hardware abstraction.
 */

#include "snd_local.h"
#include "../common/qcommon.h"

#ifdef USE_SDL2
#include <SDL2/SDL.h>
#endif

/* TODO: Implement SDL2 audio backend
 *
 * Required:
 * - Open audio device with SDL_OpenAudioDevice()
 * - Audio callback for mixing
 * - WAV/OGG decoding for sound effects
 * - Streaming audio for music
 * - 3D audio spatialization
 */
