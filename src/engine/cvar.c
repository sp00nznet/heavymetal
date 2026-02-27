/*
 * cvar.c -- Console variable system
 *
 * CVars provide runtime configuration for the engine and game.
 * FAKK2 registers ~85 cvars (vs ~20 in base Q2).
 */

#include "qcommon.h"
#include <string.h>
#include <stdlib.h>

#define CVAR_HASH_SIZE  256

static cvar_t   *cvar_vars = NULL;
static cvar_t   *cvar_hash[CVAR_HASH_SIZE];

static int Cvar_Hash(const char *name) {
    int hash = 0;
    for (const char *s = name; *s; s++) {
        hash = hash * 33 + (unsigned char)*s;
    }
    return hash & (CVAR_HASH_SIZE - 1);
}

static cvar_t *Cvar_Find(const char *name) {
    int hash = Cvar_Hash(name);
    for (cvar_t *var = cvar_hash[hash]; var; var = var->hashNext) {
        if (!Q_stricmp(name, var->name)) return var;
    }
    return NULL;
}

void Cvar_Init(void) {
    memset(cvar_hash, 0, sizeof(cvar_hash));
    Com_Printf("Cvar system initialized\n");
}

cvar_t *Cvar_Get(const char *name, const char *value, int flags) {
    cvar_t *var = Cvar_Find(name);
    if (var) {
        var->flags |= flags;
        return var;
    }

    var = (cvar_t *)Z_Malloc(sizeof(cvar_t));
    var->name = (char *)Z_Malloc((int)strlen(name) + 1);
    strcpy(var->name, name);

    var->string = (char *)Z_Malloc((int)strlen(value) + 1);
    strcpy(var->string, value);

    var->resetString = (char *)Z_Malloc((int)strlen(value) + 1);
    strcpy(var->resetString, value);

    var->flags = flags;
    var->value = (float)atof(value);
    var->integer = atoi(value);
    var->modified = qfalse;
    var->modificationCount = 0;

    /* Link into list */
    var->next = cvar_vars;
    cvar_vars = var;

    /* Hash */
    int hash = Cvar_Hash(name);
    var->hashNext = cvar_hash[hash];
    cvar_hash[hash] = var;

    return var;
}

cvar_t *Cvar_Set(const char *name, const char *value) {
    cvar_t *var = Cvar_Find(name);
    if (!var) return Cvar_Get(name, value, 0);

    Z_Free(var->string);
    var->string = (char *)Z_Malloc((int)strlen(value) + 1);
    strcpy(var->string, value);
    var->value = (float)atof(value);
    var->integer = atoi(value);
    var->modified = qtrue;
    var->modificationCount++;

    return var;
}

cvar_t *Cvar_SetValue(const char *name, float value) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%f", value);
    return Cvar_Set(name, buf);
}

float Cvar_VariableValue(const char *name) {
    cvar_t *var = Cvar_Find(name);
    return var ? var->value : 0.0f;
}

int Cvar_VariableIntegerValue(const char *name) {
    cvar_t *var = Cvar_Find(name);
    return var ? var->integer : 0;
}

char *Cvar_VariableString(const char *name) {
    cvar_t *var = Cvar_Find(name);
    return var ? var->string : "";
}

/* Accessor for tab completion (console.c) */
cvar_t *Cvar_GetVars(void) {
    return cvar_vars;
}
