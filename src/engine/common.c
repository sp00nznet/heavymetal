/*
 * common.c -- Common engine functions for FAKK2
 *
 * This is the central nervous system of the engine. It initializes
 * all subsystems, runs the main frame, and handles errors.
 *
 * Subsystem initialization order (from original binary analysis):
 *   1. Memory (Z_Init, Hunk_Init)
 *   2. Command buffer (Cbuf_Init)
 *   3. Command system (Cmd_Init)
 *   4. Cvar system (Cvar_Init)
 *   5. Filesystem (FS_Init) -- mounts fakk/ and PK3 archives
 *   6. Network (NET_Init)
 *   7. TIKI model system (TIKI_Init)
 *   8. Morpheus scripting (Script_Init)
 *   9. Ghost particles (Ghost_Init)
 *  10. Server (SV_Init)
 *  11. Client (CL_Init) -- creates window, GL context, renderer
 *  12. Game DLL loading (gamex86.dll via GetGameAPI)
 *  13. Client game DLL (cgamex86.dll via GetCGameAPI)
 */

#include "qcommon.h"
#include <stdio.h>
#include <stdarg.h>

/* =========================================================================
 * Cvars registered by the engine
 * ========================================================================= */

static cvar_t *com_dedicated;
static cvar_t *com_developer;
static cvar_t *com_speeds;
static cvar_t *com_maxfps;
static cvar_t *com_fixedtime;
static cvar_t *timescale;

/* =========================================================================
 * Printing
 * ========================================================================= */

void Com_Printf(const char *fmt, ...) {
    va_list args;
    char    msg[4096];

    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    Sys_Print(msg);

    /* Feed console output buffer */
    extern void Con_Print(const char *text);
    Con_Print(msg);
}

void Com_DPrintf(const char *fmt, ...) {
    va_list args;
    char    msg[4096];

    if (com_developer && !com_developer->integer) return;

    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    Com_Printf("%s", msg);
}

void Com_Error(int code, const char *fmt, ...) {
    va_list args;
    char    msg[4096];

    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    Com_Printf("***** ERROR *****\n%s\n", msg);

    if (code == ERR_FATAL) {
        Com_Shutdown();
        Sys_Error("%s", msg);
    }

    /* TODO: ERR_DROP -- disconnect and return to menu */
    /* TODO: ERR_DISCONNECT -- just disconnect */
}

/* =========================================================================
 * Initialization
 * ========================================================================= */

void Com_Init(int argc, char **argv) {
    Com_Printf("--- Com_Init ---\n");

    /* Platform init (SDL2, timers, etc.) */
    Sys_Init();

    /* Memory manager */
    /* TODO: Z_Init(), Hunk_Init() */

    /* Core systems */
    Cbuf_Init();
    Cmd_Init();
    Cvar_Init();

    /* Register engine cvars */
    com_dedicated = Cvar_Get("dedicated", "0", CVAR_LATCH);
    com_developer = Cvar_Get("developer", "0", CVAR_TEMP);
    com_speeds = Cvar_Get("com_speeds", "0", 0);
    com_maxfps = Cvar_Get("com_maxfps", "85", CVAR_ARCHIVE);
    com_fixedtime = Cvar_Get("fixedtime", "0", CVAR_CHEAT);
    timescale = Cvar_Get("timescale", "1", CVAR_CHEAT);

    /* Filesystem -- mount fakk/ directory and PK3 archives */
    FS_Init();

    /* Execute default configs */
    Cbuf_AddText("exec default.cfg\n");
    Cbuf_AddText("exec autoexec.cfg\n");
    Cbuf_Execute();

    /* Network */
    NET_Init();

    /* Collision model */
    CM_Init();

    /* Alias system */
    Alias_Init();

    /* Sound system */
    S_Init();

    /* UberTools subsystems */
    TIKI_Init();        /* TIKI model system */
    Script_Init();      /* Morpheus scripting engine */
    Ghost_Init();       /* Ghost particle system */

    /* Server and client */
    SV_Init();
    CL_Init();

    Com_Printf("--- FAKK2 engine initialized ---\n");
    Com_Printf("\n");
}

/* =========================================================================
 * Main frame
 * ========================================================================= */

void Com_Frame(void) {
    int msec;
    static int lastTime = 0;

    /* Frame rate limiting */
    if (com_maxfps && com_maxfps->integer > 0) {
        int minMsec = 1000 / com_maxfps->integer;
        int elapsed;
        do {
            elapsed = Sys_Milliseconds() - lastTime;
        } while (elapsed < minMsec);
    }

    /* Get frame time */
    int timeNow = Sys_Milliseconds();
    msec = timeNow - lastTime;
    lastTime = timeNow;

    if (msec < 1) msec = 1;
    if (msec > 500) msec = 500;  /* prevent spiral of death */

    /* Fixed time for debugging */
    if (com_fixedtime && com_fixedtime->integer) {
        msec = com_fixedtime->integer;
    }

    /* Timescale */
    if (timescale && timescale->value != 1.0f) {
        msec = (int)(msec * timescale->value);
        if (msec < 1) msec = 1;
    }

    /* Execute any pending commands */
    Cbuf_Execute();

    /* Run server frame */
    SV_Frame(msec);

    /* Run client frame */
    CL_Frame(msec);
}

/* =========================================================================
 * Shutdown
 * ========================================================================= */

void Com_Shutdown(void) {
    Com_Printf("--- Com_Shutdown ---\n");

    CL_Shutdown();
    SV_Shutdown("Server quit");

    Ghost_Shutdown();
    Script_Shutdown();
    TIKI_Shutdown();

    S_Shutdown();
    Alias_Shutdown();
    CM_Shutdown();

    NET_Shutdown();
    FS_Shutdown();
}

/* =========================================================================
 * Stub implementations (to be filled in during recomp)
 * ========================================================================= */

/* Memory -- simple passthrough for now */
void *Z_Malloc(int size) {
    void *ptr = calloc(1, size);
    if (!ptr) Com_Error(ERR_FATAL, "Z_Malloc: failed to allocate %d bytes", size);
    return ptr;
}

void *Z_TagMalloc(int size, memtag_t tag) {
    (void)tag;
    return Z_Malloc(size);
}

void Z_Free(void *ptr) {
    if (ptr) free(ptr);
}

void Z_FreeTags(memtag_t tag) {
    (void)tag;
    /* TODO: Implement tagged memory tracking */
}

void *Hunk_Alloc(int size) {
    return Z_Malloc(size);
}

void Hunk_Clear(void) {
    /* TODO: Implement hunk allocator */
}

/* Event loop */
int Com_EventLoop(void) {
    /* In the original engine, this pumps the system event queue
     * (keyboard, mouse, network packets) and dispatches them.
     * In the recomp, SDL2 event processing is done in Win_ProcessEvents()
     * which is called from CL_Frame(). This function remains for
     * compatibility with code that calls it directly. */
    return 0;
}
