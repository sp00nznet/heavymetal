/*
 * script_main.c -- Morpheus scripting engine core
 *
 * Manages command registry, thread pool, and the main execution loop.
 * The Morpheus engine processes script threads each frame, executing
 * commands until a wait/delay or end-of-script is reached.
 *
 * FAKK2 scripts are plain text with one command per line:
 *   commandname arg1 arg2 arg3 ...
 *
 * Special flow commands:
 *   wait <seconds>     -- pause thread for N seconds
 *   waitframe          -- pause until next frame
 *   end                -- terminate thread
 *   goto <label>       -- jump to label
 *   thread <label>     -- spawn sub-thread at label
 */

#include "script.h"
#include "../common/qcommon.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/* =========================================================================
 * State
 * ========================================================================= */

static qboolean         script_initialized = qfalse;
static float            script_currentTime = 0.0f;

/* Command registry (hash table) */
#define CMD_HASH_SIZE   256
static scriptCmd_t      script_cmds[SCRIPT_MAX_COMMANDS];
static int              script_numCmds;
static int              script_cmdHash[CMD_HASH_SIZE];

/* Thread pool */
static scriptThread_t   script_threads[SCRIPT_MAX_THREADS];
static int              script_nextThreadId = 1;

/* Global variables */
static scriptVar_t      script_globalVars[SCRIPT_MAX_GLOBAL_VARS];
static int              script_numGlobalVars;

/* =========================================================================
 * Hash function
 * ========================================================================= */

static unsigned int Script_HashName(const char *name) {
    unsigned int hash = 0;
    for (const char *p = name; *p; p++) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        hash = hash * 31 + (unsigned char)c;
    }
    return hash % CMD_HASH_SIZE;
}

/* =========================================================================
 * Command registration
 * ========================================================================= */

void Script_RegisterCommand(const char *name, scriptCmdFunc_t func,
                            int minArgs, const char *usage) {
    if (!name || !func) return;
    if (script_numCmds >= SCRIPT_MAX_COMMANDS) {
        Com_Printf("Script_RegisterCommand: command table full\n");
        return;
    }

    scriptCmd_t *cmd = &script_cmds[script_numCmds];
    Q_strncpyz(cmd->name, name, sizeof(cmd->name));
    cmd->func = func;
    cmd->minArgs = minArgs;
    cmd->usage = usage;

    script_numCmds++;
}

static scriptCmd_t *Script_FindCommand(const char *name) {
    unsigned int hash = Script_HashName(name);
    (void)hash; /* linear scan for correctness; hash can optimize later */

    for (int i = 0; i < script_numCmds; i++) {
        if (!Q_stricmp(script_cmds[i].name, name)) {
            return &script_cmds[i];
        }
    }
    return NULL;
}

/* =========================================================================
 * Tokenizer
 *
 * Parses a single line of script text into command name + arguments.
 * Handles quoted strings and // comments.
 * Returns pointer past the parsed line (next line start).
 * ========================================================================= */

static const char *Script_ParseLine(const char *text, scriptArgs_t *args) {
    args->argc = 0;

    if (!text || !*text) return NULL;

    /* Skip leading whitespace and blank lines */
    while (*text && (*text == ' ' || *text == '\t' || *text == '\r')) text++;
    if (*text == '\n') return text + 1;
    if (!*text) return NULL;

    /* Skip comment lines */
    if (text[0] == '/' && text[1] == '/') {
        while (*text && *text != '\n') text++;
        if (*text == '\n') text++;
        return text;
    }

    /* Parse tokens */
    while (*text && *text != '\n' && args->argc < SCRIPT_MAX_ARGS) {
        /* Skip whitespace between tokens */
        while (*text == ' ' || *text == '\t') text++;
        if (!*text || *text == '\n') break;

        /* Check for end-of-line comment */
        if (text[0] == '/' && text[1] == '/') {
            while (*text && *text != '\n') text++;
            break;
        }

        char *dest = args->argv[args->argc];
        int maxLen = SCRIPT_MAX_ARGLEN - 1;
        int len = 0;

        if (*text == '"') {
            /* Quoted string */
            text++;
            while (*text && *text != '"' && *text != '\n' && len < maxLen) {
                dest[len++] = *text++;
            }
            if (*text == '"') text++;
        } else {
            /* Unquoted token */
            while (*text && *text != ' ' && *text != '\t' &&
                   *text != '\n' && len < maxLen) {
                dest[len++] = *text++;
            }
        }

        dest[len] = '\0';
        if (len > 0) args->argc++;
    }

    /* Advance past newline */
    if (*text == '\n') text++;

    return text;
}

