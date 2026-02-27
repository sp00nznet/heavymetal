/*
 * console.c -- In-game drop-down console
 *
 * The drop-down console, activated with ~ (tilde).
 * Provides command input, output history, tab completion, and scroll.
 *
 * FAKK2's console extends the Q3 console with:
 *   - Morpheus script commands visible in output
 *   - Extended tab completion for TIKI model paths
 *   - Developer console with entity information
 *
 * The console maintains a circular buffer of text lines and
 * a command history for up/down arrow recall.
 */

#include "../common/qcommon.h"
#include "../renderer/tr_types.h"
#include <string.h>
#include <ctype.h>

/* =========================================================================
 * Console buffer
 * ========================================================================= */

#define CON_MAX_LINES       1024
#define CON_LINE_LENGTH     256
#define CON_MAX_INPUT       256
#define CON_MAX_HISTORY     64

static struct {
    /* Text buffer -- circular */
    char        lines[CON_MAX_LINES][CON_LINE_LENGTH];
    int         lineCount;      /* total lines ever written */
    int         displayLine;    /* bottom line currently displayed */
    qboolean    scrollLock;     /* auto-scroll to bottom */

    /* Input line */
    char        input[CON_MAX_INPUT];
    int         inputLen;
    int         inputCursor;

    /* Command history */
    char        history[CON_MAX_HISTORY][CON_MAX_INPUT];
    int         historyCount;
    int         historyLine;    /* current browsing position */

    /* Visual state */
    float       displayFrac;    /* 0.0 = hidden, 1.0 = fully visible */
    float       targetFrac;     /* animation target */
    qboolean    initialized;
} con;

/* =========================================================================
 * Console output -- called by Com_Printf
 * ========================================================================= */

void Con_Print(const char *text) {
    if (!con.initialized) return;

    while (*text) {
        int lineIdx = con.lineCount % CON_MAX_LINES;
        char *line = con.lines[lineIdx];
        int len = (int)strlen(line);

        /* Copy characters until newline or end */
        while (*text && *text != '\n') {
            if (len < CON_LINE_LENGTH - 1) {
                line[len++] = *text;
            }
            text++;
        }
        line[len] = '\0';

        if (*text == '\n') {
            text++;
            con.lineCount++;
            /* Clear next line */
            int nextIdx = con.lineCount % CON_MAX_LINES;
            con.lines[nextIdx][0] = '\0';
        }

        /* Auto-scroll to bottom */
        if (!con.scrollLock) {
            con.displayLine = con.lineCount;
        }
    }
}

/* =========================================================================
 * Tab completion
 * ========================================================================= */

/* =========================================================================
 * Tab completion -- match commands and cvars against partial input
 *
 * Walk both the command linked list and cvar linked list to find
 * all names matching the current input prefix. If there's exactly
 * one match, complete it. If there are multiple, complete to the
 * longest common prefix and print the candidates.
 * ========================================================================= */

/* cmd.c exposes its list head via this accessor */
typedef struct cmd_function_s cmd_function_t;
extern cmd_function_t *Cmd_GetFunctions(void);
extern const char *Cmd_FunctionName(const cmd_function_t *cmd);
extern const cmd_function_t *Cmd_FunctionNext(const cmd_function_t *cmd);

/* cvar.c exposes its list head */
extern cvar_t *Cvar_GetVars(void);

static void Con_TabComplete(void) {
    if (con.inputLen == 0) return;

    /* Extract the partial token (first word only) */
    char partial[CON_MAX_INPUT];
    Q_strncpyz(partial, con.input, sizeof(partial));

    /* Trim to first word */
    for (int i = 0; partial[i]; i++) {
        if (partial[i] == ' ') {
            partial[i] = '\0';
            break;
        }
    }

    int partialLen = (int)strlen(partial);
    if (partialLen == 0) return;

    /* Collect matches */
    #define MAX_COMPLETE_MATCHES 256
    const char *matches[MAX_COMPLETE_MATCHES];
    int numMatches = 0;

    /* Search commands */
    for (const cmd_function_t *cmd = Cmd_GetFunctions(); cmd; cmd = Cmd_FunctionNext(cmd)) {
        const char *name = Cmd_FunctionName(cmd);
        if (!Q_stricmpn(name, partial, partialLen)) {
            if (numMatches < MAX_COMPLETE_MATCHES)
                matches[numMatches++] = name;
        }
    }

    /* Search cvars */
    for (cvar_t *var = Cvar_GetVars(); var; var = var->next) {
        if (!Q_stricmpn(var->name, partial, partialLen)) {
            if (numMatches < MAX_COMPLETE_MATCHES)
                matches[numMatches++] = var->name;
        }
    }

    if (numMatches == 0) {
        Com_Printf("No match for \"%s\"\n", partial);
        return;
    }

    if (numMatches == 1) {
        /* Single match -- complete it */
        Q_strncpyz(con.input, matches[0], CON_MAX_INPUT);
        con.inputLen = (int)strlen(con.input);
        /* Append a space for convenience */
        if (con.inputLen < CON_MAX_INPUT - 1) {
            con.input[con.inputLen++] = ' ';
            con.input[con.inputLen] = '\0';
        }
        con.inputCursor = con.inputLen;
        return;
    }

    /* Multiple matches -- find longest common prefix */
    int commonLen = (int)strlen(matches[0]);
    for (int i = 1; i < numMatches; i++) {
        int j = 0;
        while (j < commonLen) {
            char a = matches[0][j];
            char b = matches[i][j];
            /* Case-insensitive comparison */
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) break;
            j++;
        }
        commonLen = j;
    }

    /* Complete to common prefix if it's longer than what we have */
    if (commonLen > partialLen) {
        memcpy(con.input, matches[0], commonLen);
        con.input[commonLen] = '\0';
        con.inputLen = commonLen;
        con.inputCursor = commonLen;
    }

    /* Print all matches */
    Com_Printf("] %s\n", partial);
    for (int i = 0; i < numMatches; i++) {
        Com_Printf("  %s\n", matches[i]);
    }

    #undef MAX_COMPLETE_MATCHES
}

