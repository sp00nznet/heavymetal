/*
 * snd_sdl.c -- SDL2 audio backend
 *
 * Provides low-level audio output via SDL2's audio API.
 * This replaces the Miles Sound System's hardware abstraction.
 *
 * Audio pipeline:
 *   1. Sound files (.wav) loaded from PK3 into memory
 *   2. Playing sounds tracked in channel array
 *   3. SDL2 audio callback mixes active channels
 *   4. 3D spatialization via distance attenuation and stereo pan
 *
 * The original Miles Sound System supported:
 *   - WAV (PCM, ADPCM)
 *   - MP3 (for music)
 *   - 3D positional audio
 *   - Reverb effects
 *   - Streaming music with crossfading
 *
 * We replicate these features using SDL2's audio API.
 */

#include "snd_local.h"
#include "../common/qcommon.h"
#include <string.h>
#include <math.h>

#ifdef USE_SDL2
#include <SDL2/SDL.h>
#endif

/* =========================================================================
 * Audio device state
 * ========================================================================= */

#define SND_SAMPLE_RATE     22050   /* original FAKK2 audio rate */
#define SND_CHANNELS        2       /* stereo output */
#define SND_SAMPLES         1024    /* buffer size in samples */
#define SND_FORMAT          AUDIO_S16SYS

typedef struct {
    qboolean        initialized;
#ifdef USE_SDL2
    SDL_AudioDeviceID deviceId;
    SDL_AudioSpec   spec;
#endif
    float           masterVolume;
    float           musicVolume;
} sndBackend_t;

static sndBackend_t snd_backend;

/* =========================================================================
 * Sound data cache
 *
 * Loaded sound files are kept in memory for fast playback.
 * ========================================================================= */

#define MAX_SFX             512
#define MAX_SFX_NAME        MAX_QPATH

typedef struct {
    char        name[MAX_SFX_NAME];
    qboolean    loaded;
    byte        *data;          /* raw PCM samples (16-bit signed) */
    int         dataLen;        /* length in bytes */
    int         sampleRate;
    int         channels;       /* 1 = mono, 2 = stereo */
    int         bitsPerSample;
    float       duration;       /* length in seconds */
} sfx_t;

static sfx_t    sfx_cache[MAX_SFX];
static int      sfx_count;

/* =========================================================================
 * Active sound channels
 * ========================================================================= */

#define MAX_SOUND_CHANNELS  64

typedef struct {
    qboolean    active;
    int         entityNum;
    int         channel;
    sfx_t       *sfx;
    int         playOffset;     /* current sample offset */
    float       volume;
    float       attenuation;
    float       pitch;
    vec3_t      origin;
    qboolean    positional;     /* true = 3D positioned */
    qboolean    looping;
} sndChannel_t;

static sndChannel_t snd_channels[MAX_SOUND_CHANNELS];

/* Listener state */
static vec3_t   listener_origin;
static vec3_t   listener_forward;
static vec3_t   listener_right;
static vec3_t   listener_up;

/* =========================================================================
 * WAV file loading
 *
 * Loads a .wav file from the virtual filesystem into the sfx cache.
 * Supports standard PCM format (uncompressed).
 * ========================================================================= */

/* Simple WAV header parsing */
typedef struct {
    char    riffId[4];      /* "RIFF" */
    int     riffSize;
    char    waveId[4];      /* "WAVE" */
} wavHeader_t;

typedef struct {
    char    id[4];
    int     size;
} wavChunk_t;

typedef struct {
    short   formatTag;      /* 1 = PCM */
    short   channels;
    int     sampleRate;
    int     avgBytesPerSec;
    short   blockAlign;
    short   bitsPerSample;
} wavFmt_t;