/* =========================================================================
 * Thread management
 * ========================================================================= */

static scriptThread_t *Script_AllocThread(void) {
    for (int i = 0; i < SCRIPT_MAX_THREADS; i++) {
        if (script_threads[i].state == STS_FREE ||
            script_threads[i].state == STS_FINISHED) {
            scriptThread_t *t = &script_threads[i];
            /* Free any owned memory */
            if (t->ownsMem && t->scriptText) {
                Z_Free((void *)t->scriptText);
            }
            memset(t, 0, sizeof(*t));
            t->id = script_nextThreadId++;
            t->entityNum = -1;
            return t;
        }
    }
    return NULL;
}

int Script_CreateThread(const char *scriptText, int entityNum,
                        const char *label) {
    if (!script_initialized || !scriptText) return -1;

    scriptThread_t *t = Script_AllocThread();
    if (!t) {
        Com_Printf("Script_CreateThread: no free thread slots\n");
        return -1;
    }

    t->state = STS_RUNNING;
    t->scriptText = scriptText;
    t->currentPos = scriptText;
    t->ownsMem = qfalse;
    t->entityNum = entityNum;
    t->startTime = script_currentTime;
    if (label) {
        Q_strncpyz(t->label, label, sizeof(t->label));
    }

    return t->id;
}

int Script_CreateThreadFromFile(const char *filename, int entityNum) {
    if (!script_initialized || !filename) return -1;

    void *buffer;
    long len = FS_ReadFile(filename, &buffer);
    if (len <= 0 || !buffer) {
        Com_Printf("Script_CreateThreadFromFile: can't load '%s'\n", filename);
        return -1;
    }

    /* Copy to owned buffer so we can free the FS buffer */
    char *copy = (char *)Z_Malloc((int)len + 1);
    memcpy(copy, buffer, len);
    copy[len] = '\0';
    FS_FreeFile(buffer);

    scriptThread_t *t = Script_AllocThread();
    if (!t) {
        Z_Free(copy);
        Com_Printf("Script_CreateThreadFromFile: no free thread slots\n");
        return -1;
    }

    t->state = STS_RUNNING;
    t->scriptText = copy;
    t->currentPos = copy;
    t->ownsMem = qtrue;
    t->entityNum = entityNum;
    t->startTime = script_currentTime;
    Q_strncpyz(t->label, filename, sizeof(t->label));

    return t->id;
}

void Script_KillThread(int threadId) {
    for (int i = 0; i < SCRIPT_MAX_THREADS; i++) {
        if (script_threads[i].id == threadId &&
            script_threads[i].state != STS_FREE) {
            script_threads[i].state = STS_FINISHED;
            return;
        }
    }
}

void Script_KillEntityThreads(int entityNum) {
    for (int i = 0; i < SCRIPT_MAX_THREADS; i++) {
        if (script_threads[i].entityNum == entityNum &&
            script_threads[i].state != STS_FREE) {
            script_threads[i].state = STS_FINISHED;
        }
    }
}

scriptThread_t *Script_GetThread(int threadId) {
    for (int i = 0; i < SCRIPT_MAX_THREADS; i++) {
        if (script_threads[i].id == threadId &&
            script_threads[i].state != STS_FREE) {
            return &script_threads[i];
        }
    }
    return NULL;
}

int Script_NumActiveThreads(void) {
    int count = 0;
    for (int i = 0; i < SCRIPT_MAX_THREADS; i++) {
        if (script_threads[i].state == STS_RUNNING ||
            script_threads[i].state == STS_WAITING ||
            script_threads[i].state == STS_SUSPENDED) {
            count++;
        }
    }
    return count;
}

/* =========================================================================
 * Variable management
 * ========================================================================= */

