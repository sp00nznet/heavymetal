/*
 * script.h -- Morpheus scripting engine
 *
 * Morpheus is Ritual's scripting language, evolved from SiN's system.
 * It provides ~700 commands for controlling game entities, cameras,
 * AI behaviors, and cinematic sequences.
 *
 * Key capabilities:
 *   - Entity manipulation (move, rotate, animate, damage)
 *   - AI behavior control (path, state, target)
 *   - Camera scripting (spline paths, FOV, tracking)
 *   - Cinematic sequences (Babble lip-sync integration)
 *   - Trigger/event responses
 *   - Variable/conditional logic
 *
 * Scripts are embedded in:
 *   - TIKI files (init blocks, frame events)
 *   - BSP entities (trigger scripts)
 *   - Standalone script files
 *   - Console commands
 *
 * Architecture:
 *   - Script threads execute concurrently with independent state
 *   - Each thread has an entity context (the "self" entity)
 *   - Commands are registered in a hash table for fast dispatch
 *   - Wait/delay pauses a thread until time elapses
 *   - TIKI init commands and frame commands feed into this system
 */

#ifndef SCRIPT_H
#define SCRIPT_H

#include "../common/fakk_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Constants
 * ========================================================================= */

#define SCRIPT_MAX_THREADS      64
#define SCRIPT_MAX_COMMANDS     1024     /* registered command slots */
#define SCRIPT_MAX_STACK        32       /* goto/gosub stack depth */
#define SCRIPT_MAX_ARGS         16       /* max args per command */
#define SCRIPT_MAX_ARGLEN       256      /* max length per argument */
#define SCRIPT_MAX_VARS         256      /* max variables per thread */
#define SCRIPT_MAX_GLOBAL_VARS  512      /* global variable pool */
#define SCRIPT_MAX_LABEL_LEN    64

/* =========================================================================
 * Script variable
 * ========================================================================= */

typedef enum {
    SVT_NONE,
    SVT_INTEGER,
    SVT_FLOAT,
    SVT_STRING,
    SVT_VECTOR
} scriptVarType_t;

typedef struct {
    char                name[SCRIPT_MAX_LABEL_LEN];
    scriptVarType_t     type;
    union {
        int             intVal;
        float           floatVal;
        char            stringVal[SCRIPT_MAX_ARGLEN];
        vec3_t          vecVal;
    };
} scriptVar_t;

/* =========================================================================
 * Script thread
 *
 * Each thread executes a script independently. Threads can be:
 *   - Active (executing commands each frame)
 *   - Waiting (paused until a time threshold)
 *   - Suspended (waiting for an event/signal)
 * ========================================================================= */

typedef enum {
    STS_FREE,           /* slot available */
    STS_RUNNING,        /* executing commands */
    STS_WAITING,        /* waiting for time to elapse */
    STS_SUSPENDED,      /* waiting for event */
    STS_FINISHED        /* done, can be reclaimed */
} scriptThreadState_t;

typedef struct scriptThread_s {
    scriptThreadState_t state;
    int                 id;             /* unique thread ID */

    /* Script source */
    const char          *scriptText;    /* pointer into script source */
    const char          *currentPos;    /* current parse position */
    qboolean            ownsMem;        /* true = we allocated scriptText */

    /* Timing */
    float               waitTime;       /* time to resume (game time) */
    float               startTime;      /* when this thread started */

    /* Entity context */
    int                 entityNum;      /* "self" entity (-1 = none) */

    /* Call stack for gosub/goto */
    const char          *callStack[SCRIPT_MAX_STACK];
    int                 callDepth;

    /* Local variables */
    scriptVar_t         localVars[SCRIPT_MAX_VARS];
    int                 numLocalVars;

    /* Label for debugging */
    char                label[SCRIPT_MAX_LABEL_LEN];
} scriptThread_t;

/* =========================================================================
 * Script command callback
 *
 * Commands receive the executing thread and parsed arguments.
 * Return qtrue to continue execution, qfalse to stop the thread.
 * ========================================================================= */

typedef struct scriptArgs_s {
    int     argc;
    char    argv[SCRIPT_MAX_ARGS][SCRIPT_MAX_ARGLEN];
} scriptArgs_t;

typedef qboolean (*scriptCmdFunc_t)(scriptThread_t *thread, scriptArgs_t *args);

typedef struct {
    char                name[SCRIPT_MAX_LABEL_LEN];
    scriptCmdFunc_t     func;
    int                 minArgs;        /* min required args (excluding cmd name) */
    const char          *usage;         /* brief usage hint */
} scriptCmd_t;

/* =========================================================================
 * Public API
 * ========================================================================= */

/* System lifecycle */
void            Script_Init(void);
void            Script_Shutdown(void);
void            Script_RunThreads(float currentTime);

/* Command registration */
void            Script_RegisterCommand(const char *name, scriptCmdFunc_t func,
                                       int minArgs, const char *usage);

/* Thread creation */
int             Script_CreateThread(const char *scriptText, int entityNum,
                                    const char *label);
int             Script_CreateThreadFromFile(const char *filename, int entityNum);
void            Script_KillThread(int threadId);
void            Script_KillEntityThreads(int entityNum);

/* Script execution (immediate, no thread) */
void            Script_Execute(const char *scriptText);
void            Script_ExecuteFile(const char *filename);

/* Variable access */
void            Script_SetGlobalVar(const char *name, const char *value);
const char      *Script_GetGlobalVar(const char *name);
void            Script_SetLocalVar(scriptThread_t *thread, const char *name,
                                   const char *value);
const char      *Script_GetLocalVar(scriptThread_t *thread, const char *name);

/* Thread queries */
scriptThread_t  *Script_GetThread(int threadId);
int             Script_NumActiveThreads(void);

/* Utility */
float           Script_GetCurrentTime(void);

#ifdef __cplusplus
}
#endif

#endif /* SCRIPT_H */
