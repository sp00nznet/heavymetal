/*
 * cl_input.c -- Client input processing
 *
 * Translates input events into user commands (usercmd_t) sent to the server.
 * FAKK2's input system supports:
 *   - Keyboard (from default.cfg bindings)
 *   - Mouse (with +attackleft/+attackright for dual-wielding)
 *   - Joystick (via SDL2 game controller API)
 *
 * The +command/-command system:
 *   "+forward" on key press -> sets forward move
 *   "-forward" on key release -> clears forward move
 *
 * Notable FAKK2 bindings (from default.cfg):
 *   MOUSE1  -> +attackleft   (left hand weapon)
 *   MOUSE2  -> +attackright  (right hand weapon)
 *   1-6     -> warpinv       (weapon categories: swords, defense, guns, explosives, bigguns, health)
 *   TAB     -> +cameralook   (third-person camera look)
 *   MOUSE3  -> +use          (interact with objects)
 *   w/a/s/d -> +forward/+moveleft/+back/+moveright
 */

#include "../common/qcommon.h"
#include <string.h>

/* =========================================================================
 * Button state tracking
 *
 * Each +command creates a "button" that tracks press/release state.
 * Multiple keys can activate the same button (e.g., both W and UPARROW
 * can activate +forward).
 * ========================================================================= */

typedef struct {
    int     down[2];    /* key numbers holding this button down */
    int     downtime;   /* msec when first pressed */
    int     msec;       /* accumulated msec while down */
    qboolean active;
    qboolean wasPressed;
} kbutton_t;

static kbutton_t   in_forward;
static kbutton_t   in_back;
static kbutton_t   in_moveleft;
static kbutton_t   in_moveright;
static kbutton_t   in_moveup;
static kbutton_t   in_movedown;
static kbutton_t   in_speed;        /* walk/run toggle */
static kbutton_t   in_attackleft;   /* FAKK2: left hand attack */
static kbutton_t   in_attackright;  /* FAKK2: right hand attack */
static kbutton_t   in_use;
static kbutton_t   in_cameralook;   /* FAKK2: third-person camera free-look */
static kbutton_t   in_lookup;
static kbutton_t   in_lookdown;
static kbutton_t   in_left;
static kbutton_t   in_right;

/* Mouse sensitivity */
static cvar_t *sensitivity;
static cvar_t *cl_mouseaccel;
static cvar_t *m_pitch;
static cvar_t *m_yaw;
static cvar_t *m_forward;
static cvar_t *m_side;

/* =========================================================================
 * Button command handlers
 * ========================================================================= */

static void KeyDown(kbutton_t *b) {
    int k = atoi(Cmd_Argv(1));
    if (k == b->down[0] || k == b->down[1]) return;

    if (!b->down[0]) {
        b->down[0] = k;
    } else if (!b->down[1]) {
        b->down[1] = k;
    } else {
        Com_Printf("Three keys down for a button!\n");
        return;
    }

    if (b->active) return;
    b->active = qtrue;
    b->wasPressed = qtrue;
    b->downtime = atoi(Cmd_Argv(2));
}

static void KeyUp(kbutton_t *b) {
    int k = atoi(Cmd_Argv(1));

    if (b->down[0] == k) {
        b->down[0] = 0;
    } else if (b->down[1] == k) {
        b->down[1] = 0;
    } else {
        return;
    }

    if (b->down[0] || b->down[1]) return;

    b->active = qfalse;
}