void Script_SetGlobalVar(const char *name, const char *value) {
    if (!name) return;

    /* Find existing */
    for (int i = 0; i < script_numGlobalVars; i++) {
        if (!Q_stricmp(script_globalVars[i].name, name)) {
            Q_strncpyz(script_globalVars[i].stringVal, value ? value : "",
                       sizeof(script_globalVars[i].stringVal));
            script_globalVars[i].type = SVT_STRING;
            return;
        }
    }

    /* Add new */
    if (script_numGlobalVars >= SCRIPT_MAX_GLOBAL_VARS) {
        Com_Printf("Script_SetGlobalVar: variable table full\n");
        return;
    }

    scriptVar_t *v = &script_globalVars[script_numGlobalVars++];
    Q_strncpyz(v->name, name, sizeof(v->name));
    Q_strncpyz(v->stringVal, value ? value : "", sizeof(v->stringVal));
    v->type = SVT_STRING;
}

const char *Script_GetGlobalVar(const char *name) {
    if (!name) return "";
    for (int i = 0; i < script_numGlobalVars; i++) {
        if (!Q_stricmp(script_globalVars[i].name, name)) {
            return script_globalVars[i].stringVal;
        }
    }
    return "";
}

void Script_SetLocalVar(scriptThread_t *thread, const char *name,
                        const char *value) {
    if (!thread || !name) return;

    for (int i = 0; i < thread->numLocalVars; i++) {
        if (!Q_stricmp(thread->localVars[i].name, name)) {
            Q_strncpyz(thread->localVars[i].stringVal, value ? value : "",
                       sizeof(thread->localVars[i].stringVal));
            thread->localVars[i].type = SVT_STRING;
            return;
        }
    }

    if (thread->numLocalVars >= SCRIPT_MAX_VARS) return;

    scriptVar_t *v = &thread->localVars[thread->numLocalVars++];
    Q_strncpyz(v->name, name, sizeof(v->name));
    Q_strncpyz(v->stringVal, value ? value : "", sizeof(v->stringVal));
    v->type = SVT_STRING;
}

const char *Script_GetLocalVar(scriptThread_t *thread, const char *name) {
    if (!thread || !name) return "";
    for (int i = 0; i < thread->numLocalVars; i++) {
        if (!Q_stricmp(thread->localVars[i].name, name)) {
            return thread->localVars[i].stringVal;
        }
    }
    /* Fall through to globals */
    return Script_GetGlobalVar(name);
}

/* =========================================================================
 * Thread execution -- process commands until wait or end
 * ========================================================================= */

#define SCRIPT_MAX_CMDS_PER_TICK 1000  /* prevent infinite loops */