static qboolean SND_LoadWav(sfx_t *sfx, const char *name) {
    void *buffer;
    long len = FS_ReadFile(name, &buffer);
    if (len <= 0 || !buffer) return qfalse;

    const byte *ptr = (const byte *)buffer;
    const byte *end = ptr + len;

    /* Validate RIFF header */
    if (len < (long)sizeof(wavHeader_t)) {
        FS_FreeFile(buffer);
        return qfalse;
    }

    const wavHeader_t *header = (const wavHeader_t *)ptr;
    if (memcmp(header->riffId, "RIFF", 4) != 0 ||
        memcmp(header->waveId, "WAVE", 4) != 0) {
        FS_FreeFile(buffer);
        return qfalse;
    }

    ptr += sizeof(wavHeader_t);

    /* Find fmt and data chunks */
    const wavFmt_t *fmt = NULL;
    const byte *dataPtr = NULL;
    int dataLen = 0;

    while (ptr + sizeof(wavChunk_t) <= end) {
        const wavChunk_t *chunk = (const wavChunk_t *)ptr;
        ptr += sizeof(wavChunk_t);

        if (memcmp(chunk->id, "fmt ", 4) == 0) {
            fmt = (const wavFmt_t *)ptr;
        }
        else if (memcmp(chunk->id, "data", 4) == 0) {
            dataPtr = ptr;
            dataLen = chunk->size;
        }

        ptr += chunk->size;
        /* WAV chunks are word-aligned */
        if (chunk->size & 1) ptr++;
    }

    if (!fmt || !dataPtr || dataLen <= 0) {
        FS_FreeFile(buffer);
        return qfalse;
    }

    /* Only support PCM format */
    if (fmt->formatTag != 1) {
        Com_Printf("SND_LoadWav: '%s' is not PCM (tag=%d)\n", name, fmt->formatTag);
        FS_FreeFile(buffer);
        return qfalse;
    }

    /* Copy PCM data */
    sfx->data = (byte *)Z_Malloc(dataLen);
    memcpy(sfx->data, dataPtr, dataLen);
    sfx->dataLen = dataLen;
    sfx->sampleRate = fmt->sampleRate;
    sfx->channels = fmt->channels;
    sfx->bitsPerSample = fmt->bitsPerSample;

    int bytesPerSample = (fmt->bitsPerSample / 8) * fmt->channels;
    int numSamples = dataLen / bytesPerSample;
    sfx->duration = (float)numSamples / (float)fmt->sampleRate;

    sfx->loaded = qtrue;
    FS_FreeFile(buffer);
    return qtrue;
}

/* =========================================================================
 * Sound registration
 * ========================================================================= */

sfxHandle_t SND_RegisterSound(const char *name) {
    if (!name || !name[0]) return 0;

    /* Check cache */
    for (int i = 0; i < sfx_count; i++) {
        if (!Q_stricmp(sfx_cache[i].name, name)) {
            return i;
        }
    }

    /* Allocate new slot */
    if (sfx_count >= MAX_SFX) {
        Com_Printf("SND_RegisterSound: MAX_SFX exceeded\n");
        return 0;
    }

    sfx_t *sfx = &sfx_cache[sfx_count];
    memset(sfx, 0, sizeof(*sfx));
    Q_strncpyz(sfx->name, name, sizeof(sfx->name));

    /* Try loading the wav file */
    char wavpath[MAX_QPATH];

    /* Try direct path first */
    if (!SND_LoadWav(sfx, name)) {
        /* Try with sound/ prefix */
        snprintf(wavpath, sizeof(wavpath), "sound/%s", name);
        if (!SND_LoadWav(sfx, wavpath)) {
            /* Try with .wav extension */
            snprintf(wavpath, sizeof(wavpath), "sound/%s.wav", name);
            SND_LoadWav(sfx, wavpath);
        }
    }

    int handle = sfx_count++;
    return handle;
}

/* =========================================================================
 * 3D spatialization
 *
 * Calculates volume and stereo pan based on listener-relative position.
 * ========================================================================= */

