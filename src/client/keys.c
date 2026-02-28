/*
 * keys.c -- Key binding system
 *
 * Handles key press/release events and binding execution.
 * FAKK2's input system supports three binding layers:
 *   - Standard binds: bind KEY "command"
 *   - Ctrl binds:     ctrlbind KEY "command"  (Ctrl modifier)
 *   - Alt binds:      altbind KEY "command"   (Alt modifier)
 *
 * Notable FAKK2 bindings (from default.cfg):
 *   MOUSE1  -> +attackleft   (left hand weapon)
 *   MOUSE2  -> +attackright  (right hand weapon)
 *   1-6     -> warpinv       (weapon categories)
 *   TAB     -> +cameralook   (third-person camera look)
 *   MOUSE3  -> +use          (interact with objects)
 *   ~       -> toggleconsole
 *
 * Key state machine:
 *   Keys can be in one of three catch destinations:
 *     KEY_GAME    - commands go to game (normal play)
 *     KEY_CONSOLE - typing goes to console input
 *     KEY_MESSAGE - typing goes to chat message
 */

#include "../common/qcommon.h"
#include <string.h>
#include <ctype.h>

/* =========================================================================
 * Key state
 * ========================================================================= */

#define MAX_KEYS        256
#define MAX_BIND_LENGTH 256

typedef enum {
    KEY_GAME,
    KEY_CONSOLE,
    KEY_MESSAGE,
    KEY_MENU
} keyCatch_t;

static char         key_bindings[MAX_KEYS][MAX_BIND_LENGTH];
static char         key_ctrlbindings[MAX_KEYS][MAX_BIND_LENGTH];
static char         key_altbindings[MAX_KEYS][MAX_BIND_LENGTH];
static qboolean     key_down[MAX_KEYS];
static int          key_repeats[MAX_KEYS];
static unsigned int key_downtime[MAX_KEYS];
static keyCatch_t   key_catcher = KEY_GAME;

/* Console state (shared with console.c) */
static qboolean     console_active = qfalse;

qboolean Key_GetConsoleActive(void) { return console_active; }
keyCatch_t Key_GetCatcher(void) { return (int)key_catcher; }

void Key_SetCatcher(int catcher) {
    key_catcher = (keyCatch_t)catcher;
}

qboolean Key_IsDown(int key) {
    if (key < 0 || key >= MAX_KEYS) return qfalse;
    return key_down[key];
}

/* =========================================================================
 * Key name table -- maps key names to keyNum_t values
 * ========================================================================= */

typedef struct {
    const char  *name;
    int         keynum;
} keyname_t;

static const keyname_t keynames[] = {
    { "TAB",            K_TAB },
    { "ENTER",          K_ENTER },
    { "ESCAPE",         K_ESCAPE },
    { "SPACE",          K_SPACE },
    { "BACKSPACE",      K_BACKSPACE },
    { "UPARROW",        K_UPARROW },
    { "DOWNARROW",      K_DOWNARROW },
    { "LEFTARROW",      K_LEFTARROW },
    { "RIGHTARROW",     K_RIGHTARROW },
    { "ALT",            K_ALT },
    { "CTRL",           K_CTRL },
    { "SHIFT",          K_SHIFT },
    { "F1",             K_F1 },
    { "F2",             K_F2 },
    { "F3",             K_F3 },
    { "F4",             K_F4 },
    { "F5",             K_F5 },
    { "F6",             K_F6 },
    { "F7",             K_F7 },
    { "F8",             K_F8 },
    { "F9",             K_F9 },
    { "F10",            K_F10 },
    { "F11",            K_F11 },
    { "F12",            K_F12 },
    { "INS",            K_INS },
    { "DEL",            K_DEL },
    { "PGDN",           K_PGDN },
    { "PGUP",           K_PGUP },
    { "HOME",           K_HOME },
    { "END",            K_END },
    { "MOUSE1",         K_MOUSE1 },
    { "MOUSE2",         K_MOUSE2 },
    { "MOUSE3",         K_MOUSE3 },
    { "MOUSE4",         K_MOUSE4 },
    { "MOUSE5",         K_MOUSE5 },
    { "MWHEELDOWN",     K_MWHEELDOWN },
    { "MWHEELUP",       K_MWHEELUP },
    { "JOY1",           K_JOY1 },
    { "PAUSE",          K_PAUSE },
    { "SEMICOLON",      ';' },
    { NULL,             0 }
};

int Key_StringToKeynum(const char *str) {
    if (!str || !str[0]) return -1;

    /* Single ASCII character */
    if (!str[1]) return (int)(unsigned char)tolower((unsigned char)str[0]);

    /* Named keys */
    for (const keyname_t *kn = keynames; kn->name; kn++) {
        if (!Q_stricmp(str, kn->name)) return kn->keynum;
    }

    Com_Printf("Unknown key name: \"%s\"\n", str);
    return -1;
}

