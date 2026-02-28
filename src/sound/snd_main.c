/*
 * snd_main.c -- Sound system core
 *
 * Replaces Miles Sound System with SDL2 audio.
 * Provides all sound functions needed by both game_import_t and
 * clientGameImport_t function pointer tables.
 *
 * This file is the public API layer; actual audio work is in snd_sdl.c.
 */

#include "snd_local.h"
#include "../common/qcommon.h"
#include <string.h>
#include <math.h>

static qboolean snd_initialized = qfalse;

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

void S_Init(void) {
    Com_Printf("--- S_Init ---\n");
    Com_Printf("Sound: SDL2 audio (replacing Miles Sound System)\n");

    if (SND_Init()) {
        snd_initialized = qtrue;
        Com_Printf("Sound system initialized\n");
    } else {
        Com_Printf("Sound system failed to initialize\n");
        snd_initialized = qfalse;
    }
}

void S_Shutdown(void) {
    if (!snd_initialized) return;
    SND_Shutdown();
    Com_Printf("Sound system shutdown\n");
    snd_initialized = qfalse;
}

void S_Update(void) {
    if (!snd_initialized) return;
    /* SDL2 callback-based mixing handles output automatically.
     * This is called per-frame for any housekeeping needed. */
}

/* =========================================================================
 * Sound playback
 * ========================================================================= */

void S_StartSound(const vec3_t origin, int entityNum, int channel,
                  sfxHandle_t sfx, float volume, float minDist, float pitch) {
    if (!snd_initialized) return;
    SND_StartSound(origin, entityNum, channel, sfx, volume, minDist, pitch);
}

void S_StopSound(int entityNum, int channel) {
    if (!snd_initialized) return;
    SND_StopSound(entityNum, channel);
}

void S_StartLocalSound(sfxHandle_t sfx, int channelNum) {
    if (!snd_initialized) return;
    /* Local sounds are non-positional (NULL origin) */
    SND_StartSound(NULL, 0, channelNum, sfx, 1.0f, 0.0f, 1.0f);
}

void S_StartLocalSoundName(const char *sound_name) {
    if (!snd_initialized || !sound_name) return;
    /* Register on the fly and play */
    sfxHandle_t sfx = SND_RegisterSound(sound_name);
    SND_StartSound(NULL, 0, 0, sfx, 1.0f, 0.0f, 1.0f);
}

/* =========================================================================
 * Looping sounds
 * ========================================================================= */

void S_ClearLoopingSounds(void) {
    if (!snd_initialized) return;
    SND_ClearLoopingSounds();
}

void S_AddLoopingSound(const vec3_t origin, const vec3_t velocity,
                       sfxHandle_t sfx, float volume, float minDist) {
    if (!snd_initialized) return;
    (void)velocity; /* velocity-based doppler not yet implemented */
    SND_AddLoopingSound(origin, sfx, volume, minDist);
}

/* =========================================================================
 * Registration
 * ========================================================================= */

static qboolean snd_registering = qfalse;
static int      snd_registrationSequence = 0;

void S_BeginRegistration(void) {
    /* Begin a new level's sound precaching phase.
     * Bumps the registration sequence so we can identify stale sounds
     * from the previous level during EndRegistration. */
    if (!snd_initialized) return;
    snd_registering = qtrue;
    snd_registrationSequence++;
    Com_DPrintf("S_BeginRegistration: sequence %d\n", snd_registrationSequence);
}

sfxHandle_t S_RegisterSound(const char *name) {
    if (!snd_initialized) return 0;
    return SND_RegisterSound(name);
}

void S_EndRegistration(void) {
    /* End the precaching phase. Sounds not touched during this registration
     * pass could be freed here, but for simplicity we keep them cached --
     * memory pressure is lower with modern systems. */
    if (!snd_initialized) return;
    snd_registering = qfalse;
    Com_DPrintf("S_EndRegistration: complete\n");
}

/* =========================================================================
 * Entity updates and spatialization
 * ========================================================================= */

