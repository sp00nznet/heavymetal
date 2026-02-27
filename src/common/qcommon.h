/*
 * qcommon.h -- Engine-side definitions for FAKK2 recomp
 *
 * This header defines the engine's internal subsystem interfaces.
 * Based on SDK qcommon.h and id Tech 3 architecture.
 */

#ifndef QCOMMON_H
#define QCOMMON_H

#include "fakk_types.h"
#include "q_shared.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Message I/O -- network packet construction
 * ========================================================================= */

typedef struct {
    qboolean    allowoverflow;
    qboolean    overflowed;
    qboolean    oob;
    byte        *data;
    int         maxsize;
    int         cursize;
    int         readcount;
    int         bit;
} msg_t;

void    MSG_Init(msg_t *buf, byte *data, int length);
void    MSG_Clear(msg_t *buf);
void    MSG_WriteBits(msg_t *msg, int value, int bits);
int     MSG_ReadBits(msg_t *msg, int bits);
void    MSG_WriteByte(msg_t *sb, int c);
void    MSG_WriteShort(msg_t *sb, int c);
void    MSG_WriteLong(msg_t *sb, int c);
void    MSG_WriteFloat(msg_t *sb, float f);
void    MSG_WriteString(msg_t *sb, const char *s);
void    MSG_WriteAngle(msg_t *sb, float f);
void    MSG_WriteAngle16(msg_t *sb, float f);
void    MSG_WriteCoord(msg_t *sb, float f);
void    MSG_WriteDir(msg_t *sb, const vec3_t dir);
void    MSG_WriteData(msg_t *sb, const void *data, int length);
int     MSG_ReadByte(msg_t *msg);
int     MSG_ReadShort(msg_t *msg);
int     MSG_ReadLong(msg_t *msg);
float   MSG_ReadFloat(msg_t *msg);
char    *MSG_ReadString(msg_t *msg);
char    *MSG_ReadStringLine(msg_t *msg);
float   MSG_ReadAngle(msg_t *msg);
float   MSG_ReadAngle16(msg_t *msg);
float   MSG_ReadCoord(msg_t *msg);
void    MSG_ReadDir(msg_t *msg, vec3_t dir);
void    MSG_ReadData(msg_t *msg, void *data, int length);
void    MSG_WriteDeltaEntity(msg_t *msg, entityState_t *from, entityState_t *to, qboolean force);
void    MSG_ReadDeltaEntity(msg_t *msg, entityState_t *from, entityState_t *to, int number);
void    MSG_WriteDeltaPlayerstate(msg_t *msg, playerState_t *from, playerState_t *to);
void    MSG_ReadDeltaPlayerstate(msg_t *msg, playerState_t *from, playerState_t *to);

/* =========================================================================
 * Command buffer
 * ========================================================================= */

void    Cbuf_Init(void);
void    Cbuf_AddText(const char *text);
void    Cbuf_InsertText(const char *text);
void    Cbuf_Execute(void);

/* =========================================================================
 * Command system
 * ========================================================================= */

typedef void (*xcommand_t)(void);

void    Cmd_Init(void);
void    Cmd_AddCommand(const char *name, xcommand_t function);
void    Cmd_RemoveCommand(const char *name);
int     Cmd_Argc(void);
char    *Cmd_Argv(int arg);
char    *Cmd_Args(void);
void    Cmd_TokenizeString(const char *text);
void    Cmd_ExecuteString(const char *text);

/* =========================================================================
 * Cvar system
 * ========================================================================= */

cvar_t  *Cvar_Get(const char *name, const char *value, int flags);
cvar_t  *Cvar_Set(const char *name, const char *value);
cvar_t  *Cvar_SetValue(const char *name, float value);
float   Cvar_VariableValue(const char *name);
int     Cvar_VariableIntegerValue(const char *name);
char    *Cvar_VariableString(const char *name);
void    Cvar_Init(void);

/* =========================================================================
 * Filesystem -- PK3 (ZIP) based, like Q3
 * FAKK2 uses fakk/ as the base game directory (equivalent to baseq3/)
 * ========================================================================= */

void    FS_Init(void);
void    FS_Shutdown(void);
int     FS_FOpenFileRead(const char *filename, fileHandle_t *file, qboolean uniqueFILE);
int     FS_FOpenFileWrite(const char *filename, fileHandle_t *file);
void    FS_FCloseFile(fileHandle_t f);
int     FS_Read(void *buffer, int len, fileHandle_t f);
int     FS_Write(const void *buffer, int len, fileHandle_t f);
int     FS_Seek(fileHandle_t f, long offset, int origin);
int     FS_FTell(fileHandle_t f);
void    FS_Flush(fileHandle_t f);
char    **FS_ListFiles(const char *directory, const char *extension, int *numfiles);
void    FS_FreeFileList(char **filelist);
int     FS_GetFileList(const char *path, const char *extension, char *listbuf, int bufsize);
long    FS_ReadFile(const char *qpath, void **buffer);
void    FS_FreeFile(void *buffer);
void    FS_WriteFile(const char *qpath, const void *buffer, int size);

