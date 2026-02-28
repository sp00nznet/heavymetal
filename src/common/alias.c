/*
 * alias.c -- FAKK2 alias system
 *
 * Maps alias names to one or more resource names with optional random
 * selection. Used extensively by the TIKI animation system and sound
 * system for things like:
 *
 *   alias idle_fx   idle_fx1.wav idle_fx2.wav idle_fx3.wav random
 *   alias snd_pain  julie_pain1.wav julie_pain2.wav
 *
 * Each alias can have multiple candidates; FindRandom picks one.
 */

#include "alias.h"
#include "qcommon.h"
#include <string.h>

/* =========================================================================
 * Alias entry
 * ========================================================================= */

#define MAX_ALIAS_ENTRIES       512
#define MAX_ALIAS_CANDIDATES    8
#define MAX_ALIAS_NAME          64

typedef struct {
    char    alias[MAX_ALIAS_NAME];
    char    names[MAX_ALIAS_CANDIDATES][MAX_QPATH];
    char    parameters[MAX_QPATH];
    int     numCandidates;
    int     modelindex;     /* -1 for global */
    qboolean used;
} aliasEntry_t;

static aliasEntry_t alias_entries[MAX_ALIAS_ENTRIES];
static int alias_count;
static qboolean alias_initialized;

/* =========================================================================
 * Init / Shutdown
 * ========================================================================= */

void Alias_Init(void) {
    memset(alias_entries, 0, sizeof(alias_entries));
    alias_count = 0;
    alias_initialized = qtrue;
}

void Alias_Shutdown(void) {
    alias_count = 0;
    alias_initialized = qfalse;
}

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

static aliasEntry_t *Alias_Find(int modelindex, const char *alias) {
    for (int i = 0; i < alias_count; i++) {
        if (alias_entries[i].used &&
            alias_entries[i].modelindex == modelindex &&
            !Q_stricmp(alias_entries[i].alias, alias)) {
            return &alias_entries[i];
        }
    }
    return NULL;
}

static aliasEntry_t *Alias_AllocEntry(void) {
    if (alias_count < MAX_ALIAS_ENTRIES) {
        aliasEntry_t *e = &alias_entries[alias_count++];
        memset(e, 0, sizeof(*e));
        e->used = qtrue;
        return e;
    }

    /* Search for unused slot */
    for (int i = 0; i < MAX_ALIAS_ENTRIES; i++) {
        if (!alias_entries[i].used) {
            memset(&alias_entries[i], 0, sizeof(aliasEntry_t));
            alias_entries[i].used = qtrue;
            return &alias_entries[i];
        }
    }

    Com_Printf("WARNING: Alias_AllocEntry: table full\n");
    return NULL;
}

static qboolean Alias_AddInternal(int modelindex, const char *alias,
                                   const char *name, const char *parameters) {
    if (!alias || !name) return qfalse;

    /* Check for existing entry to add candidates */
    aliasEntry_t *e = Alias_Find(modelindex, alias);
    if (e) {
        if (e->numCandidates < MAX_ALIAS_CANDIDATES) {
            Q_strncpyz(e->names[e->numCandidates], name, MAX_QPATH);
            e->numCandidates++;
        }
        if (parameters && parameters[0]) {
            Q_strncpyz(e->parameters, parameters, MAX_QPATH);
        }
        return qtrue;
    }

    /* Create new entry */
    e = Alias_AllocEntry();
    if (!e) return qfalse;

    e->modelindex = modelindex;
    Q_strncpyz(e->alias, alias, MAX_ALIAS_NAME);
    Q_strncpyz(e->names[0], name, MAX_QPATH);
    e->numCandidates = 1;

    if (parameters && parameters[0]) {
        Q_strncpyz(e->parameters, parameters, MAX_QPATH);
    }

    return qtrue;
}

static const char *Alias_FindRandomInternal(int modelindex, const char *alias) {
    aliasEntry_t *e = Alias_Find(modelindex, alias);
    if (!e) return NULL;

    if (e->numCandidates <= 1) return e->names[0];

    /* Random selection */
    int pick = Sys_Milliseconds() % e->numCandidates;
    return e->names[pick];
}

static void Alias_ClearInternal(int modelindex) {
    for (int i = 0; i < alias_count; i++) {
        if (alias_entries[i].used && alias_entries[i].modelindex == modelindex) {
            alias_entries[i].used = qfalse;
        }
    }
}

static void Alias_DumpInternal(int modelindex) {
    Com_Printf("--- Alias dump (model %d) ---\n", modelindex);
    for (int i = 0; i < alias_count; i++) {
        if (alias_entries[i].used && alias_entries[i].modelindex == modelindex) {
            Com_Printf("  %s -> %s (%d candidates)\n",
                       alias_entries[i].alias,
                       alias_entries[i].names[0],
                       alias_entries[i].numCandidates);
        }
    }
}

/* =========================================================================
 * Global alias API
 * ========================================================================= */

qboolean Alias_GlobalAdd(const char *alias, const char *name, const char *parameters) {
    return Alias_AddInternal(-1, alias, name, parameters);
}

const char *Alias_GlobalFindRandom(const char *alias) {
    return Alias_FindRandomInternal(-1, alias);
}

void Alias_GlobalDump(void) {
    Alias_DumpInternal(-1);
}

void Alias_GlobalClear(void) {
    Alias_ClearInternal(-1);
}

/* =========================================================================
 * Per-model alias API
 * ========================================================================= */

qboolean Alias_ModelAdd(int modelindex, const char *alias, const char *name, const char *parameters) {
    return Alias_AddInternal(modelindex, alias, name, parameters);
}

const char *Alias_ModelFindRandom(int modelindex, const char *alias) {
    const char *result = Alias_FindRandomInternal(modelindex, alias);
    /* Fall back to global */
    if (!result) result = Alias_FindRandomInternal(-1, alias);
    return result;
}

void Alias_ModelDump(int modelindex) {
    Alias_DumpInternal(modelindex);
}

void Alias_ModelClear(int modelindex) {
    Alias_ClearInternal(modelindex);
}

const char *Alias_ModelFindDialog(int modelindex, const char *alias,
                                   int random, int entity_number) {
    (void)random; (void)entity_number;
    return Alias_ModelFindRandom(modelindex, alias);
}

void *Alias_ModelGetList(int modelindex) {
    /* Returns the internal alias table pointer for the given model.
     * The game DLL uses this for save/load serialization of alias state.
     * We return the beginning of aliases for this model index. */
    for (int i = 0; i < alias_count; i++) {
        if (alias_entries[i].used && alias_entries[i].modelindex == modelindex) {
            return &alias_entries[i];
        }
    }
    return NULL;
}

void Alias_ModelUpdateDialog(int modelindex, const char *alias,
                              int number_of_times_played,
                              byte been_played_this_loop,
                              int last_time_played) {
    (void)modelindex; (void)alias;
    (void)number_of_times_played; (void)been_played_this_loop;
    (void)last_time_played;
}

void Alias_ModelAddActorDialog(int modelindex, const char *alias,
                                int actor_number, int number_of_times_played,
                                byte been_played_this_loop,
                                int last_time_played) {
    (void)modelindex; (void)alias; (void)actor_number;
    (void)number_of_times_played; (void)been_played_this_loop;
    (void)last_time_played;
}