void S_UpdateEntity(int entityNum, const vec3_t origin, const vec3_t velocity,
                    qboolean useListener) {
    if (!snd_initialized) return;
    (void)velocity; /* doppler not implemented */
    (void)useListener;
    SND_UpdateEntityPosition(entityNum, origin);
}

void S_Respatialize(int entityNum, vec3_t origin, vec3_t axis[3]) {
    if (!snd_initialized) return;
    (void)entityNum;
    /* axis[0]=forward, axis[1]=right, axis[2]=up */
    if (origin && axis) {
        SND_UpdateListener(origin, axis[0], axis[1], axis[2]);
    }
}

/* =========================================================================
 * Music system
 *
 * FAKK2's dynamic music system uses mood-based soundtrack selection.
 * Music tracks are organized by mood (normal, action, suspense, etc.)
 * and the game switches between them based on combat/exploration state.
 * ========================================================================= */

/* =========================================================================
 * Music system state
 *
 * FAKK2's music system uses "moods" (normal, action, suspense, etc.)
 * with soundtrack definition files that map moods to track filenames.
 * The system crossfades between tracks when the mood changes.
 *
 * Soundtrack files are text files in the format:
 *   <mood_name> <track_filename>
 * e.g.:
 *   normal   music/jungle_normal.wav
 *   action   music/jungle_action.wav
 *   suspense music/jungle_suspense.wav
 * ========================================================================= */

#define MUSIC_MAX_MOODS     16
#define MUSIC_MOOD_NAME_LEN 32
#define MUSIC_MAX_TRACKS    32

typedef struct {
    char    name[MUSIC_MOOD_NAME_LEN];
    char    filename[MAX_QPATH];
} musicTrack_t;

static struct {
    /* Soundtrack definition */
    char            soundtrackName[MAX_QPATH];
    musicTrack_t    tracks[MUSIC_MAX_TRACKS];
    int             numTracks;

    /* Current playback state */
    int             currentMood;
    int             fallbackMood;
    sfxHandle_t     currentTrack;
    float           musicVolume;
    float           fadeTarget;
    float           fadeRate;        /* volume change per second */

    qboolean        playing;
} music;

static const char *musicMoodNames[] = {
    "normal", "action", "suspense", "mystery", "success",
    "failure", "surprise", "special", "aux1", "aux2",
    "aux3", "aux4", "aux5", "aux6", "aux7", "aux8"
};

void S_StartMusic(const char *intro, const char *loop) {
    if (!snd_initialized) return;

    Com_DPrintf("S_StartMusic: intro=%s loop=%s\n",
                intro ? intro : "(none)", loop ? loop : "(none)");

    /* Register and play the intro track (or loop if no intro) */
    const char *trackName = intro ? intro : loop;
    if (trackName && trackName[0]) {
        music.currentTrack = SND_RegisterSound(trackName);
        SND_StartSound(NULL, -1, 0, music.currentTrack,
                       music.musicVolume, 0.0f, 1.0f);
        music.playing = qtrue;
    }
}

void S_StopMusic(void) {
    if (!snd_initialized) return;
    SND_StopAllSounds();  /* stop all channels including music */
    music.playing = qfalse;
    music.currentTrack = 0;
}

void S_SetMusicMood(int mood, float volume) {
    if (!snd_initialized) return;
    if (mood < 0 || mood >= MUSIC_MAX_MOODS) return;

    music.currentMood = mood;
    music.musicVolume = volume;

    /* Find track for this mood */
    const char *moodName = musicMoodNames[mood];
    for (int i = 0; i < music.numTracks; i++) {
        if (!Q_stricmp(music.tracks[i].name, moodName)) {
            sfxHandle_t newTrack = SND_RegisterSound(music.tracks[i].filename);
            if (newTrack != music.currentTrack) {
                music.currentTrack = newTrack;
                SND_StartSound(NULL, -1, 0, newTrack, volume, 0.0f, 1.0f);
                music.playing = qtrue;
            }
            return;
        }
    }
    Com_DPrintf("S_SetMusicMood: no track for mood '%s'\n", moodName);
}