/* Button command registrations */
static void IN_ForwardDown(void)    { KeyDown(&in_forward); }
static void IN_ForwardUp(void)      { KeyUp(&in_forward); }
static void IN_BackDown(void)       { KeyDown(&in_back); }
static void IN_BackUp(void)         { KeyUp(&in_back); }
static void IN_MoveLeftDown(void)   { KeyDown(&in_moveleft); }
static void IN_MoveLeftUp(void)     { KeyUp(&in_moveleft); }
static void IN_MoveRightDown(void)  { KeyDown(&in_moveright); }
static void IN_MoveRightUp(void)    { KeyUp(&in_moveright); }
static void IN_MoveUpDown(void)     { KeyDown(&in_moveup); }
static void IN_MoveUpUp(void)       { KeyUp(&in_moveup); }
static void IN_MoveDownDown(void)   { KeyDown(&in_movedown); }
static void IN_MoveDownUp(void)     { KeyUp(&in_movedown); }
static void IN_SpeedDown(void)      { KeyDown(&in_speed); }
static void IN_SpeedUp(void)        { KeyUp(&in_speed); }
static void IN_AttackLeftDown(void) { KeyDown(&in_attackleft); }
static void IN_AttackLeftUp(void)   { KeyUp(&in_attackleft); }
static void IN_AttackRightDown(void){ KeyDown(&in_attackright); }
static void IN_AttackRightUp(void)  { KeyUp(&in_attackright); }
static void IN_UseDown(void)        { KeyDown(&in_use); }
static void IN_UseUp(void)          { KeyUp(&in_use); }
static void IN_CameraLookDown(void) { KeyDown(&in_cameralook); }
static void IN_CameraLookUp(void)   { KeyUp(&in_cameralook); }
static void IN_LookUpDown(void)     { KeyDown(&in_lookup); }
static void IN_LookUpUp(void)       { KeyUp(&in_lookup); }
static void IN_LookDownDown(void)   { KeyDown(&in_lookdown); }
static void IN_LookDownUp(void)     { KeyUp(&in_lookdown); }
static void IN_LeftDown(void)       { KeyDown(&in_left); }
static void IN_LeftUp(void)         { KeyUp(&in_left); }
static void IN_RightDown(void)      { KeyDown(&in_right); }
static void IN_RightUp(void)        { KeyUp(&in_right); }

/* =========================================================================
 * Build user command from button state
 * ========================================================================= */

static float CL_KeyState(kbutton_t *key) {
    /* Returns 0.0 to 1.0 based on how long key was held this frame */
    float val = key->active ? 1.0f : 0.0f;
    key->wasPressed = qfalse;
    return val;
}

void CL_CreateCmd(usercmd_t *cmd, int serverTime, int mouseDx, int mouseDy) {
    memset(cmd, 0, sizeof(*cmd));
    cmd->serverTime = serverTime;

    /* Movement */
    float forward = CL_KeyState(&in_forward) - CL_KeyState(&in_back);
    float right = CL_KeyState(&in_moveright) - CL_KeyState(&in_moveleft);
    float up = CL_KeyState(&in_moveup) - CL_KeyState(&in_movedown);

    /* Speed -- scale movement to [-127, 127] */
    float scale = in_speed.active ? 64.0f : 127.0f;
    cmd->forwardmove = (signed char)(forward * scale);
    cmd->rightmove = (signed char)(right * scale);
    cmd->upmove = (signed char)(up * scale);

    /* Buttons */
    if (in_attackleft.active)   cmd->buttons |= 1;
    if (in_attackright.active)  cmd->buttons |= 2;
    if (in_use.active)          cmd->buttons |= 4;

    /* Mouse look */
    if (sensitivity && m_yaw && m_pitch) {
        float sens = sensitivity->value;
        cmd->angles[YAW] -= (int)(mouseDx * sens * m_yaw->value);
        cmd->angles[PITCH] += (int)(mouseDy * sens * m_pitch->value);
    }
}

/* =========================================================================
 * Initialization
 * ========================================================================= */

