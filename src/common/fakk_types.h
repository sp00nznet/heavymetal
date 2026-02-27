/*
 * fakk_types.h -- Core type definitions for FAKK2 recomp
 *
 * Heavy Metal: FAKK2 Static Recompilation Project
 * Engine: id Tech 3 + Ritual UberTools (2000)
 * Original: MSVC 6.0, 32-bit x86, Windows
 * Target: MSVC/GCC/Clang, 64-bit x86_64, Windows 11
 *
 * This header defines the canonical types used throughout the recomp.
 * All engine modules should include this rather than using raw C types.
 */

#ifndef FAKK_TYPES_H
#define FAKK_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* =========================================================================
 * Fixed-width integer types
 * Original used Win32 DWORD/WORD/BYTE and MSVC __int64
 * ========================================================================= */

typedef uint8_t     byte;
typedef int8_t      sbyte;
typedef uint16_t    word;
typedef int16_t     sword;
typedef uint32_t    dword;
typedef int32_t     sdword;
typedef uint64_t    qword;
typedef int64_t     sqword;

/* =========================================================================
 * Quake3/UberTools base types
 * ========================================================================= */

typedef float       vec_t;
typedef vec_t       vec2_t[2];
typedef vec_t       vec3_t[3];
typedef vec_t       vec4_t[4];
typedef vec_t       mat3_t[3][3];  /* 3x3 rotation matrix */
typedef vec_t       mat4_t[4][4];  /* 4x4 transformation matrix */

typedef int         qboolean;
#define qtrue       1
#define qfalse      0

typedef int         qhandle_t;     /* generic engine handle */
typedef int         sfxHandle_t;   /* sound effect handle */
typedef int         fileHandle_t;  /* file system handle */
typedef int         clipHandle_t;  /* collision model handle */

/* =========================================================================
 * Engine constants
 * ========================================================================= */

#define MAX_STRING_CHARS    1024    /* max length of a string passed to Cmd_TokenizeString */
#define MAX_STRING_TOKENS   1024    /* max tokens resulting from Cmd_TokenizeString */
#define MAX_TOKEN_CHARS     1024    /* max length of an individual token */

#define MAX_INFO_STRING     1024
#define MAX_INFO_KEY        1024
#define MAX_INFO_VALUE      1024

#define BIG_INFO_STRING     8192
#define BIG_INFO_KEY        8192
#define BIG_INFO_VALUE      8192

#define MAX_QPATH           64      /* max quake path length */
#define MAX_OSPATH          256     /* max OS file path length */

#define MAX_CLIENTS         64      /* max number of connected clients */
#define MAX_GENTITIES       1024    /* max game entities */
#define MAX_MODELS          256
#define MAX_SOUNDS          256
#define MAX_CONFIGSTRINGS   2048

/* UberTools-specific constants */
#define TIKI_MAX_SURFACES   32      /* max surfaces per TIKI model */
#define TIKI_MAX_BONES      128     /* max bones in skeleton */
#define TIKI_MAX_ANIMS      512     /* max animation sequences */
#define TIKI_MAX_COMMANDS    64     /* max frame commands per anim */

#define MORPHEUS_MAX_COMMANDS  700  /* ~700 script commands per Ritual docs */

#define GHOST_MAX_EMITTERS   256   /* max simultaneous particle emitters */
#define GHOST_MAX_PARAMS      50   /* ~50 customizable params per emitter */

/* =========================================================================
 * Entity state -- transmitted over the network
 * Matches original entityState_t layout from q_shared.h (SDK)
 * ========================================================================= */

