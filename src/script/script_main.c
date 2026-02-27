/*
 * script_main.c -- Morpheus scripting engine initialization
 */

#include "script.h"
#include "../common/qcommon.h"

static qboolean script_initialized = qfalse;

void Script_Init(void) {
    Com_Printf("--- Script_Init ---\n");
    Com_Printf("Morpheus scripting engine: ~%d commands\n", MORPHEUS_MAX_COMMANDS);
    script_initialized = qtrue;
}

void Script_Shutdown(void) {
    if (!script_initialized) return;
    Com_Printf("Morpheus scripting engine shutdown\n");
    script_initialized = qfalse;
}

void Script_Execute(const char *script_text) {
    (void)script_text;
    /* TODO: Parse and execute Morpheus script commands */
}

void Script_ExecuteFile(const char *filename) {
    (void)filename;
    /* TODO: Load and execute script file */
}

void Script_RegisterCommand(const char *name, script_cmd_func_t func) {
    (void)name; (void)func;
    /* TODO: Register script command */
}