void CL_InitInput(void) {
    /* Register movement cvars */
    sensitivity = Cvar_Get("sensitivity", "5", CVAR_ARCHIVE);
    cl_mouseaccel = Cvar_Get("cl_mouseaccel", "0", CVAR_ARCHIVE);
    m_pitch = Cvar_Get("m_pitch", "0.022", CVAR_ARCHIVE);
    m_yaw = Cvar_Get("m_yaw", "0.022", CVAR_ARCHIVE);
    m_forward = Cvar_Get("m_forward", "0.25", CVAR_ARCHIVE);
    m_side = Cvar_Get("m_side", "0.25", CVAR_ARCHIVE);

    /* Register +/- commands */
    Cmd_AddCommand("+forward",      IN_ForwardDown);
    Cmd_AddCommand("-forward",      IN_ForwardUp);
    Cmd_AddCommand("+back",         IN_BackDown);
    Cmd_AddCommand("-back",         IN_BackUp);
    Cmd_AddCommand("+moveleft",     IN_MoveLeftDown);
    Cmd_AddCommand("-moveleft",     IN_MoveLeftUp);
    Cmd_AddCommand("+moveright",    IN_MoveRightDown);
    Cmd_AddCommand("-moveright",    IN_MoveRightUp);
    Cmd_AddCommand("+moveup",       IN_MoveUpDown);
    Cmd_AddCommand("-moveup",       IN_MoveUpUp);
    Cmd_AddCommand("+movedown",     IN_MoveDownDown);
    Cmd_AddCommand("-movedown",     IN_MoveDownUp);
    Cmd_AddCommand("+speed",        IN_SpeedDown);
    Cmd_AddCommand("-speed",        IN_SpeedUp);
    Cmd_AddCommand("+attackleft",   IN_AttackLeftDown);
    Cmd_AddCommand("-attackleft",   IN_AttackLeftUp);
    Cmd_AddCommand("+attackright",  IN_AttackRightDown);
    Cmd_AddCommand("-attackright",  IN_AttackRightUp);
    Cmd_AddCommand("+use",          IN_UseDown);
    Cmd_AddCommand("-use",          IN_UseUp);
    Cmd_AddCommand("+cameralook",   IN_CameraLookDown);
    Cmd_AddCommand("-cameralook",   IN_CameraLookUp);
    Cmd_AddCommand("+lookup",       IN_LookUpDown);
    Cmd_AddCommand("-lookup",       IN_LookUpUp);
    Cmd_AddCommand("+lookdown",     IN_LookDownDown);
    Cmd_AddCommand("-lookdown",     IN_LookDownUp);
    Cmd_AddCommand("+left",         IN_LeftDown);
    Cmd_AddCommand("-left",         IN_LeftUp);
    Cmd_AddCommand("+right",        IN_RightDown);
    Cmd_AddCommand("-right",        IN_RightUp);

    Com_Printf("Input commands registered\n");
}

void CL_ShutdownInput(void) {
    Cmd_RemoveCommand("+forward");
    Cmd_RemoveCommand("-forward");
    Cmd_RemoveCommand("+back");
    Cmd_RemoveCommand("-back");
    Cmd_RemoveCommand("+moveleft");
    Cmd_RemoveCommand("-moveleft");
    Cmd_RemoveCommand("+moveright");
    Cmd_RemoveCommand("-moveright");
    Cmd_RemoveCommand("+moveup");
    Cmd_RemoveCommand("-moveup");
    Cmd_RemoveCommand("+movedown");
    Cmd_RemoveCommand("-movedown");
    Cmd_RemoveCommand("+speed");
    Cmd_RemoveCommand("-speed");
    Cmd_RemoveCommand("+attackleft");
    Cmd_RemoveCommand("-attackleft");
    Cmd_RemoveCommand("+attackright");
    Cmd_RemoveCommand("-attackright");
    Cmd_RemoveCommand("+use");
    Cmd_RemoveCommand("-use");
    Cmd_RemoveCommand("+cameralook");
    Cmd_RemoveCommand("-cameralook");
    Cmd_RemoveCommand("+lookup");
    Cmd_RemoveCommand("-lookup");
    Cmd_RemoveCommand("+lookdown");
    Cmd_RemoveCommand("-lookdown");
    Cmd_RemoveCommand("+left");
    Cmd_RemoveCommand("-left");
    Cmd_RemoveCommand("+right");
    Cmd_RemoveCommand("-right");
}