static void SND_Spatialize(sndChannel_t *ch, float *leftVol, float *rightVol) {
    if (!ch->positional) {
        /* Non-positional (UI sounds, etc.) */
        *leftVol = ch->volume;
        *rightVol = ch->volume;
        return;
    }

    /* Distance attenuation */
    vec3_t diff;
    VectorSubtract(ch->origin, listener_origin, diff);
    float dist = VectorLength(diff);

    float atten = 1.0f;
    if (ch->attenuation > 0.0f && dist > ch->attenuation) {
        atten = ch->attenuation / dist;
        if (atten > 1.0f) atten = 1.0f;
    }

    float vol = ch->volume * atten;

    /* Stereo panning */
    float pan = 0.0f;
    if (dist > 1.0f) {
        vec3_t dir;
        VectorScale(diff, 1.0f / dist, dir);
        pan = DotProduct(dir, listener_right);
    }

    /* Apply pan to left/right channels */
    *leftVol = vol * (0.5f - pan * 0.5f);
    *rightVol = vol * (0.5f + pan * 0.5f);

    if (*leftVol < 0.0f) *leftVol = 0.0f;
    if (*rightVol < 0.0f) *rightVol = 0.0f;
    if (*leftVol > 1.0f) *leftVol = 1.0f;
    if (*rightVol > 1.0f) *rightVol = 1.0f;
}

/* =========================================================================
 * Audio mixing callback
 *
 * Called by SDL2's audio thread to fill the output buffer.
 * Mixes all active sound channels into the output.
 * ========================================================================= */

#ifdef USE_SDL2
static void SND_AudioCallback(void *userdata, Uint8 *stream, int len) {
    (void)userdata;

    /* Clear output buffer */
    memset(stream, 0, len);

    int numSamples = len / (sizeof(short) * SND_CHANNELS);
    short *out = (short *)stream;

    for (int ch = 0; ch < MAX_SOUND_CHANNELS; ch++) {
        sndChannel_t *chan = &snd_channels[ch];
        if (!chan->active || !chan->sfx || !chan->sfx->loaded) continue;

        sfx_t *sfx = chan->sfx;

        /* Calculate spatialization */
        float leftVol, rightVol;
        SND_Spatialize(chan, &leftVol, &rightVol);

        float masterVol = snd_backend.masterVolume;
        leftVol *= masterVol;
        rightVol *= masterVol;

        /* Mix samples */
        int bytesPerSample = sfx->bitsPerSample / 8;
        int sfxSamples = sfx->dataLen / bytesPerSample;
        if (sfx->channels == 2) sfxSamples /= 2;

        for (int i = 0; i < numSamples; i++) {
            int sampleIdx = chan->playOffset;

            if (sampleIdx >= sfxSamples) {
                if (chan->looping) {
                    chan->playOffset = 0;
                    sampleIdx = 0;
                } else {
                    chan->active = qfalse;
                    break;
                }
            }

            /* Read source sample */
            float sample = 0.0f;
            if (sfx->bitsPerSample == 16) {
                short *src = (short *)sfx->data;
                if (sfx->channels == 2) {
                    sample = (src[sampleIdx * 2] + src[sampleIdx * 2 + 1]) * 0.5f;
                } else {
                    sample = src[sampleIdx];
                }
            } else if (sfx->bitsPerSample == 8) {
                byte *src = sfx->data;
                sample = (src[sampleIdx] - 128) * 256.0f;
            }

            /* Mix into output */
            int outIdx = i * 2;
            int left = out[outIdx] + (int)(sample * leftVol);
            int right = out[outIdx + 1] + (int)(sample * rightVol);

            /* Clamp */
            if (left > 32767) left = 32767;
            if (left < -32768) left = -32768;
            if (right > 32767) right = 32767;
            if (right < -32768) right = -32768;

            out[outIdx] = (short)left;
            out[outIdx + 1] = (short)right;

            chan->playOffset++;
        }
    }
}
#endif

/* =========================================================================
 * Backend initialization
 * ========================================================================= */

