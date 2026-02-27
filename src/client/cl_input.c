/*
 * cl_input.c -- Client input processing
 *
 * Translates SDL2 input events into game commands.
 * FAKK2's input system supports:
 *   - Keyboard (from default.cfg bindings)
 *   - Mouse (with +attackleft/+attackright for dual-wielding)
 *   - Joystick (via WINMM.DLL joyGetPosEx originally)
 *   - MIDI input (original had MIDI controller support!)
 *
 * Notable FAKK2 bindings (from default.cfg):
 *   MOUSE1  -> +attackleft   (left hand weapon)
 *   MOUSE2  -> +attackright  (right hand weapon)
 *   1-6     -> warpinv       (weapon categories: swords, defense, guns, explosives, bigguns, health)
 *   TAB     -> +cameralook   (third-person camera look)
 *   MOUSE3  -> +use          (interact with objects)
 */

#include "../common/qcommon.h"

/* TODO: Implement input processing */
