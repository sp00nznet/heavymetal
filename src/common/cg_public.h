/*
 * cg_public.h -- Client game module public interface
 *
 * Defines the function pointer tables exchanged between the engine
 * and the client-side game module (cgamex86.dll).
 *
 * Based on the FAKK2 SDK cg_public.h.
 * CGAME_IMPORT_API_VERSION 3.
 */

#ifndef CG_PUBLIC_H
#define CG_PUBLIC_H

#include "fakk_types.h"
#include "g_public.h"
#include "../renderer/tr_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CGAME_IMPORT_API_VERSION    3
#define MAX_ENTITIES_IN_SNAPSHOT    256
#define MAX_SERVER_SOUNDS           64
#define MAX_MAP_AREA_BYTES          32

/* =========================================================================
 * Stereo rendering mode
 * ========================================================================= */

typedef enum {
    STEREO_CENTER,
    STEREO_LEFT,
    STEREO_RIGHT
} stereoFrame_t;

/* =========================================================================
 * Mark fragment for decal system
 * ========================================================================= */

typedef struct {
    int     firstPoint;
    int     numPoints;
} markFragment_t;

/* =========================================================================
 * Server sound event
 * ========================================================================= */

typedef struct {
    vec3_t      origin;
    int         entity_number;
    int         channel;
    const char  *sound_name;
    float       volume;
    float       min_dist;
} server_sound_t;

/* =========================================================================
 * Game state (configstrings)
 * ========================================================================= */

typedef struct {
    int         stringOffsets[MAX_CONFIGSTRINGS];
    char        stringData[16384];
    int         dataCount;
} gameState_t;

/* =========================================================================
 * Snapshot -- client view of the game state at a point in time
 * ========================================================================= */

typedef struct {
    int             snapFlags;
    int             ping;
    int             serverTime;
    byte            areamask[MAX_MAP_AREA_BYTES];
    playerState_t   ps;
    int             numEntities;
    entityState_t   entities[MAX_ENTITIES_IN_SNAPSHOT];
    int             numServerCommands;
    int             serverCommandSequence;
    int             number_of_sounds;
    server_sound_t  sounds[MAX_SERVER_SOUNDS];
} snapshot_t;

/* =========================================================================
 * clientGameImport_t -- Engine functions provided TO the client game module
 *
 * ~120+ function pointers covering rendering, sound, collision, TIKI, etc.
 * ========================================================================= */