static void Script_RunThread(scriptThread_t *thread) {
    if (thread->state != STS_RUNNING) return;

    scriptArgs_t args;
    int cmdCount = 0;

    while (thread->currentPos && *thread->currentPos && cmdCount < SCRIPT_MAX_CMDS_PER_TICK) {
        const char *nextPos = Script_ParseLine(thread->currentPos, &args);

        if (args.argc == 0) {
            thread->currentPos = nextPos;
            if (!nextPos) {
                thread->state = STS_FINISHED;
                return;
            }
            continue;
        }

        thread->currentPos = nextPos;
        cmdCount++;

        const char *cmdName = args.argv[0];

        /* Built-in flow commands */
        if (!Q_stricmp(cmdName, "end")) {
            thread->state = STS_FINISHED;
            return;
        }

        if (!Q_stricmp(cmdName, "wait")) {
            float delay = 0.0f;
            if (args.argc > 1) delay = (float)atof(args.argv[1]);
            thread->waitTime = script_currentTime + delay;
            thread->state = STS_WAITING;
            return;
        }

        if (!Q_stricmp(cmdName, "waitframe")) {
            /* Resume next frame -- set tiny wait */
            thread->waitTime = script_currentTime;
            thread->state = STS_WAITING;
            return;
        }

        if (!Q_stricmp(cmdName, "goto") || !Q_stricmp(cmdName, "label")) {
            /* Labels are just markers; goto searches for them */
            if (!Q_stricmp(cmdName, "goto") && args.argc > 1) {
                /* Search for label from start of script */
                const char *search = thread->scriptText;
                scriptArgs_t searchArgs;
                while (search && *search) {
                    const char *lineStart = search;
                    search = Script_ParseLine(search, &searchArgs);
                    if (searchArgs.argc >= 2 &&
                        !Q_stricmp(searchArgs.argv[0], "label") &&
                        !Q_stricmp(searchArgs.argv[1], args.argv[1])) {
                        thread->currentPos = search;
                        break;
                    }
                    (void)lineStart;
                }
            }
            continue;
        }

        if (!Q_stricmp(cmdName, "thread") && args.argc > 1) {
            /* Search for label and spawn a new thread there */
            const char *search = thread->scriptText;
            scriptArgs_t searchArgs;
            while (search && *search) {
                search = Script_ParseLine(search, &searchArgs);
                if (searchArgs.argc >= 2 &&
                    !Q_stricmp(searchArgs.argv[0], "label") &&
                    !Q_stricmp(searchArgs.argv[1], args.argv[1])) {
                    scriptThread_t *newThread = Script_AllocThread();
                    if (newThread) {
                        newThread->state = STS_RUNNING;
                        newThread->scriptText = thread->scriptText;
                        newThread->currentPos = search;
                        newThread->ownsMem = qfalse;
                        newThread->entityNum = thread->entityNum;
                        newThread->startTime = script_currentTime;
                        Q_strncpyz(newThread->label, args.argv[1],
                                   sizeof(newThread->label));
                    }
                    break;
                }
            }
            continue;
        }

        if (!Q_stricmp(cmdName, "set") || !Q_stricmp(cmdName, "local")) {
            if (args.argc >= 3) {
                if (!Q_stricmp(cmdName, "local")) {
                    Script_SetLocalVar(thread, args.argv[1], args.argv[2]);
                } else {
                    Script_SetGlobalVar(args.argv[1], args.argv[2]);
                }
            }
            continue;
        }

        /* Look up registered command */
        scriptCmd_t *cmd = Script_FindCommand(cmdName);
        if (cmd) {
            /* Shift args: argv[0] is command name, actual args start at [1] */
            if (!cmd->func(thread, &args)) {
                thread->state = STS_FINISHED;
                return;
            }
        } else {
            /* Try forwarding to console */
            char consoleLine[1024];
            consoleLine[0] = '\0';
            for (int i = 0; i < args.argc; i++) {
                if (i > 0) Q_strcat(consoleLine, sizeof(consoleLine), " ");
                Q_strcat(consoleLine, sizeof(consoleLine), args.argv[i]);
            }
            Cbuf_AddText(consoleLine);
            Cbuf_AddText("\n");
        }

        if (!nextPos) {
            thread->state = STS_FINISHED;
            return;
        }
    }

    if (cmdCount >= SCRIPT_MAX_CMDS_PER_TICK) {
        Com_Printf("Script warning: thread '%s' hit command limit (%d), "
                   "possible infinite loop\n", thread->label, SCRIPT_MAX_CMDS_PER_TICK);
        thread->state = STS_FINISHED;
    }
}

/* =========================================================================
 * Per-frame update -- run all active threads
 * ========================================================================= */

void Script_RunThreads(float currentTime) {
    if (!script_initialized) return;
    script_currentTime = currentTime;

    for (int i = 0; i < SCRIPT_MAX_THREADS; i++) {
        scriptThread_t *t = &script_threads[i];

        switch (t->state) {
        case STS_WAITING:
            if (currentTime >= t->waitTime) {
                t->state = STS_RUNNING;
                Script_RunThread(t);
            }
            break;
        case STS_RUNNING:
            Script_RunThread(t);
            break;
        case STS_FINISHED:
            /* Reclaim */
            if (t->ownsMem && t->scriptText) {
                Z_Free((void *)t->scriptText);
            }
            memset(t, 0, sizeof(*t));
            break;
        default:
            break;
        }
    }
}

/* =========================================================================
 * Immediate execution (no thread overhead)
 * ========================================================================= */

void Script_Execute(const char *scriptText) {
    if (!script_initialized || !scriptText) return;

    /* Create a temporary thread and run it immediately */
    scriptThread_t temp;
    memset(&temp, 0, sizeof(temp));
    temp.state = STS_RUNNING;
    temp.scriptText = scriptText;
    temp.currentPos = scriptText;
    temp.entityNum = -1;
    Q_strncpyz(temp.label, "<immediate>", sizeof(temp.label));

    Script_RunThread(&temp);
}