const char *Key_KeynumToString(int keynum) {
    static char buf[32];

    if (keynum < 0 || keynum >= MAX_KEYS) return "<INVALID>";

    /* Named keys */
    for (const keyname_t *kn = keynames; kn->name; kn++) {
        if (kn->keynum == keynum) return kn->name;
    }

    /* Printable ASCII */
    if (keynum >= 32 && keynum < 127) {
        buf[0] = (char)keynum;
        buf[1] = '\0';
        return buf;
    }

    snprintf(buf, sizeof(buf), "0x%02x", keynum);
    return buf;
}

/* =========================================================================
 * Bind commands
 * ========================================================================= */

static void Key_Bind_f(void) {
    int argc = Cmd_Argc();
    if (argc < 2) {
        Com_Printf("Usage: bind <key> [command]\n");
        return;
    }

    int keynum = Key_StringToKeynum(Cmd_Argv(1));
    if (keynum < 0) return;

    if (argc == 2) {
        if (key_bindings[keynum][0])
            Com_Printf("\"%s\" = \"%s\"\n", Cmd_Argv(1), key_bindings[keynum]);
        else
            Com_Printf("\"%s\" is not bound\n", Cmd_Argv(1));
        return;
    }

    /* Combine remaining args into bind string */
    char cmd[MAX_BIND_LENGTH] = "";
    for (int i = 2; i < argc; i++) {
        if (i > 2) strcat(cmd, " ");
        strcat(cmd, Cmd_Argv(i));
    }
    Q_strncpyz(key_bindings[keynum], cmd, MAX_BIND_LENGTH);
}

static void Key_Unbind_f(void) {
    if (Cmd_Argc() < 2) {
        Com_Printf("Usage: unbind <key>\n");
        return;
    }

    int keynum = Key_StringToKeynum(Cmd_Argv(1));
    if (keynum < 0) return;

    key_bindings[keynum][0] = '\0';
}

static void Key_Unbindall_f(void) {
    for (int i = 0; i < MAX_KEYS; i++) {
        key_bindings[i][0] = '\0';
        key_ctrlbindings[i][0] = '\0';
        key_altbindings[i][0] = '\0';
    }
}

static void Key_CtrlBind_f(void) {
    int argc = Cmd_Argc();
    if (argc < 3) {
        Com_Printf("Usage: ctrlbind <key> <command>\n");
        return;
    }

    int keynum = Key_StringToKeynum(Cmd_Argv(1));
    if (keynum < 0) return;

    char cmd[MAX_BIND_LENGTH] = "";
    for (int i = 2; i < argc; i++) {
        if (i > 2) strcat(cmd, " ");
        strcat(cmd, Cmd_Argv(i));
    }
    Q_strncpyz(key_ctrlbindings[keynum], cmd, MAX_BIND_LENGTH);
}

static void Key_AltBind_f(void) {
    int argc = Cmd_Argc();
    if (argc < 3) {
        Com_Printf("Usage: altbind <key> <command>\n");
        return;
    }

    int keynum = Key_StringToKeynum(Cmd_Argv(1));
    if (keynum < 0) return;

    char cmd[MAX_BIND_LENGTH] = "";
    for (int i = 2; i < argc; i++) {
        if (i > 2) strcat(cmd, " ");
        strcat(cmd, Cmd_Argv(i));
    }
    Q_strncpyz(key_altbindings[keynum], cmd, MAX_BIND_LENGTH);
}

static void Key_Bindlist_f(void) {
    int count = 0;
    for (int i = 0; i < MAX_KEYS; i++) {
        if (key_bindings[i][0]) {
            Com_Printf("%-16s \"%s\"\n", Key_KeynumToString(i), key_bindings[i]);
            count++;
        }
    }
    Com_Printf("%d bindings listed\n", count);
}

/* =========================================================================
 * Key event processing
 * ========================================================================= */