typedef struct {
    int         apiversion;

    /* --- Core --- */
    void        (*Printf)(const char *fmt, ...);
    void        (*DPrintf)(const char *fmt, ...);
    void        (*DebugPrintf)(const char *fmt, ...);
    void        *(*Malloc)(int size);
    void        (*Free)(void *block);
    void        (*Error)(int level, const char *fmt, ...);
    int         (*Milliseconds)(void);

    /* --- Console Variables --- */
    cvar_t      *(*Cvar_Get)(const char *name, const char *value, int flags);
    void        (*Cvar_Set)(const char *name, const char *value);

    /* --- Commands --- */
    int         (*Argc)(void);
    char        *(*Argv)(int n);
    char        *(*Args)(void);
    void        (*AddCommand)(const char *cmd);

    /* --- Filesystem --- */
    int         (*FS_ReadFile)(const char *name, void **buf, qboolean quiet);
    void        (*FS_FreeFile)(void *buf);
    void        (*FS_WriteFile)(const char *qpath, const void *buffer, int size);
    void        (*FS_WriteTextFile)(const char *qpath, const void *buffer, int size);
    void        (*SendConsoleCommand)(const char *text);
    void        (*UpdateLoadingScreen)(void);

    /* --- Client --- */
    void        (*SendClientCommand)(const char *s);

    /* --- Collision Model --- */
    void        (*CM_LoadMap)(const char *name);
    clipHandle_t (*CM_InlineModel)(int index);
    int         (*CM_NumInlineModels)(void);
    int         (*CM_PointContents)(const vec3_t p, int headnode);
    int         (*CM_TransformedPointContents)(const vec3_t p, int headnode, vec3_t origin, vec3_t angles);
    void        (*CM_BoxTrace)(trace_t *results, const vec3_t start, const vec3_t end,
                               const vec3_t mins, const vec3_t maxs, int headnode,
                               int brushmask, qboolean cylinder);
    void        (*CM_TransformedBoxTrace)(trace_t *results, const vec3_t start, const vec3_t end,
                                          const vec3_t mins, const vec3_t maxs, int headnode,
                                          int brushmask, const vec3_t origin, const vec3_t angles,
                                          qboolean cylinder);
    clipHandle_t (*CM_TempBoxModel)(const vec3_t mins, const vec3_t maxs, int contents);
    int         (*CM_MarkFragments)(int numPoints, const vec3_t *points, const vec3_t projection,
                                    int maxPoints, vec3_t pointBuffer, int maxFragments,
                                    markFragment_t *fragmentBuffer);

    /* --- Sound --- */
    void        (*S_StartSound)(vec3_t origin, int entnum, int entchannel, sfxHandle_t sfx,
                                float volume, float min_dist);
    void        (*S_StartLocalSound)(const char *sound_name);
    void        (*S_StopSound)(int entnum, int channel);
    void        (*S_ClearLoopingSounds)(void);
    void        (*S_AddLoopingSound)(const vec3_t origin, const vec3_t velocity,
                                     sfxHandle_t sfx, float volume, float min_dist);
    void        (*S_Respatialize)(int entityNum, vec3_t origin, vec3_t axis[3]);
    void        (*S_BeginRegistration)(void);
    sfxHandle_t (*S_RegisterSound)(const char *sample);
    void        (*S_EndRegistration)(void);
    void        (*S_UpdateEntity)(int entityNum, const vec3_t origin, const vec3_t velocity,
                                  qboolean use_listener);
    void        (*S_SetReverb)(int reverb_type, float reverb_level);
    void        (*S_SetGlobalAmbientVolumeLevel)(float volume);

    /* --- Music --- */
    void        (*MUSIC_NewSoundtrack)(const char *name);
    void        (*MUSIC_UpdateMood)(int current_mood, int fallback_mood);
    void        (*MUSIC_UpdateVolume)(float volume, float fade_time);

    /* --- Lip sync --- */
    float       (*get_lip_length)(const char *name);
    byte        *(*get_lip_amplitudes)(const char *name, int *number_of_amplitudes);

    /* --- Camera --- */
    float       *(*get_camera_offset)(qboolean *lookactive, qboolean *resetview);

    /* --- Renderer --- */
    void        (*BeginRegistration)(void);
    void        (*EndRegistration)(void);
    void        (*R_ClearScene)(void);
    void        (*R_RenderScene)(const refdef_t *fd);
    void        (*R_LoadWorldMap)(const char *mapname);
    qhandle_t   (*R_RegisterModel)(const char *name);
    qhandle_t   (*R_RegisterSkin)(const char *name);
    qhandle_t   (*R_RegisterShader)(const char *name);
    qhandle_t   (*R_RegisterShaderNoMip)(const char *name);
    void        (*R_AddRefEntityToScene)(refEntity_t *ent);
    void        (*R_AddRefSpriteToScene)(refEntity_t *ent);
    void        (*R_AddLightToScene)(vec3_t origin, float intensity, float r, float g, float b, int type);
    void        (*R_AddPolyToScene)(qhandle_t hShader, int numVerts, const polyVert_t *verts, int renderfx);
    void        (*R_SetColor)(const vec4_t rgba);
    void        (*R_DrawStretchPic)(float x, float y, float w, float h,
                                    float s1, float t1, float s2, float t2, qhandle_t hShader);
    refEntity_t *(*R_GetRenderEntity)(int entityNumber);
    void        (*R_ModelBounds)(clipHandle_t model, vec3_t mins, vec3_t maxs);
    float       (*R_ModelRadius)(clipHandle_t model);
    float       (*R_Noise)(float x, float y, float z, float t);
    void        (*R_DebugLine)(vec3_t start, vec3_t end, float r, float g, float b, float alpha);

    /* --- Swipes (weapon trail effects) --- */
    void        (*R_SwipeBegin)(float thistime, float life, qhandle_t shader);
    void        (*R_SwipePoint)(vec3_t p1, vec3_t p2, float time);
    void        (*R_SwipeEnd)(void);
    int         (*R_GetShaderWidth)(qhandle_t shader);
    int         (*R_GetShaderHeight)(qhandle_t shader);
    void        (*R_DrawBox)(float x, float y, float w, float h);

    /* --- Client state --- */
    void        (*GetGameState)(gameState_t *gamestate);
    int         (*GetSnapshot)(int snapshotNumber, snapshot_t *snapshot);
    void        (*GetCurrentSnapshotNumber)(int *snapshotNumber, int *serverTime);
    void        (*GetGlconfig)(glconfig_t *glconfig);
    qboolean    (*GetParseEntityState)(int parseEntityNumber, entityState_t *state);
    int         (*GetCurrentCmdNumber)(void);
    qboolean    (*GetUserCmd)(int cmdNumber, usercmd_t *ucmd);
    qboolean    (*GetServerCommand)(int serverCommandNumber);

    /* --- Alias system --- */
    qboolean    (*Alias_Add)(const char *alias, const char *name, const char *parameters);
    const char  *(*Alias_FindRandom)(const char *alias);
    void        (*Alias_Dump)(void);
    void        (*Alias_Clear)(void);

    /* --- TIKI queries --- */
    int         (*TIKI_GetHandle)(qhandle_t handle);
    int         (*NumAnims)(int tikihandle);
    int         (*NumSkins)(int tikihandle);
    int         (*NumSurfaces)(int tikihandle);
    int         (*NumTags)(int tikihandle);
    qboolean    (*InitCommands)(int tikihandle, tiki_cmd_t *tiki_cmd);
    void        (*CalculateBounds)(int tikihandle, float scale, vec3_t mins, vec3_t maxs);
    void        (*FlushAll)(void);
    const char  *(*TIKI_NameForNum)(int tikihandle);

    /* --- Animation queries --- */
    const char  *(*Anim_NameForNum)(int tikihandle, int animnum);
    int         (*Anim_NumForName)(int tikihandle, const char *name);
    int         (*Anim_Random)(int tikihandle, const char *name);
    int         (*Anim_NumFrames)(int tikihandle, int animnum);
    float       (*Anim_Time)(int tikihandle, int animnum);
    void        (*Anim_Delta)(int tikihandle, int animnum, vec3_t delta);
    int         (*Anim_Flags)(int tikihandle, int animnum);
    int         (*Anim_CrossblendTime)(int tikihandle, int animnum);
    qboolean    (*Anim_HasCommands)(int tikihandle, int animnum);

    /* --- Frame queries --- */
    qboolean    (*Frame_Commands)(int tikihandle, int animnum, int framenum, tiki_cmd_t *tiki_cmd);
    void        (*Frame_Delta)(int tikihandle, int animnum, int framenum, vec3_t delta);
    float       (*Frame_Time)(int tikihandle, int animnum, int framenum);
    void        (*Frame_Bounds)(int tikihandle, int animnum, int framenum, float scale, vec3_t mins, vec3_t maxs);
    float       (*Frame_Radius)(int tikihandle, int animnum, int framenum);

    /* --- Surface queries --- */
    int         (*Surface_NameToNum)(int tikihandle, const char *name);
    const char  *(*Surface_NumToName)(int tikihandle, int num);
    int         (*Surface_Flags)(int tikihandle, int num);
    int         (*Surface_NumSkins)(int tikihandle, int num);

    /* --- Tag (bone) queries --- */
    int         (*Tag_NumForName)(int tikihandle, const char *name);
    const char  *(*Tag_NameForNum)(int tikihandle, int num);
    orientation_t (*Tag_Orientation)(int tikihandle, int anim, int frame, int tagnum,
                                     float scale, int *bone_tag, vec4_t *bone_quat);

    /* --- TIKI alias system --- */
    qboolean    (*TIKI_Alias_Add)(int tikihandle, const char *alias, const char *name, const char *parameters);
    const char  *(*TIKI_Alias_FindRandom)(int tikihandle, const char *alias);
    void        (*TIKI_Alias_Dump)(int tikihandle);
    void        (*TIKI_Alias_Clear)(int tikihandle);
    const char  *(*TIKI_Alias_FindDialog)(int tikihandle, const char *alias, int random, int entity_number);

} clientGameImport_t;

/* =========================================================================
 * clientGameExport_t -- Client game callbacks provided BY the cgame module
 * ========================================================================= */

typedef struct {
    void        (*CG_Init)(clientGameImport_t *imported, int serverMessageNum, int serverCommandSequence);
    void        (*CG_Shutdown)(void);
    void        (*CG_DrawActiveFrame)(int serverTime, stereoFrame_t stereoView, qboolean demoPlayback);
    qboolean    (*CG_ConsoleCommand)(void);
    void        (*CG_GetRendererConfig)(void);
    void        (*CG_Draw2D)(void);
} clientGameExport_t;

/* =========================================================================
 * GetCGameAPI -- the DLL entry point
 * ========================================================================= */

typedef clientGameExport_t *(*GetCGameAPI_t)(void);

#ifdef __cplusplus
}
#endif

#endif /* CG_PUBLIC_H */