void MUSIC_NewSoundtrack(const char *name) {
    if (!name || !name[0]) return;

    Com_DPrintf("MUSIC_NewSoundtrack: %s\n", name);
    Q_strncpyz(music.soundtrackName, name, sizeof(music.soundtrackName));
    music.numTracks = 0;

    /* Try to load soundtrack definition file */
    char path[MAX_QPATH];
    snprintf(path, sizeof(path), "music/%s.mus", name);

    void *buffer;
    long len = FS_ReadFile(path, &buffer);
    if (len <= 0 || !buffer) {
        /* Try alternate path */
        snprintf(path, sizeof(path), "sound/music/%s.mus", name);
        len = FS_ReadFile(path, &buffer);
    }

    if (len > 0 && buffer) {
        /* Parse soundtrack: each line is "mood_name filename" */
        const char *data = (const char *)buffer;
        const char *p = data;
        while (*p && music.numTracks < MUSIC_MAX_TRACKS) {
            /* Skip whitespace */
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
            if (!*p || *p == '#') break;

            /* Read mood name */
            char moodName[MUSIC_MOOD_NAME_LEN];
            int mi = 0;
            while (*p && *p != ' ' && *p != '\t' && mi < MUSIC_MOOD_NAME_LEN - 1)
                moodName[mi++] = *p++;
            moodName[mi] = '\0';

            /* Skip whitespace */
            while (*p == ' ' || *p == '\t') p++;

            /* Read filename */
            char filename[MAX_QPATH];
            int fi = 0;
            while (*p && *p != '\r' && *p != '\n' && fi < MAX_QPATH - 1)
                filename[fi++] = *p++;
            filename[fi] = '\0';

            if (moodName[0] && filename[0]) {
                Q_strncpyz(music.tracks[music.numTracks].name, moodName,
                           sizeof(music.tracks[0].name));
                Q_strncpyz(music.tracks[music.numTracks].filename, filename,
                           sizeof(music.tracks[0].filename));
                music.numTracks++;
            }

            /* Skip to next line */
            while (*p && *p != '\n') p++;
        }
        FS_FreeFile(buffer);
        Com_DPrintf("Loaded soundtrack '%s': %d tracks\n", name, music.numTracks);
    } else {
        Com_DPrintf("MUSIC_NewSoundtrack: couldn't load '%s'\n", path);
    }
}

void MUSIC_UpdateMood(int current_mood, int fallback_mood) {
    if (music.currentMood != current_mood) {
        music.currentMood = current_mood;
        music.fallbackMood = fallback_mood;
        S_SetMusicMood(current_mood, music.musicVolume);
    }
}

void MUSIC_UpdateVolume(float volume, float fade_time) {
    music.musicVolume = volume;
    if (fade_time > 0.0f) {
        music.fadeTarget = volume;
        music.fadeRate = 1.0f / fade_time;
    }
    if (snd_initialized) {
        SND_SetVolume(-1.0f, volume); /* -1 = don't change master */
    }
}

/* =========================================================================
 * Reverb and ambient volume
 * ========================================================================= */

/* Current reverb state -- stored for the sound mixer.
 * Full reverb DSP requires an effects pipeline; for now we store the
 * state so it can be queried by the mixer when DSP is added later. */
static int   s_reverbType = 0;
static float s_reverbLevel = 0.0f;

void S_SetReverb(int reverb_type, float reverb_level) {
    s_reverbType = reverb_type;
    s_reverbLevel = reverb_level;
    Com_DPrintf("S_SetReverb: type=%d level=%.2f\n", reverb_type, reverb_level);
}

/* Global ambient volume scale (0.0 - 1.0) applied to all looping sounds */
static float s_ambientVolume = 1.0f;

void S_SetGlobalAmbientVolumeLevel(float volume) {
    s_ambientVolume = volume;
    if (s_ambientVolume < 0.0f) s_ambientVolume = 0.0f;
    if (s_ambientVolume > 1.0f) s_ambientVolume = 1.0f;
    Com_DPrintf("S_SetGlobalAmbientVolumeLevel: %.2f\n", volume);
}

