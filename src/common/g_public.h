/*
 * g_public.h -- Game module public interface
 *
 * Defines the function pointer tables exchanged between the engine
 * (fakk2.exe) and the server-side game module (gamex86.dll).
 *
 * Based on the FAKK2 SDK g_public.h. This is the engine-side view.
 * GAME_API_VERSION 4.
 */

#ifndef G_PUBLIC_H
#define G_PUBLIC_H

#include "fakk_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GAME_API_VERSION    4

/* =========================================================================
 * Forward declarations
 * ========================================================================= */

struct gentity_s;
typedef struct gentity_s gentity_t;

/* TIKI command structure passed to game for frame events */
typedef struct {
    int     num_cmds;
    struct {
        int     argc;
        char    **argv;
    } cmds[128];
} tiki_cmd_t;

/* Orientation result from tag queries */
typedef struct {
    vec3_t  origin;
    vec3_t  axis[3];
} orientation_t;

/* =========================================================================
 * SVFlags -- entity server flags
 * ========================================================================= */

#define SVF_NOCLIENT            (1 << 0)
#define SVF_BOT                 (1 << 1)
#define SVF_BROADCAST           (1 << 2)
#define SVF_PORTAL              (1 << 3)
#define SVF_SENDPVS             (1 << 4)
#define SVF_USE_CURRENT_ORIGIN  (1 << 5)
#define SVF_DEADMONSTER         (1 << 6)
#define SVF_MONSTER             (1 << 7)
#define SVF_USEBBOX             (1 << 9)
#define SVF_ONLYPARENT          (1 << 10)
#define SVF_HIDEOWNER           (1 << 11)
#define SVF_MONSTERCLIP         (1 << 12)
#define SVF_PLAYERCLIP          (1 << 13)
#define SVF_SENDONCE            (1 << 14)
#define SVF_SENT                (1 << 15)

/* =========================================================================
 * Solid types
 * ========================================================================= */

typedef enum {
    SOLID_NOT,
    SOLID_TRIGGER,
    SOLID_BBOX,
    SOLID_BSP
} solid_t;

/* =========================================================================
 * gentity_s -- server-side entity structure
 *
 * The engine manages the first portion of this struct. The game module
 * can extend it with additional fields after the engine-defined portion.
 * ========================================================================= */

struct gentity_s {
    entityState_t   s;              /* networked entity state */
    struct playerState_s *client;

    qboolean        inuse;
    qboolean        linked;
    int             linkcount;

    int             svFlags;

    qboolean        bmodel;
    vec3_t          mins, maxs;
    int             contents;

    vec3_t          absmin, absmax;

    float           radius;
    vec3_t          centroid;
    int             areanum;

    vec3_t          currentOrigin;
    vec3_t          currentAngles;

    int             ownerNum;

    solid_t         solid;

    /* Game DLL adds its own fields after this point */
};

/* =========================================================================
 * Debug line structure
 * ========================================================================= */

typedef struct {
    vec3_t  start;
    vec3_t  end;
    float   color[4];   /* RGBA */
} debugline_t;

/* =========================================================================
 * game_import_t -- Engine functions provided TO the game module
 *
 * The engine fills this struct and passes it to GetGameAPI().
 * ~130+ function pointers covering all engine services the game needs.
 * ========================================================================= */

