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
#include <stdlib.h>
#include <string.h>

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
    Z_Init();
    Hunk_Init();

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
 * Zone memory allocator -- tagged allocation with Z_FreeTags support
 *
 * Every Z_TagMalloc allocation is tracked in a doubly-linked list
 * so Z_FreeTags can free all allocations of a given tag at once.
 * This matches the original Q3/FAKK2 zone allocator semantics.
 *
 * The actual memory layout per allocation:
 *   [zoneHeader_t][user data...]
 * The pointer returned to the caller points past the header.
 * ========================================================================= */

#define ZONE_MAGIC  0x1D4A11C  /* "idalloc" */

typedef struct zoneHeader_s {
    int                     magic;
    int                     size;       /* user-requested size */
    memtag_t                tag;
    struct zoneHeader_s     *prev;
    struct zoneHeader_s     *next;
} zoneHeader_t;

static zoneHeader_t zone_head;      /* sentinel node */
static int          zone_count;     /* number of active allocations */
static int          zone_bytes;     /* total bytes in active allocations */

static void Z_Init(void) {
    zone_head.next = &zone_head;
    zone_head.prev = &zone_head;
    zone_head.magic = ZONE_MAGIC;
    zone_head.tag = TAG_FREE;
    zone_count = 0;
    zone_bytes = 0;
}

void *Z_TagMalloc(int size, memtag_t tag) {
    if (size <= 0) {
        Com_Error(ERR_FATAL, "Z_TagMalloc: bad size %d", size);
    }

    int allocSize = size + (int)sizeof(zoneHeader_t);
    zoneHeader_t *hdr = (zoneHeader_t *)calloc(1, allocSize);
    if (!hdr) {
        Com_Error(ERR_FATAL, "Z_TagMalloc: failed to allocate %d bytes (tag %d)", size, tag);
    }

    hdr->magic = ZONE_MAGIC;
    hdr->size = size;
    hdr->tag = tag;

    /* Insert at head of list */
    hdr->next = zone_head.next;
    hdr->prev = &zone_head;
    zone_head.next->prev = hdr;
    zone_head.next = hdr;

    zone_count++;
    zone_bytes += size;

    return (void *)(hdr + 1);
}

void *Z_Malloc(int size) {
    return Z_TagMalloc(size, TAG_GENERAL);
}

void Z_Free(void *ptr) {
    if (!ptr) return;

    zoneHeader_t *hdr = ((zoneHeader_t *)ptr) - 1;
    if (hdr->magic != ZONE_MAGIC) {
        Com_Error(ERR_FATAL, "Z_Free: bad magic (double free or corruption)");
    }

    /* Unlink from list */
    hdr->prev->next = hdr->next;
    hdr->next->prev = hdr->prev;

    zone_count--;
    zone_bytes -= hdr->size;

    hdr->magic = 0;  /* poison to catch use-after-free */
    free(hdr);
}

void Z_FreeTags(memtag_t tag) {
    zoneHeader_t *node = zone_head.next;
    int freed = 0;

    while (node != &zone_head) {
        zoneHeader_t *next = node->next;
        if (node->tag == tag) {
            /* Unlink */
            node->prev->next = node->next;
            node->next->prev = node->prev;
            zone_count--;
            zone_bytes -= node->size;
            node->magic = 0;
            free(node);
            freed++;
        }
        node = next;
    }

    if (freed > 0) {
        Com_DPrintf("Z_FreeTags(%d): freed %d allocations\n", tag, freed);
    }
}

/* =========================================================================
 * Hunk allocator -- level-scoped bump allocator
 *
 * The hunk is a large contiguous block used for per-level data
 * (BSP, models, textures). Hunk_Clear resets the pointer on
 * level transitions, effectively freeing all level data at once.
 *
 * Original FAKK2 used ~64MB hunk. We use 256MB for 64-bit.
 * ========================================================================= */

#define HUNK_SIZE   (256 * 1024 * 1024)  /* 256 MB */
#define HUNK_MAGIC  0x48554E4B           /* "HUNK" */

static struct {
    byte    *base;
    int     size;
    int     used;
    int     peak;
    int     tempUsed;       /* temp allocations from the top */
    int     initialized;
} hunk;

static void Hunk_Init(void) {
    hunk.base = (byte *)malloc(HUNK_SIZE);
    if (!hunk.base) {
        Com_Error(ERR_FATAL, "Hunk_Init: failed to allocate %d MB", HUNK_SIZE / (1024*1024));
    }
    hunk.size = HUNK_SIZE;
    hunk.used = 0;
    hunk.peak = 0;
    hunk.tempUsed = 0;
    hunk.initialized = 1;
    Com_Printf("Hunk initialized: %d MB\n", HUNK_SIZE / (1024*1024));
}

void *Hunk_Alloc(int size) {
    if (!hunk.initialized) {
        /* Fallback before hunk is set up */
        return Z_TagMalloc(size, TAG_GENERAL);
    }
    if (size <= 0) {
        Com_Error(ERR_FATAL, "Hunk_Alloc: bad size %d", size);
    }

    /* Align to 16 bytes */
    size = (size + 15) & ~15;

    if (hunk.used + size > hunk.size - hunk.tempUsed) {
        Com_Error(ERR_FATAL, "Hunk_Alloc: overflow (%d bytes requested, %d/%d used)",
                  size, hunk.used, hunk.size);
    }

    void *ptr = hunk.base + hunk.used;
    hunk.used += size;
    memset(ptr, 0, size);

    if (hunk.used > hunk.peak) {
        hunk.peak = hunk.used;
    }

    return ptr;
}

void Hunk_Clear(void) {
    if (!hunk.initialized) return;

    Com_DPrintf("Hunk_Clear: peak usage was %d KB of %d KB\n",
                hunk.peak / 1024, hunk.size / 1024);

    hunk.used = 0;
    hunk.tempUsed = 0;
    /* Don't zero the memory -- cleared on alloc */
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
