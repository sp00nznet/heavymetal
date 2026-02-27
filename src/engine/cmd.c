/*
 * cmd.c -- Command buffer and execution system
 *
 * The command system handles console commands, config file execution,
 * and key binding actions. FAKK2 extends Q3's command system with
 * additional commands for the UberTools features.
 */

#include "qcommon.h"
#include <string.h>
#include <stdlib.h>

/* =========================================================================
 * Command buffer
 * ========================================================================= */

#define MAX_CMD_BUFFER  16384

static struct {
    char    text[MAX_CMD_BUFFER];
    int     len;
} cmd_text;

void Cbuf_Init(void) {
    cmd_text.len = 0;
    cmd_text.text[0] = '\0';
    Com_Printf("Command buffer initialized\n");
}

void Cbuf_AddText(const char *text) {
    int len = (int)strlen(text);
    if (cmd_text.len + len >= MAX_CMD_BUFFER) {
        Com_Printf("Cbuf_AddText: overflow\n");
        return;
    }
    memcpy(cmd_text.text + cmd_text.len, text, len + 1);
    cmd_text.len += len;
}

void Cbuf_InsertText(const char *text) {
    int len = (int)strlen(text);
    if (cmd_text.len + len >= MAX_CMD_BUFFER) {
        Com_Printf("Cbuf_InsertText: overflow\n");
        return;
    }
    memmove(cmd_text.text + len, cmd_text.text, cmd_text.len + 1);
    memcpy(cmd_text.text, text, len);
    cmd_text.len += len;
}

void Cbuf_Execute(void) {
    char    line[1024];
    int     i, quotes;

    while (cmd_text.len) {
        /* Find a complete command line */
        quotes = 0;
        for (i = 0; i < cmd_text.len; i++) {
            if (cmd_text.text[i] == '"') quotes++;
            if (!(quotes & 1) && cmd_text.text[i] == ';') break;
            if (cmd_text.text[i] == '\n' || cmd_text.text[i] == '\r') break;
        }

        if (i >= (int)sizeof(line) - 1) i = sizeof(line) - 2;
        memcpy(line, cmd_text.text, i);
        line[i] = '\0';

        /* Skip past the command and any trailing newline */
        if (i < cmd_text.len) i++;
        cmd_text.len -= i;
        memmove(cmd_text.text, cmd_text.text + i, cmd_text.len + 1);

        /* Execute the line */
        Cmd_ExecuteString(line);
    }
}

/* =========================================================================
 * Command registration
 * ========================================================================= */

typedef struct cmd_function_s {
    struct cmd_function_s   *next;
    char                    *name;
    xcommand_t              function;
} cmd_function_t;

static cmd_function_t *cmd_functions = NULL;

/* Tokenized command state */
static int      cmd_argc;
static char     *cmd_argv[MAX_STRING_TOKENS];
static char     cmd_tokenized[MAX_STRING_CHARS + MAX_STRING_TOKENS];
static char     cmd_cmd[MAX_STRING_CHARS];

void Cmd_Init(void) {
    Com_Printf("Command system initialized\n");
}

void Cmd_AddCommand(const char *name, xcommand_t function) {
    cmd_function_t *cmd = (cmd_function_t *)Z_Malloc(sizeof(cmd_function_t));
    cmd->name = (char *)Z_Malloc((int)strlen(name) + 1);
    strcpy(cmd->name, name);
    cmd->function = function;
    cmd->next = cmd_functions;
    cmd_functions = cmd;
}

void Cmd_RemoveCommand(const char *name) {
    cmd_function_t **back = &cmd_functions;
    for (;;) {
        cmd_function_t *cmd = *back;
        if (!cmd) return;
        if (!Q_stricmp(name, cmd->name)) {
            *back = cmd->next;
            Z_Free(cmd->name);
            Z_Free(cmd);
            return;
        }
        back = &cmd->next;
    }
}

int Cmd_Argc(void) {
    return cmd_argc;
}

char *Cmd_Argv(int arg) {
    if (arg >= cmd_argc) return "";
    return cmd_argv[arg];
}

char *Cmd_Args(void) {
    static char args[MAX_STRING_CHARS];
    args[0] = '\0';
    for (int i = 1; i < cmd_argc; i++) {
        strcat(args, cmd_argv[i]);
        if (i < cmd_argc - 1) strcat(args, " ");
    }
    return args;
}

void Cmd_TokenizeString(const char *text) {
    cmd_argc = 0;
    if (!text) return;

    Q_strncpyz(cmd_cmd, text, sizeof(cmd_cmd));

    char *out = cmd_tokenized;
    while (1) {
        /* Skip whitespace */
        while (*text && *text <= ' ') text++;
        if (!*text) return;

        if (cmd_argc >= MAX_STRING_TOKENS) return;

        cmd_argv[cmd_argc] = out;
        cmd_argc++;

        /* Handle quoted strings */
        if (*text == '"') {
            text++;
            while (*text && *text != '"') *out++ = *text++;
            if (*text == '"') text++;
        } else {
            while (*text > ' ') *out++ = *text++;
        }
        *out++ = '\0';
    }
}

void Cmd_ExecuteString(const char *text) {
    Cmd_TokenizeString(text);
    if (!cmd_argc) return;

    /* Check registered commands */
    for (cmd_function_t *cmd = cmd_functions; cmd; cmd = cmd->next) {
        if (!Q_stricmp(cmd_argv[0], cmd->name)) {
            if (cmd->function) {
                cmd->function();
            }
            return;
        }
    }

    /* Check cvars */
    if (Cvar_VariableString(cmd_argv[0])[0]) {
        if (cmd_argc == 1) {
            Com_Printf("\"%s\" is \"%s\"\n", cmd_argv[0], Cvar_VariableString(cmd_argv[0]));
        } else {
            Cvar_Set(cmd_argv[0], cmd_argv[1]);
        }
        return;
    }

    Com_Printf("Unknown command \"%s\"\n", cmd_argv[0]);
}