typedef struct {
    /* --- Printing --- */
    void        (*Printf)(const char *fmt, ...);
    void        (*DPrintf)(const char *fmt, ...);
    void        (*DebugPrintf)(const char *fmt, ...);
    void        (*Error)(int level, const char *fmt, ...);

    /* --- Timing --- */
    int         (*Milliseconds)(void);

    /* --- Memory --- */
    void        *(*Malloc)(int size);
    void        (*Free)(void *block);

    /* --- Console Variables --- */
    cvar_t      *(*cvar)(const char *name, const char *value, int flags);
    void        (*cvar_set)(const char *name, const char *value);

    /* --- Commands --- */
    int         (*argc)(void);
    char        *(*argv)(int n);
    const char  *(*args)(void);
    void        (*AddCommand)(const char *cmd);

    /* --- Filesystem --- */
    int         (*FS_ReadFile)(const char *name, void **buf, qboolean quiet);
    void        (*FS_FreeFile)(void *buf);
    void        (*FS_WriteFile)(const char *qpath, const void *buffer, int size);
    fileHandle_t (*FS_FOpenFileWrite)(const char *qpath);
    fileHandle_t (*FS_FOpenFileAppend)(const char *filename);
    char        *(*FS_PrepFileWrite)(const char *filename);
    int         (*FS_Write)(const void *buffer, int len, fileHandle_t f);
    int         (*FS_Read)(void *buffer, int len, fileHandle_t f);
    void        (*FS_FCloseFile)(fileHandle_t f);
    int         (*FS_FTell)(fileHandle_t f);
    int         (*FS_FSeek)(fileHandle_t f, long offset, int origin);
    void        (*FS_Flush)(fileHandle_t f);

    const char  *(*GetArchiveFileName)(const char *filename, const char *extension);
    void        (*SendConsoleCommand)(const char *text);
    void        (*DebugGraph)(float value, int color);

    /* --- Server --- */
    void        (*SendServerCommand)(int clientnum, const char *fmt, ...);
    void        (*setConfigstring)(int index, const char *val);
    char        *(*getConfigstring)(int index);
    void        (*setUserinfo)(int index, const char *val);
    void        (*getUserinfo)(int index, char *buffer, int bufferSize);

    /* --- Collision --- */
    void        (*SetBrushModel)(gentity_t *ent, const char *name);
    void        (*trace)(trace_t *result, const vec3_t start, const vec3_t mins,
                         const vec3_t maxs, const vec3_t end, int passEntityNum,
                         int contentmask, qboolean cylinder);
    int         (*pointcontents)(const vec3_t point, int passEntityNum);
    int         (*pointbrushnum)(const vec3_t point, int passEntityNum);
    qboolean    (*inPVS)(vec3_t p1, vec3_t p2);
    qboolean    (*inPVSIgnorePortals)(vec3_t p1, vec3_t p2);
    void        (*AdjustAreaPortalState)(gentity_t *ent, qboolean open);
    qboolean    (*AreasConnected)(int area1, int area2);

    /* --- Entity linking --- */
    void        (*linkentity)(gentity_t *ent);
    void        (*unlinkentity)(gentity_t *ent);
    int         (*AreaEntities)(vec3_t mins, vec3_t maxs, int *list, int maxcount);
    void        (*ClipToEntity)(trace_t *trace, const vec3_t start, const vec3_t mins,
                                const vec3_t maxs, const vec3_t end, int entityNum,
                                int contentmask);

    /* --- Resource indexing --- */
    int         (*imageindex)(const char *name);
    int         (*itemindex)(const char *name);
    int         (*soundindex)(const char *name);
    int         (*modelindex)(const char *name);

    /* --- Rendering --- */
    void        (*SetLightStyle)(int i, const char *data);
    const char  *(*GameDir)(void);
    qboolean    (*IsModel)(int index);
    void        (*setmodel)(gentity_t *ent, const char *name);

    /* --- TIKI model functions --- */
    int         (*NumAnims)(int modelindex);
    int         (*NumSkins)(int modelindex);
    int         (*NumSurfaces)(int modelindex);
    int         (*NumTags)(int modelindex);
    qboolean    (*InitCommands)(int modelindex, tiki_cmd_t *tiki_cmd);
    void        (*CalculateBounds)(int modelindex, float scale, vec3_t mins, vec3_t maxs);

    /* --- Animation queries --- */
    const char  *(*Anim_NameForNum)(int modelindex, int animnum);
    int         (*Anim_NumForName)(int modelindex, const char *name);
    int         (*Anim_Random)(int modelindex, const char *name);
    int         (*Anim_NumFrames)(int modelindex, int animnum);
    float       (*Anim_Time)(int modelindex, int animnum);
    void        (*Anim_Delta)(int modelindex, int animnum, vec3_t delta);
    void        (*Anim_AbsoluteDelta)(int modelindex, int animnum, vec3_t delta);
    int         (*Anim_Flags)(int modelindex, int animnum);
    qboolean    (*Anim_HasCommands)(int modelindex, int animnum);

    /* --- Frame queries --- */
    qboolean    (*Frame_Commands)(int modelindex, int animnum, int framenum, tiki_cmd_t *tiki_cmd);
    void        (*Frame_Delta)(int modelindex, int animnum, int framenum, vec3_t delta);
    float       (*Frame_Time)(int modelindex, int animnum, int framenum);
    void        (*Frame_Bounds)(int modelindex, int animnum, int framenum, float scale, vec3_t mins, vec3_t maxs);

    /* --- Surface queries --- */
    int         (*Surface_NameToNum)(int modelindex, const char *name);
    const char  *(*Surface_NumToName)(int modelindex, int num);
    int         (*Surface_Flags)(int modelindex, int num);
    int         (*Surface_NumSkins)(int modelindex, int num);

    /* --- Tag (bone) queries --- */
    int         (*Tag_NumForName)(int modelindex, const char *name);
    const char  *(*Tag_NameForNum)(int modelindex, int num);
    orientation_t (*Tag_Orientation)(int modelindex, int anim, int frame, int num,
                                     float scale, int *bone_tag, vec4_t *bone_quat);

    /* --- Alias system --- */
    qboolean    (*Alias_Add)(int modelindex, const char *alias, const char *name, const char *parameters);
    const char  *(*Alias_FindRandom)(int modelindex, const char *alias);
    void        (*Alias_Dump)(int modelindex);
    void        (*Alias_Clear)(int modelindex);
    const char  *(*Alias_FindDialog)(int modelindex, const char *alias, int random, int entity_number);
    void        *(*Alias_GetList)(int modelindex);
    void        (*Alias_UpdateDialog)(int model_index, const char *alias, int number_of_times_played,
                                      byte been_played_this_loop, int last_time_played);
    void        (*Alias_AddActorDialog)(int model_index, const char *alias, int actor_number,
                                        int number_of_times_played, byte been_played_this_loop,
                                        int last_time_played);

    const char  *(*NameForNum)(int modelindex);

    /* --- Global alias system --- */
    qboolean    (*GlobalAlias_Add)(const char *alias, const char *name, const char *parameters);
    const char  *(*GlobalAlias_FindRandom)(const char *alias);
    void        (*GlobalAlias_Dump)(void);
    void        (*GlobalAlias_Clear)(void);

    /* --- Screen printing --- */
    void        (*centerprintf)(gentity_t *ent, const char *fmt, ...);
    void        (*locationprintf)(gentity_t *ent, int x, int y, const char *fmt, ...);

    /* --- Sound --- */
    void        (*Sound)(vec3_t *org, int entnum, int channel, const char *sound_name,
                         float volume, float attenuation);
    void        (*StopSound)(int entnum, int channel);
    float       (*SoundLength)(const char *path);
    byte        *(*SoundAmplitudes)(const char *name, int *number_of_amplitudes);

    /* --- CRC --- */
    unsigned short (*CalcCRC)(const unsigned char *start, int count);

    /* --- Debug --- */
    debugline_t **DebugLines;
    int         *numDebugLines;

    /* --- Entity management --- */
    void        (*LocateGameData)(gentity_t *gEnts, int numGEntities, int sizeofGEntity_t,
                                  playerState_t *clients, int sizeofGameClient);

    /* --- Rendering control --- */
    void        (*SetFarPlane)(int farplane);
    void        (*SetSkyPortal)(qboolean skyportal);

} game_import_t;