/* =========================================================================
 * Lip sync (Babble system)
 *
 * FAKK2 uses amplitude data from sound files to drive character
 * lip animation during dialogue playback.
 * ========================================================================= */

float S_GetLipLength(const char *name) {
    if (!snd_initialized || !name) return 0.0f;
    sfxHandle_t sfx = SND_RegisterSound(name);
    return SND_SoundLength(sfx);
}

byte *S_GetLipAmplitudes(const char *name, int *number_of_amplitudes) {
    if (!snd_initialized || !name || !number_of_amplitudes) {
        if (number_of_amplitudes) *number_of_amplitudes = 0;
        return NULL;
    }

    /* Register/load the sound to get PCM data */
    sfxHandle_t sfx = SND_RegisterSound(name);
    float duration = SND_SoundLength(sfx);
    if (duration <= 0.0f) {
        *number_of_amplitudes = 0;
        return NULL;
    }

    /* Generate amplitude envelope at ~30 fps for lip sync.
     * The Babble system uses this to drive mouth open/close. */
    extern byte *SND_GetPCMData(sfxHandle_t sfx, int *outLen, int *outRate, int *outBits, int *outChan);
    int dataLen, sampleRate, bitsPerSample, channels;
    byte *pcm = SND_GetPCMData(sfx, &dataLen, &sampleRate, &bitsPerSample, &channels);
    if (!pcm || dataLen <= 0 || sampleRate <= 0) {
        *number_of_amplitudes = 0;
        return NULL;
    }

    int bytesPerSample = (bitsPerSample / 8) * channels;
    int totalSamples = dataLen / bytesPerSample;

    /* 30 fps amplitude windows */
    int framesPerWindow = sampleRate / 30;
    if (framesPerWindow < 1) framesPerWindow = 1;
    int numWindows = totalSamples / framesPerWindow;
    if (numWindows < 1) numWindows = 1;

    byte *amplitudes = (byte *)Z_TagMalloc(numWindows, TAG_GENERAL);

    for (int w = 0; w < numWindows; w++) {
        int startSample = w * framesPerWindow;
        int endSample = startSample + framesPerWindow;
        if (endSample > totalSamples) endSample = totalSamples;

        /* Compute RMS amplitude for this window */
        float sumSq = 0.0f;
        int count = 0;
        for (int s = startSample; s < endSample; s++) {
            float sample;
            if (bitsPerSample == 16) {
                short *src = (short *)pcm;
                sample = (float)src[s * channels] / 32768.0f;
            } else {
                sample = ((float)pcm[s * channels] - 128.0f) / 128.0f;
            }
            sumSq += sample * sample;
            count++;
        }

        float rms = (count > 0) ? sqrtf(sumSq / count) : 0.0f;
        /* Scale to 0-255 byte range with some amplification */
        int val = (int)(rms * 4.0f * 255.0f);
        if (val > 255) val = 255;
        amplitudes[w] = (byte)val;
    }

    *number_of_amplitudes = numWindows;
    return amplitudes;
}

/* =========================================================================
 * Sound queries for game DLL
 * ========================================================================= */

float S_SoundLength(const char *path) {
    if (!snd_initialized || !path) return 0.0f;
    sfxHandle_t sfx = SND_RegisterSound(path);
    return SND_SoundLength(sfx);
}

byte *S_SoundAmplitudes(const char *name, int *number_of_amplitudes) {
    /* Game DLL variant -- delegates to the same lip sync implementation */
    return S_GetLipAmplitudes(name, number_of_amplitudes);
}

/* =========================================================================
 * Listener
 * ========================================================================= */

void S_UpdateListener(const vec3_t origin, const vec3_t forward,
                      const vec3_t right, const vec3_t up) {
    if (!snd_initialized) return;
    SND_UpdateListener(origin, forward, right, up);
}
