# Module Interfaces

## Overview

FAKK2 uses DLL-based module separation (unlike stock Q3's QVM bytecode). Function pointer tables are exchanged at load time:

```
fakk2.exe (engine)
    |
    |-- GetGameAPI(game_import_t*) -> game_export_t*
    |       Server-side game logic (gamex86.dll)
    |       game_import_t: ~60+ engine functions
    |       game_export_t: ~20 game callbacks
    |
    |-- GetCGameAPI() -> clientGameExport_t*
    |       Client-side game logic (cgamex86.dll)
    |       clientGameImport_t: ~90+ engine functions
    |       clientGameExport_t: 6 callbacks
    |
    '-- str class (50 symbols)
            Shared C++ string class, exported by all 3 binaries
            Primary ABI contract
```

## Game API (Server-Side)

**Version**: `GAME_API_VERSION 4`

### game_import_t (engine -> game, ~60+ function pointers)

From SDK `fgame/g_public.h`:

```c
typedef struct {
    /* Printing */
    void    (*Printf)(const char *fmt, ...);
    void    (*DPrintf)(const char *fmt, ...);
    void    (*DebugPrintf)(const char *fmt, ...);
    void    (*Error)(int level, const char *fmt, ...);

    /* Memory */
    void    *(*Malloc)(int size);
    void    (*Free)(void *ptr);

    /* Console variables */
    cvar_t  *(*cvar_set)(const char *name, const char *value);
    cvar_t  *(*cvar_get)(const char *name, const char *defvalue, int flags);

    /* Commands */
    int     (*argc)(void);
    char    *(*argv)(int n);
    char    *(*args)(void);
    void    (*AddCommand)(const char *name);
    void    (*SendConsoleCommand)(const char *text);

    /* Filesystem */
    int     (*FS_ReadFile)(const char *name, void **buf);
    void    (*FS_FreeFile)(void *buf);
    int     (*FS_WriteFile)(const char *name, const void *buf, int len);

    /* Collision */
    void    (*SetBrushModel)(void *ent, const char *name);
    trace_t (*trace)(const vec3_t start, const vec3_t mins, const vec3_t maxs,
                     const vec3_t end, int passEntityNum, int contentMask, int capsule);
    int     (*pointcontents)(const vec3_t point, int passEntityNum);
    qboolean (*inPVS)(const vec3_t p1, const vec3_t p2);

    /* Entity linking */
    void    (*linkentity)(void *ent);
    void    (*unlinkentity)(void *ent);
    int     (*AreaEntities)(const vec3_t mins, const vec3_t maxs, int *list, int maxcount);

    /* Model/TIKI queries */
    int     (*modelindex)(const char *name);
    int     (*imageindex)(const char *name);
    int     (*soundindex)(const char *name);

    /* TIKI-specific */
    dtiki_t (*TIKI_RegisterModel)(const char *name);
    void    *(*TIKI_GetBones)(dtiki_t tiki);
    int     (*TIKI_NumAnims)(dtiki_t tiki);
    const char *(*TIKI_AnimName)(dtiki_t tiki, int index);
    int     (*TIKI_NumSurfaces)(dtiki_t tiki);
    int     (*Tag_NumForName)(dtiki_t tiki, const char *name);
    const char *(*Tag_NameForNum)(dtiki_t tiki, int num);

    /* Alias system (sound/model aliases) */
    void    (*Alias_Add)(const char *alias, const char *name, const char *param);
    const char *(*Alias_FindRandom)(const char *alias, void *data);

    /* Sound */
    void    (*Sound)(vec3_t *origin, int entnum, int channel, const char *name, float vol, float dist, float pitch);
    void    (*StopSound)(int entnum, int channel);

    /* Configstrings */
    void    (*SetConfigstring)(int num, const char *string);
    char    *(*GetConfigstring)(int num);

    /* ... additional functions for:
     *   - Entity spawning
     *   - AI navigation
     *   - Archived configstrings
     *   - Client messaging
     *   - Debug drawing
     */
} game_import_t;
```

### game_export_t (game -> engine, ~20 callbacks)

```c
typedef struct {
    int         apiversion;

    void        (*Init)(int startTime, int randomSeed);
    void        (*Shutdown)(void);
    void        (*Cleanup)(qboolean samemap);

    void        (*SpawnEntities)(const char *mapname, const char *entities);
    const char  *(*ClientConnect)(int clientNum, qboolean firstTime);
    void        (*ClientBegin)(int clientNum);
    void        (*ClientUserinfoChanged)(int clientNum);
    void        (*ClientDisconnect)(int clientNum);
    void        (*ClientCommand)(int clientNum);
    void        (*ClientThink)(int clientNum, usercmd_t *ucmd);

    void        (*RunFrame)(int levelTime, int frameTime);
    void        (*PrepFrame)(void);

    qboolean    (*ConsoleCommand)(void);

    void        (*WriteSaveGame)(void);
    void        (*ReadSaveGame)(void);
} game_export_t;
```

## Client Game API

**Version**: `CGAME_IMPORT_API_VERSION 3`

### clientGameImport_t (engine -> cgame, ~90+ function pointers)

From SDK `cgame/cg_public.h`:

```c
typedef struct {
    /* Core engine functions */
    void    (*Printf)(const char *fmt, ...);
    void    (*DPrintf)(const char *fmt, ...);
    void    (*Error)(int level, const char *fmt, ...);
    void    *(*Malloc)(int size);
    void    (*Free)(void *ptr);

    /* Console variables */
    cvar_t  *(*Cvar_Get)(const char *name, const char *value, int flags);
    void    (*Cvar_Set)(const char *name, const char *value);

    /* Filesystem */
    int     (*FS_ReadFile)(const char *name, void **buf);
    void    (*FS_FreeFile)(void *buf);

    /* Collision model */
    void    (*CM_LoadMap)(const char *name);
    int     (*CM_NumInlineModels)(void);
    clipHandle_t (*CM_InlineModel)(int index);
    int     (*CM_PointContents)(const vec3_t p, clipHandle_t model);
    void    (*CM_BoxTrace)(trace_t *results, const vec3_t start, const vec3_t end,
                           const vec3_t mins, const vec3_t maxs,
                           clipHandle_t model, int brushmask, int capsule);

    /* Sound functions (S_*) */
    void    (*S_StartSound)(const vec3_t origin, int entityNum, int channel,
                            sfxHandle_t sfx, float volume, float dist, float pitch);
    sfxHandle_t (*S_RegisterSound)(const char *name);
    void    (*S_StartLocalSound)(const char *name);
    void    (*S_StopAllSounds)(void);

    /* Music functions (MUSIC_*) */
    void    (*MUSIC_NewSoundtrack)(const char *name);
    void    (*MUSIC_SetMood)(int mood, int val);
    void    (*MUSIC_SetVolume)(float volume);

    /* Renderer functions (R_*) */
    void    (*R_ClearScene)(void);
    void    (*R_AddRefEntityToScene)(const refEntity_t *ent);
    void    (*R_AddLightToScene)(const vec3_t origin, float intensity, float r, float g, float b, int type);
    void    (*R_RenderScene)(const refdef_t *fd);
    void    (*R_SetColor)(const float *rgba);
    void    (*R_DrawStretchPic)(float x, float y, float w, float h,
                                float s1, float t1, float s2, float t2, qhandle_t shader);
    qhandle_t (*R_RegisterModel)(const char *name);
    qhandle_t (*R_RegisterShader)(const char *name);
    qhandle_t (*R_RegisterSkin)(const char *name);
    int     (*R_GetShaderWidth)(qhandle_t shader);
    int     (*R_GetShaderHeight)(qhandle_t shader);
    void    (*R_DrawBox)(float x, float y, float w, float h);
    void    (*R_ModelBounds)(clipHandle_t model, vec3_t mins, vec3_t maxs);

    /* TIKI queries */
    dtiki_t (*TIKI_RegisterModel)(const char *name);
    int     (*TIKI_NumAnims)(dtiki_t tiki);
    const char *(*TIKI_AnimName)(dtiki_t tiki, int index);
    float   (*TIKI_AnimLength)(dtiki_t tiki, int index);
    int     (*TIKI_NumSurfaces)(dtiki_t tiki);

    /* Snapshot access */
    qboolean (*GetSnapshot)(int snapshotNumber, void *snapshot);
    qboolean (*GetServerCommand)(int *serverCommandNumber);
    int     (*GetCurrentSnapshotNumber)(int *snapshotNumber, int *serverTime);

    /* Alias system */
    const char *(*Alias_FindRandom)(const char *alias, void *data);

    /* Commands */
    int     (*Cmd_Argc)(void);
    char    *(*Cmd_Argv)(int n);
    void    (*SendClientCommand)(const char *string);

    /* Configuration */
    void    (*GetGlconfig)(glconfig_t *config);
    void    (*GetGameState)(void *gs);
    void    (*GetConfigstring)(int index, char *buf, int size);

    /* Key binding */
    int     (*Key_GetCatcher)(void);
    void    (*Key_SetCatcher)(int catcher);
    qboolean (*Key_IsDown)(int keynum);
    char    *(*Key_GetBinding)(int keynum);
} clientGameImport_t;
```

### clientGameExport_t (cgame -> engine, 6 callbacks)

```c
typedef struct {
    void    (*CG_Init)(clientGameImport_t *imported, int serverMessageNum, int serverCommandSequence);
    void    (*CG_Shutdown)(void);
    void    (*CG_DrawActiveFrame)(int serverTime, int frameTime, stereoFrame_t stereoView);
    qboolean (*CG_ConsoleCommand)(void);
    void    (*CG_GetRendererConfig)(void);
    void    (*CG_Draw2D)(void);
} clientGameExport_t;
```

## str Class ABI (50 exported symbols)

The shared string class is the binding contract between all three binaries. Every method must be binary-compatible.

Key methods:
- Construction: `str()`, `str(const char*)`, `str(const str&)`, `~str()`
- Assignment: `operator=(const char*)`, `operator=(const str&)`
- Concatenation: `operator+`, `operator+=`, `append`
- Access: `operator[]`, `c_str()`, `length()`
- Comparison: `operator==`, `operator!=`, `cmp`, `icmp`, `cmpn`, `icmpn`
- Modification: `tolower()`, `toupper()`, `capLength()`, `strip()`
- Formatting: `snprintf(const char*, ...)`
- Query: `isNumeric()`