/* =========================================================================
 * Network
 * ========================================================================= */

typedef enum {
    NA_BAD,
    NA_LOOPBACK,
    NA_BROADCAST,
    NA_IP
} netadrtype_t;

typedef struct {
    netadrtype_t    type;
    byte            ip[4];
    unsigned short  port;
} netadr_t;

void    NET_Init(void);
void    NET_Shutdown(void);
void    NET_SendPacket(int length, const void *data, netadr_t to);
qboolean NET_GetPacket(netadr_t *from, msg_t *msg);
qboolean NET_StringToAdr(const char *s, netadr_t *a);
char    *NET_AdrToString(netadr_t a);

/* =========================================================================
 * System interface (platform-specific, implemented in sys_sdl.c)
 * ========================================================================= */

void    Sys_Init(void);
void    Sys_Quit(void) FAKK_NORETURN;
void    Sys_Error(const char *error, ...) FAKK_NORETURN;
void    Sys_Print(const char *msg);
int     Sys_Milliseconds(void);
char    *Sys_GetClipboardData(void);
void    Sys_SetClipboardData(const char *data);
void    Sys_Mkdir(const char *path);
char    *Sys_Cwd(void);
char    *Sys_DefaultBasePath(void);

/* DLL loading for game modules */
void    *Sys_LoadDll(const char *name);
void    *Sys_GetProcAddress(void *handle, const char *name);
void    Sys_UnloadDll(void *handle);

/* =========================================================================
 * Common engine functions
 * ========================================================================= */

void    Com_Init(int argc, char **argv);
void    Com_Frame(void);
void    Com_Shutdown(void);
void    Com_Printf(const char *fmt, ...);
void    Com_DPrintf(const char *fmt, ...);
void    Com_Error(int code, const char *fmt, ...);
int     Com_EventLoop(void);

/* Error codes */
#define ERR_FATAL       0   /* exit the entire game with a popup window */
#define ERR_DROP        1   /* print to console and disconnect from game */
#define ERR_DISCONNECT  2   /* don't kill server */

/* =========================================================================
 * Memory management -- zone allocator with tagging
 * ========================================================================= */

typedef enum {
    TAG_FREE,
    TAG_GENERAL,
    TAG_RENDERER,
    TAG_SOUND,
    TAG_GAME,
    TAG_CGAME,
    TAG_TIKI,
    TAG_GHOST,
    TAG_SCRIPT,
    TAG_SMALL,
    TAG_STATIC
} memtag_t;

void    *Z_Malloc(int size);
void    *Z_TagMalloc(int size, memtag_t tag);
void    Z_Free(void *ptr);
void    Z_FreeTags(memtag_t tag);

void    *Hunk_Alloc(int size);
void    Hunk_Clear(void);

/* =========================================================================
 * Subsystem init/shutdown entry points
 * ========================================================================= */

/* Client */
void    CL_Init(void);
void    CL_Shutdown(void);
void    CL_Frame(int msec);

/* Server */
void    SV_Init(void);
void    SV_Shutdown(const char *finalmsg);
void    SV_Frame(int msec);
void    SV_SpawnServer(const char *mapname);
void    SV_Map_f(void);
void    SV_KillServer_f(void);
int     SV_GetServerTime(void);
qboolean SV_IsRunning(void);

/* TIKI model system */
void    TIKI_Init(void);
void    TIKI_Shutdown(void);
void    TIKI_FlushAll(void);

/* Collision model */
void    CM_Init(void);
void    CM_Shutdown(void);

/* Renderer / BSP */
void    R_Init(void);
void    R_Shutdown(void);
void    R_BeginFrame(void);
void    R_EndFrame(void);
void    R_BeginRegistration(void);
void    R_EndRegistration(void);
void    R_LoadWorldMap(const char *mapname);
qboolean R_LoadBSP(const char *name);
void    R_FreeWorld(void);
const char *R_GetEntityString(void);
int     R_GetNumInlineModels(void);
qboolean R_WorldLoaded(void);

/* Alias system */
void    Alias_Init(void);
void    Alias_Shutdown(void);

/* Sound system */
void    S_Init(void);
void    S_Shutdown(void);
void    S_Update(void);

/* Morpheus scripting */
void    Script_Init(void);
void    Script_Shutdown(void);

/* Ghost particle system */
void    Ghost_Init(void);
void    Ghost_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* QCOMMON_H */