/* =========================================================================
 * game_export_t -- Game callbacks provided BY the game module
 *
 * The game module's GetGameAPI() function returns this struct.
 * ========================================================================= */

typedef struct {
    int         apiversion;

    /* Lifecycle */
    void        (*Init)(int startTime, int randomSeed);
    void        (*Shutdown)(void);
    void        (*Cleanup)(void);

    /* Level */
    void        (*SpawnEntities)(const char *mapname, const char *entstring, int levelTime);

    /* Client management */
    char        *(*ClientConnect)(int clientNum, qboolean firstTime);
    void        (*ClientBegin)(gentity_t *ent, usercmd_t *cmd);
    void        (*ClientUserinfoChanged)(gentity_t *ent, const char *userinfo);
    void        (*ClientDisconnect)(gentity_t *ent);
    void        (*ClientCommand)(gentity_t *ent);
    void        (*ClientThink)(gentity_t *ent, usercmd_t *cmd);

    /* Bots */
    void        (*BotBegin)(gentity_t *ent);
    void        (*BotThink)(gentity_t *ent, int msec);

    /* Frame */
    void        (*PrepFrame)(void);
    void        (*RunFrame)(int levelTime, int frameTime);

    /* Console */
    qboolean    (*ConsoleCommand)(void);

    /* Save/Load */
    void        (*WritePersistant)(const char *filename);
    qboolean    (*ReadPersistant)(const char *filename);
    void        (*WriteLevel)(const char *filename, qboolean autosave);
    qboolean    (*ReadLevel)(const char *filename);
    qboolean    (*LevelArchiveValid)(const char *filename);

    /* Global state exposed to engine */
    gentity_t   *gentities;
    int         gentitySize;
    int         num_entities;
    int         max_entities;

    const char  *error_message;

} game_export_t;

/* =========================================================================
 * GetGameAPI -- the DLL entry point
 * ========================================================================= */

typedef game_export_t *(*GetGameAPI_t)(game_import_t *import);

#ifdef __cplusplus
}
#endif

#endif /* G_PUBLIC_H */