/* =========================================================================
 * Input handling
 * ========================================================================= */

void Con_KeyEvent(int key, qboolean down) {
    if (!down) return;

    switch (key) {
        case K_ENTER: {
            /* Execute the command */
            if (con.inputLen > 0) {
                Com_Printf("] %s\n", con.input);

                /* Add to history */
                if (con.historyCount < CON_MAX_HISTORY) {
                    Q_strncpyz(con.history[con.historyCount], con.input, CON_MAX_INPUT);
                    con.historyCount++;
                } else {
                    memmove(con.history[0], con.history[1],
                            (CON_MAX_HISTORY - 1) * CON_MAX_INPUT);
                    Q_strncpyz(con.history[CON_MAX_HISTORY - 1], con.input, CON_MAX_INPUT);
                }
                con.historyLine = con.historyCount;

                /* Execute */
                Cbuf_AddText(con.input);
                Cbuf_AddText("\n");

                /* Clear input */
                con.input[0] = '\0';
                con.inputLen = 0;
                con.inputCursor = 0;
            }
            break;
        }

        case K_BACKSPACE:
            if (con.inputCursor > 0) {
                memmove(con.input + con.inputCursor - 1,
                        con.input + con.inputCursor,
                        con.inputLen - con.inputCursor + 1);
                con.inputCursor--;
                con.inputLen--;
            }
            break;

        case K_DEL:
            if (con.inputCursor < con.inputLen) {
                memmove(con.input + con.inputCursor,
                        con.input + con.inputCursor + 1,
                        con.inputLen - con.inputCursor);
                con.inputLen--;
            }
            break;

        case K_LEFTARROW:
            if (con.inputCursor > 0) con.inputCursor--;
            break;

        case K_RIGHTARROW:
            if (con.inputCursor < con.inputLen) con.inputCursor++;
            break;

        case K_HOME:
            con.inputCursor = 0;
            break;

        case K_END:
            con.inputCursor = con.inputLen;
            break;

        case K_UPARROW:
            /* History browse up */
            if (con.historyLine > 0) {
                con.historyLine--;
                Q_strncpyz(con.input, con.history[con.historyLine], CON_MAX_INPUT);
                con.inputLen = (int)strlen(con.input);
                con.inputCursor = con.inputLen;
            }
            break;

        case K_DOWNARROW:
            /* History browse down */
            if (con.historyLine < con.historyCount - 1) {
                con.historyLine++;
                Q_strncpyz(con.input, con.history[con.historyLine], CON_MAX_INPUT);
                con.inputLen = (int)strlen(con.input);
                con.inputCursor = con.inputLen;
            } else {
                con.historyLine = con.historyCount;
                con.input[0] = '\0';
                con.inputLen = 0;
                con.inputCursor = 0;
            }
            break;

        case K_PGUP:
            /* Scroll up */
            con.displayLine -= 4;
            if (con.displayLine < 0) con.displayLine = 0;
            con.scrollLock = qtrue;
            break;

        case K_PGDN:
            /* Scroll down */
            con.displayLine += 4;
            if (con.displayLine > con.lineCount) {
                con.displayLine = con.lineCount;
                con.scrollLock = qfalse;
            }
            break;

        case K_TAB:
            Con_TabComplete();
            break;
    }
}

void Con_CharEvent(int ch) {
    /* Insert printable character at cursor */
    if (ch < 32 || ch >= 127) return;
    if (con.inputLen >= CON_MAX_INPUT - 1) return;

    /* Make room at cursor */
    memmove(con.input + con.inputCursor + 1,
            con.input + con.inputCursor,
            con.inputLen - con.inputCursor + 1);
    con.input[con.inputCursor] = (char)ch;
    con.inputCursor++;
    con.inputLen++;
}

/* =========================================================================
 * Console rendering
 *
 * The console draws as a semi-transparent overlay at the top of the screen.
 * It slides down when activated and up when deactivated.
 * ========================================================================= */