void Key_KeyEvent(int key, qboolean down, unsigned int time) {
    if (key < 0 || key >= MAX_KEYS) return;

    /* Track state */
    if (down) {
        key_repeats[key]++;
        if (key_repeats[key] == 1) key_downtime[key] = time;
    } else {
        key_repeats[key] = 0;
    }
    key_down[key] = down;

    /* Console toggle -- ` (grave/tilde) always toggles console */
    if (key == '`' && down) {
        console_active = !console_active;
        key_catcher = console_active ? KEY_CONSOLE : KEY_GAME;
        return;
    }

    /* Escape -- close console or menu */
    if (key == K_ESCAPE && down) {
        if (console_active) {
            console_active = qfalse;
            key_catcher = KEY_GAME;
            return;
        }
        /* Toggle menu state */
        if (key_catcher == KEY_MENU) {
            /* Close menu -> return to game */
            key_catcher = KEY_GAME;
            Com_DPrintf("Menu closed\n");
        } else {
            /* Open menu */
            key_catcher = KEY_MENU;
            Com_DPrintf("Menu opened\n");
        }
        return;
    }

    /* If console is active, feed keys to console */
    if (key_catcher == KEY_CONSOLE) {
        /* Console handles its own key processing in console.c */
        /* Pass through to Con_KeyEvent */
        extern void Con_KeyEvent(int key, qboolean down);
        if (down) Con_KeyEvent(key, down);
        return;
    }

    /* Game mode -- execute bindings */
    if (key_catcher == KEY_GAME) {
        const char *bind = NULL;

        /* Check modifier bindings first */
        if (key_down[K_CTRL] && key_ctrlbindings[key][0]) {
            bind = key_ctrlbindings[key];
        } else if (key_down[K_ALT] && key_altbindings[key][0]) {
            bind = key_altbindings[key];
        } else if (key_bindings[key][0]) {
            bind = key_bindings[key];
        }

        if (bind) {
            if (bind[0] == '+') {
                /* +command / -command -- button-style bindings */
                if (down) {
                    char cmd[MAX_BIND_LENGTH + 16];
                    snprintf(cmd, sizeof(cmd), "%s %d %u\n", bind, key, time);
                    Cbuf_AddText(cmd);
                } else {
                    char cmd[MAX_BIND_LENGTH + 16];
                    /* Replace + with - for release */
                    snprintf(cmd, sizeof(cmd), "-%s %d %u\n", bind + 1, key, time);
                    Cbuf_AddText(cmd);
                }
            } else if (down) {
                /* Regular command -- only execute on press */
                Cbuf_AddText(bind);
                Cbuf_AddText("\n");
            }
        }
    }
}

void Key_CharEvent(int ch) {
    /* Character events only matter for console/message input */
    if (key_catcher == KEY_CONSOLE) {
        extern void Con_CharEvent(int ch);
        Con_CharEvent(ch);
    }
}

/* =========================================================================
 * Key binding query (for config saving)
 * ========================================================================= */

const char *Key_GetBinding(int keynum) {
    if (keynum < 0 || keynum >= MAX_KEYS) return "";
    return key_bindings[keynum];
}

void Key_SetBinding(int keynum, const char *binding) {
    if (keynum < 0 || keynum >= MAX_KEYS) return;
    Q_strncpyz(key_bindings[keynum], binding, MAX_BIND_LENGTH);
}

/* =========================================================================
 * Write bindings to config file
 * ========================================================================= */

void Key_WriteBindings(fileHandle_t f) {
    char buf[MAX_BIND_LENGTH + 64];
    for (int i = 0; i < MAX_KEYS; i++) {
        if (key_bindings[i][0]) {
            snprintf(buf, sizeof(buf), "bind %s \"%s\"\n",
                     Key_KeynumToString(i), key_bindings[i]);
            FS_Write(buf, (int)strlen(buf), f);
        }
        if (key_ctrlbindings[i][0]) {
            snprintf(buf, sizeof(buf), "ctrlbind %s \"%s\"\n",
                     Key_KeynumToString(i), key_ctrlbindings[i]);
            FS_Write(buf, (int)strlen(buf), f);
        }
        if (key_altbindings[i][0]) {
            snprintf(buf, sizeof(buf), "altbind %s \"%s\"\n",
                     Key_KeynumToString(i), key_altbindings[i]);
            FS_Write(buf, (int)strlen(buf), f);
        }
    }
}

/* =========================================================================
 * Initialization / Shutdown
 * ========================================================================= */

void Key_Init(void) {
    memset(key_bindings, 0, sizeof(key_bindings));
    memset(key_ctrlbindings, 0, sizeof(key_ctrlbindings));
    memset(key_altbindings, 0, sizeof(key_altbindings));
    memset(key_down, 0, sizeof(key_down));
    memset(key_repeats, 0, sizeof(key_repeats));

    /* Register commands */
    Cmd_AddCommand("bind", Key_Bind_f);
    Cmd_AddCommand("unbind", Key_Unbind_f);
    Cmd_AddCommand("unbindall", Key_Unbindall_f);
    Cmd_AddCommand("ctrlbind", Key_CtrlBind_f);
    Cmd_AddCommand("altbind", Key_AltBind_f);
    Cmd_AddCommand("bindlist", Key_Bindlist_f);

    Com_Printf("Key binding system initialized\n");
}

void Key_Shutdown(void) {
    Cmd_RemoveCommand("bind");
    Cmd_RemoveCommand("unbind");
    Cmd_RemoveCommand("unbindall");
    Cmd_RemoveCommand("ctrlbind");
    Cmd_RemoveCommand("altbind");
    Cmd_RemoveCommand("bindlist");
}