typedef struct entityState_s {
    int         number;             /* entity index */
    int         eType;              /* entityType_t */
    int         eFlags;

    /* trajectory for position and angles */
    struct {
        int     trType;             /* trType_t */
        int     trTime;
        int     trDuration;
        vec3_t  trBase;
        vec3_t  trDelta;
    } pos, apos;

    int         time;
    int         time2;

    vec3_t      origin;
    vec3_t      origin2;
    vec3_t      angles;
    vec3_t      angles2;

    int         otherEntityNum;
    int         otherEntityNum2;
    int         groundEntityNum;

    int         constantLight;      /* r + (g<<8) + (b<<16) + (intensity<<24) */
    int         loopSound;          /* constantly loop this sound */
    int         loopSoundVolume;
    int         loopSoundMinDist;
    float       loopSoundMaxDist;
    float       loopSoundPitch;
    int         loopSoundFlags;

    int         parent;
    int         tag_num;

    qboolean    attach_use_angles;
    vec3_t      attach_offset;

    int         beam_entnum;

    int         modelindex;
    int         usageIndex;
    int         skinNum;
    int         wasframe;
    int         frameInfo[16];      /* TIKI frame info -- 16 anim channels */

    float       actionWeight;

    int         bone_tag[5];
    vec3_t      bone_angles[5];

    int         clientNum;
    int         groundPlane;
    int         solid;

    float       scale;
    float       alpha;
    int         renderfx;
    float       shader_data[2];
    float       shader_time;

    /* TIKI/UberTools surface flags */
    int         surfaces[TIKI_MAX_SURFACES];
} entityState_t;

/* =========================================================================
 * Player state -- per-client game state
 * ========================================================================= */

typedef struct playerState_s {
    int         commandTime;        /* cmd->serverTime of last executed command */
    int         pm_type;
    int         pm_flags;
    int         pm_time;

    int         bobCycle;
    vec3_t      origin;
    vec3_t      velocity;

    int         gravity;
    int         speed;
    int         delta_angles[3];

    int         groundEntityNum;

    int         legsTimer;
    int         legsAnim;
    int         torsoTimer;
    int         torsoAnim;

    int         movementDir;

    vec3_t      grapplePoint;

    int         clientNum;
    vec3_t      viewangles;
    int         viewheight;

    float       fLeanAngle;

    int         stats[32];
    int         activeItems[8];
    int         ammo_name_index[16];
    int         ammo_amount[16];
    int         max_ammo_amount[16];

    /* UberTools additions */
    int         current_music_mood;
    int         fallback_music_mood;
    float       music_volume;
    float       music_volume_fade_time;
    int         reverb_type;
    float       reverb_level;

    float       blend[4];           /* screen blend (damage flash, etc.) */
    float       fov;
    float       camera_origin[3];
    float       camera_angles[3];
    int         camera_flags;
    float       camera_offset;
    float       camera_posofs[3];

    int         voted;
} playerState_t;

/* =========================================================================
 * User command -- input from client
 * ========================================================================= */

typedef struct usercmd_s {
    int         serverTime;
    int         buttons;
    byte        weapon;
    int         angles[3];
    signed char forwardmove;
    signed char rightmove;
    signed char upmove;
} usercmd_t;

/* =========================================================================
 * Version info
 * ========================================================================= */

#define FAKK_VERSION        "1.02"
#define FAKK_ENGINE_VERSION "FAKK2 v" FAKK_VERSION " (recomp)"
#define FAKK_GAME_DIR       "fakk"

/* BSP version -- from qfiles.h in SDK */
#define BSP_HEADER          "FAKK"
#define BSP_VERSION         12

/* GAME_API_VERSION from g_public.h */
#define GAME_API_VERSION    4

/* CGAME_IMPORT_API_VERSION from cg_public.h */
#define CGAME_API_VERSION   3

/* =========================================================================
 * Function attributes
 * ========================================================================= */

#ifdef _MSC_VER
    #define FAKK_EXPORT     __declspec(dllexport)
    #define FAKK_IMPORT     __declspec(dllimport)
    #define FAKK_INLINE     __forceinline
    #define FAKK_NORETURN   __declspec(noreturn)
#else
    #define FAKK_EXPORT     __attribute__((visibility("default")))
    #define FAKK_IMPORT
    #define FAKK_INLINE     static inline __attribute__((always_inline))
    #define FAKK_NORETURN   __attribute__((noreturn))
#endif

#ifdef __cplusplus
    #define FAKK_EXTERN_C   extern "C"
#else
    #define FAKK_EXTERN_C
#endif

#endif /* FAKK_TYPES_H */