qboolean SND_Init(void) {
#ifdef USE_SDL2
    SDL_AudioSpec desired;
    memset(&desired, 0, sizeof(desired));
    desired.freq = SND_SAMPLE_RATE;
    desired.format = SND_FORMAT;
    desired.channels = SND_CHANNELS;
    desired.samples = SND_SAMPLES;
    desired.callback = SND_AudioCallback;

    snd_backend.deviceId = SDL_OpenAudioDevice(NULL, 0, &desired,
                                                &snd_backend.spec, 0);
    if (snd_backend.deviceId == 0) {
        Com_Printf("SND_Init: SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        return qfalse;
    }

    snd_backend.masterVolume = 0.8f;
    snd_backend.musicVolume = 0.5f;
    snd_backend.initialized = qtrue;

    /* Start playback */
    SDL_PauseAudioDevice(snd_backend.deviceId, 0);

    Com_Printf("SDL2 audio: %d Hz, %d ch, %d samples\n",
               snd_backend.spec.freq, snd_backend.spec.channels,
               snd_backend.spec.samples);
    return qtrue;
#else
    return qfalse;
#endif
}

void SND_Shutdown(void) {
#ifdef USE_SDL2
    if (snd_backend.deviceId) {
        SDL_CloseAudioDevice(snd_backend.deviceId);
        snd_backend.deviceId = 0;
    }
#endif

    /* Free cached sound data */
    for (int i = 0; i < sfx_count; i++) {
        if (sfx_cache[i].data) {
            Z_Free(sfx_cache[i].data);
            sfx_cache[i].data = NULL;
        }
    }
    sfx_count = 0;

    snd_backend.initialized = qfalse;
}

/* =========================================================================
 * Channel allocation
 * ========================================================================= */

static sndChannel_t *SND_PickChannel(int entityNum, int channel) {
    /* First try to find a channel for this entity/channel combo */
    for (int i = 0; i < MAX_SOUND_CHANNELS; i++) {
        if (snd_channels[i].entityNum == entityNum &&
            snd_channels[i].channel == channel) {
            return &snd_channels[i];
        }
    }

    /* Find a free channel */
    for (int i = 0; i < MAX_SOUND_CHANNELS; i++) {
        if (!snd_channels[i].active) {
            return &snd_channels[i];
        }
    }

    /* Steal the oldest channel */
    return &snd_channels[0];
}

/* =========================================================================
 * Public interface wrappers (called from snd_main.c stubs)
 *
 * These functions bridge the high-level sound API to the SDL backend.
 * ========================================================================= */

void SND_StartSound(const vec3_t origin, int entityNum, int channel,
                     sfxHandle_t sfx, float volume, float minDist, float pitch) {
    if (!snd_backend.initialized) return;
    if (sfx < 0 || sfx >= sfx_count) return;

    sndChannel_t *ch = SND_PickChannel(entityNum, channel);
    memset(ch, 0, sizeof(*ch));

    ch->active = qtrue;
    ch->entityNum = entityNum;
    ch->channel = channel;
    ch->sfx = &sfx_cache[sfx];
    ch->playOffset = 0;
    ch->volume = volume;
    ch->attenuation = minDist;
    ch->pitch = pitch;
    ch->looping = qfalse;

    if (origin) {
        VectorCopy(origin, ch->origin);
        ch->positional = qtrue;
    } else {
        ch->positional = qfalse;
    }
}

void SND_StopSound(int entityNum, int channel) {
    for (int i = 0; i < MAX_SOUND_CHANNELS; i++) {
        if (snd_channels[i].entityNum == entityNum &&
            snd_channels[i].channel == channel) {
            snd_channels[i].active = qfalse;
        }
    }
}

void SND_UpdateListener(const vec3_t origin, const vec3_t forward,
                         const vec3_t right, const vec3_t up) {
    VectorCopy(origin, listener_origin);
    VectorCopy(forward, listener_forward);
    VectorCopy(right, listener_right);
    VectorCopy(up, listener_up);
}

/* =========================================================================
 * Additional backend functions
 * ========================================================================= */

float SND_SoundLength(sfxHandle_t sfx) {
    if (sfx < 0 || sfx >= sfx_count) return 0.0f;
    if (!sfx_cache[sfx].loaded) return 0.0f;
    return sfx_cache[sfx].duration;
}

void SND_SetVolume(float master, float music) {
    if (master >= 0.0f) snd_backend.masterVolume = master;
    if (music >= 0.0f) snd_backend.musicVolume = music;
}

/* =========================================================================
 * Looping sound management
 *
 * Looping sounds are re-added every frame via S_AddLoopingSound.
 * S_ClearLoopingSounds marks all looping channels for removal;
 * they survive only if re-added during the same frame.
 * ========================================================================= */

void SND_ClearLoopingSounds(void) {
    for (int i = 0; i < MAX_SOUND_CHANNELS; i++) {
        if (snd_channels[i].active && snd_channels[i].looping) {
            /* Mark for potential removal -- if not re-added this frame,
             * the channel will be deactivated during the next clear. */
            snd_channels[i].looping = 2;  /* 2 = pending removal */
        }
    }
}

void SND_AddLoopingSound(const vec3_t origin, sfxHandle_t sfx,
                          float volume, float minDist) {
    if (!snd_backend.initialized) return;
    if (sfx < 0 || sfx >= sfx_count) return;

    /* Check if this sfx is already playing as a looping sound */
    for (int i = 0; i < MAX_SOUND_CHANNELS; i++) {
        if (snd_channels[i].active && snd_channels[i].looping &&
            snd_channels[i].sfx == &sfx_cache[sfx]) {
            /* Update position and keep alive */
            if (origin) VectorCopy(origin, snd_channels[i].origin);
            snd_channels[i].volume = volume;
            snd_channels[i].looping = qtrue;  /* un-mark pending removal */
            return;
        }
    }

    /* Start a new looping channel */
    sndChannel_t *ch = SND_PickChannel(-2, 0); /* -2 = looping entity slot */
    memset(ch, 0, sizeof(*ch));

    ch->active = qtrue;
    ch->entityNum = -2;
    ch->channel = 0;
    ch->sfx = &sfx_cache[sfx];
    ch->playOffset = 0;
    ch->volume = volume;
    ch->attenuation = minDist;
    ch->pitch = 1.0f;
    ch->looping = qtrue;

    if (origin) {
        VectorCopy(origin, ch->origin);
        ch->positional = qtrue;
    } else {
        ch->positional = qfalse;
    }
}

/* =========================================================================
 * Entity position tracking for moving sound sources
 * ========================================================================= */

void SND_UpdateEntityPosition(int entityNum, const vec3_t origin) {
    if (!origin) return;

    for (int i = 0; i < MAX_SOUND_CHANNELS; i++) {
        if (snd_channels[i].active &&
            snd_channels[i].entityNum == entityNum &&
            snd_channels[i].positional) {
            VectorCopy(origin, snd_channels[i].origin);
        }
    }
}

byte *SND_GetPCMData(sfxHandle_t sfx, int *outLen, int *outRate, int *outBits, int *outChan) {
    if (sfx < 0 || sfx >= sfx_count || !sfx_cache[sfx].loaded) {
        if (outLen) *outLen = 0;
        return NULL;
    }
    sfx_t *s = &sfx_cache[sfx];
    if (outLen)  *outLen  = s->dataLen;
    if (outRate) *outRate = s->sampleRate;
    if (outBits) *outBits = s->bitsPerSample;
    if (outChan) *outChan = s->channels;
    return s->data;
}

void SND_StopAllSounds(void) {
#ifdef USE_SDL2
    if (snd_backend.deviceId) {
        SDL_LockAudioDevice(snd_backend.deviceId);
    }
#endif

    for (int i = 0; i < MAX_SOUND_CHANNELS; i++) {
        snd_channels[i].active = qfalse;
    }

#ifdef USE_SDL2
    if (snd_backend.deviceId) {
        SDL_UnlockAudioDevice(snd_backend.deviceId);
    }
#endif
}