void Con_DrawConsole(void) {
    extern qboolean Key_GetConsoleActive(void);

    /* Update animation */
    float target = Key_GetConsoleActive() ? 0.5f : 0.0f;
    float speed = 0.05f;

    if (con.displayFrac < target) {
        con.displayFrac += speed;
        if (con.displayFrac > target) con.displayFrac = target;
    } else if (con.displayFrac > target) {
        con.displayFrac -= speed;
        if (con.displayFrac < target) con.displayFrac = target;
    }

    if (con.displayFrac <= 0.001f) return;

    /* GL 2D drawing functions from r_gl.c */
    extern void R_Set2D(void);
    extern void R_DrawFillRect(float x, float y, float w, float h,
                               float r, float g, float b, float a);
    extern void R_DrawString(float x, float y, const char *str,
                             float scale, float r, float g, float b, float a);
    extern void R_GetGlconfig(glconfig_t *config);

    glconfig_t gc;
    R_GetGlconfig(&gc);
    float screenW = (float)gc.vidWidth;
    float screenH = (float)gc.vidHeight;

    R_Set2D();

    /* Console background */
    float consoleH = screenH * con.displayFrac;
    R_DrawFillRect(0, 0, screenW, consoleH, 0.0f, 0.0f, 0.0f, 0.8f);

    /* Version bar at bottom of console */
    R_DrawFillRect(0, consoleH - 2.0f, screenW, 2.0f, 0.7f, 0.4f, 0.0f, 1.0f);

    /* Text rendering */
    float charH = 14.0f;
    float charW = 8.0f;
    float padX = 4.0f;

    /* Draw input line at bottom */
    float inputY = consoleH - charH - 4.0f;
    R_DrawString(padX, inputY, "]", 1.0f, 0.7f, 0.7f, 0.7f, 1.0f);
    if (con.inputLen > 0) {
        R_DrawString(padX + charW, inputY, con.input, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    }

    /* Blinking cursor */
    int cursorBlink = (Sys_Milliseconds() / 500) & 1;
    if (cursorBlink) {
        float cx = padX + charW * (1 + con.inputCursor);
        R_DrawFillRect(cx, inputY + 2.0f, charW, charH - 4.0f, 1.0f, 1.0f, 1.0f, 0.7f);
    }

    /* Draw text lines (bottom-up from input line) */
    int maxVisLines = (int)((inputY - 4.0f) / charH);
    int startLine = con.displayLine;

    for (int i = 0; i < maxVisLines; i++) {
        int lineIdx = startLine - i;
        if (lineIdx < 0) break;
        if (lineIdx < con.lineCount - CON_MAX_LINES) break;

        const char *line = con.lines[lineIdx % CON_MAX_LINES];
        if (!line[0]) continue;

        float y = inputY - charH * (i + 1);
        if (y < 0) break;

        R_DrawString(padX, y, line, 1.0f, 0.8f, 0.8f, 0.8f, 1.0f);
    }
}

/* =========================================================================
 * Console commands
 * ========================================================================= */

static void Con_Clear_f(void) {
    for (int i = 0; i < CON_MAX_LINES; i++) {
        con.lines[i][0] = '\0';
    }
    con.lineCount = 0;
    con.displayLine = 0;
    Com_Printf("Console cleared\n");
}

static void Con_Dump_f(void) {
    if (Cmd_Argc() < 2) {
        Com_Printf("Usage: condump <filename>\n");
        return;
    }

    fileHandle_t f;
    FS_FOpenFileWrite(Cmd_Argv(1), &f);
    if (!f) {
        Com_Printf("Couldn't open %s for writing\n", Cmd_Argv(1));
        return;
    }

    int start = con.lineCount - CON_MAX_LINES;
    if (start < 0) start = 0;

    for (int i = start; i <= con.lineCount; i++) {
        const char *line = con.lines[i % CON_MAX_LINES];
        FS_Write(line, (int)strlen(line), f);
        FS_Write("\n", 1, f);
    }

    FS_FCloseFile(f);
    Com_Printf("Dumped console to %s\n", Cmd_Argv(1));
}

static void Con_ToggleConsole_f(void) {
    /* Toggle is handled by key event on '`' in keys.c, but this
     * command allows binding toggleconsole to other keys too.
     * The actual toggle logic is in Key_KeyEvent. */
}

/* =========================================================================
 * Initialization / Shutdown
 * ========================================================================= */

void Con_Init(void) {
    memset(&con, 0, sizeof(con));
    con.initialized = qtrue;
    con.historyLine = 0;

    Cmd_AddCommand("clear", Con_Clear_f);
    Cmd_AddCommand("condump", Con_Dump_f);
    Cmd_AddCommand("toggleconsole", Con_ToggleConsole_f);

    Com_Printf("Console initialized\n");
}

void Con_Shutdown(void) {
    Cmd_RemoveCommand("clear");
    Cmd_RemoveCommand("condump");
    Cmd_RemoveCommand("toggleconsole");
    con.initialized = qfalse;
}