void Script_ExecuteFile(const char *filename) {
    if (!script_initialized || !filename) return;

    void *buffer;
    long len = FS_ReadFile(filename, &buffer);
    if (len <= 0 || !buffer) {
        Com_Printf("Script_ExecuteFile: can't load '%s'\n", filename);
        return;
    }

    /* Need null-terminated copy */
    char *copy = (char *)Z_Malloc((int)len + 1);
    memcpy(copy, buffer, len);
    copy[len] = '\0';
    FS_FreeFile(buffer);

    Script_Execute(copy);
    Z_Free(copy);
}

/* =========================================================================
 * Time query
 * ========================================================================= */

float Script_GetCurrentTime(void) {
    return script_currentTime;
}

/* =========================================================================
 * Console commands
 * ========================================================================= */

static void Script_Exec_f(void) {
    if (Cmd_Argc() < 2) {
        Com_Printf("Usage: exec_script <filename>\n");
        return;
    }
    Script_ExecuteFile(Cmd_Argv(1));
}

static void Script_ThreadList_f(void) {
    Com_Printf("Active script threads:\n");
    int count = 0;
    for (int i = 0; i < SCRIPT_MAX_THREADS; i++) {
        scriptThread_t *t = &script_threads[i];
        if (t->state == STS_FREE) continue;

        const char *stateStr = "???";
        switch (t->state) {
        case STS_RUNNING:   stateStr = "running";   break;
        case STS_WAITING:   stateStr = "waiting";   break;
        case STS_SUSPENDED: stateStr = "suspended"; break;
        case STS_FINISHED:  stateStr = "finished";  break;
        default: break;
        }

        Com_Printf("  [%d] %-12s entity=%d label='%s'\n",
                   t->id, stateStr, t->entityNum, t->label);
        count++;
    }
    Com_Printf("%d thread(s)\n", count);
}

static void Script_VarList_f(void) {
    Com_Printf("Global script variables:\n");
    for (int i = 0; i < script_numGlobalVars; i++) {
        Com_Printf("  %s = \"%s\"\n",
                   script_globalVars[i].name,
                   script_globalVars[i].stringVal);
    }
    Com_Printf("%d variable(s)\n", script_numGlobalVars);
}

/* =========================================================================
 * Initialization and shutdown
 * ========================================================================= */

void Script_Init(void) {
    Com_Printf("--- Script_Init ---\n");

    memset(script_cmds, 0, sizeof(script_cmds));
    memset(script_threads, 0, sizeof(script_threads));
    memset(script_globalVars, 0, sizeof(script_globalVars));
    memset(script_cmdHash, 0, sizeof(script_cmdHash));

    script_numCmds = 0;
    script_nextThreadId = 1;
    script_numGlobalVars = 0;
    script_currentTime = 0.0f;
    script_initialized = qtrue;

    Cmd_AddCommand("exec_script", Script_Exec_f);
    Cmd_AddCommand("scriptthreadlist", Script_ThreadList_f);
    Cmd_AddCommand("scriptvarlist", Script_VarList_f);

    /* Register built-in commands (script_vm.c) */
    extern void Script_RegisterBuiltins(void);
    Script_RegisterBuiltins();

    Com_Printf("Morpheus scripting engine initialized (%d command slots, %d built-in)\n",
               SCRIPT_MAX_COMMANDS, script_numCmds);
}

void Script_Shutdown(void) {
    if (!script_initialized) return;

    /* Kill all threads */
    for (int i = 0; i < SCRIPT_MAX_THREADS; i++) {
        scriptThread_t *t = &script_threads[i];
        if (t->ownsMem && t->scriptText) {
            Z_Free((void *)t->scriptText);
        }
    }
    memset(script_threads, 0, sizeof(script_threads));

    Cmd_RemoveCommand("exec_script");
    Cmd_RemoveCommand("scriptthreadlist");
    Cmd_RemoveCommand("scriptvarlist");

    script_initialized = qfalse;
    Com_Printf("Morpheus scripting engine shutdown\n");
}
