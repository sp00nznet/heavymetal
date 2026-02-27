/*
 * script_vm.c -- Morpheus script built-in commands
 *
 * Registers the core set of Morpheus commands that the engine handles
 * directly. These cover entity manipulation, timing, debugging, and
 * cinematic control. Game-specific commands (~400+) are registered by
 * the game DLL via the script command API.
 *
 * FAKK2 Morpheus command categories:
 *   - Entity: move, rotate, scale, hide, show, damage, kill, remove
 *   - Animation: anim, upperanim, exec, exec_script
 *   - AI: turnto, lookat, runto, walkto, idle, behavior
 *   - Sound: playsound, stopsound, loopsound, dialog
 *   - Camera: cam, camzoom, camfov, followpath
 *   - Script flow: wait, waitframe, end, goto, thread, if/else
 *   - Variables: set, local, get
 *   - Debug: print, dprintln, assert
 *   - Cinematic: letterbox, musicmood, fadein, fadeout, freeze
 */

#include "script.h"
#include "../common/qcommon.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* =========================================================================
 * Helper: parse vec3 from three string args
 * ========================================================================= */

static void Script_ParseVec3(const char *x, const char *y, const char *z, vec3_t out) {
    out[0] = (float)atof(x);
    out[1] = (float)atof(y);
    out[2] = (float)atof(z);
}

/* =========================================================================
 * Debug / Print commands
 * ========================================================================= */

static qboolean Cmd_Print(scriptThread_t *thread, scriptArgs_t *args) {
    (void)thread;
    char buf[1024] = "";
    for (int i = 1; i < args->argc; i++) {
        if (i > 1) Q_strcat(buf, sizeof(buf), " ");
        Q_strcat(buf, sizeof(buf), args->argv[i]);
    }
    Com_Printf("%s\n", buf);
    return qtrue;
}

static qboolean Cmd_DPrintln(scriptThread_t *thread, scriptArgs_t *args) {
    (void)thread;
    char buf[1024] = "";
    for (int i = 1; i < args->argc; i++) {
        if (i > 1) Q_strcat(buf, sizeof(buf), " ");
        Q_strcat(buf, sizeof(buf), args->argv[i]);
    }
    Com_DPrintf("%s\n", buf);
    return qtrue;
}

/* =========================================================================
 * Timing commands (handled as special flow in script_main.c,
 * but registered here as fallbacks)
 * ========================================================================= */

static qboolean Cmd_Pause(scriptThread_t *thread, scriptArgs_t *args) {
    float delay = 0.0f;
    if (args->argc > 1) delay = (float)atof(args->argv[1]);
    thread->waitTime = Script_GetCurrentTime() + delay;
    thread->state = STS_WAITING;
    return qtrue;
}

/* =========================================================================
 * Entity commands (stubs -- actual implementation in game DLL)
 *
 * The engine-side implementations log the commands. The game DLL
 * registers its own handlers that do the actual entity work.
 * These serve as documentation and fallback for testing.
 * ========================================================================= */

static qboolean Cmd_Model(scriptThread_t *thread, scriptArgs_t *args) {
    if (args->argc < 2) return qtrue;
    Com_DPrintf("Script[%d]: model %s (entity %d)\n",
               thread->id, args->argv[1], thread->entityNum);
    return qtrue;
}

static qboolean Cmd_Anim(scriptThread_t *thread, scriptArgs_t *args) {
    if (args->argc < 2) return qtrue;
    Com_DPrintf("Script[%d]: anim %s (entity %d)\n",
               thread->id, args->argv[1], thread->entityNum);
    return qtrue;
}

static qboolean Cmd_UpperAnim(scriptThread_t *thread, scriptArgs_t *args) {
    if (args->argc < 2) return qtrue;
    Com_DPrintf("Script[%d]: upperanim %s (entity %d)\n",
               thread->id, args->argv[1], thread->entityNum);
    return qtrue;
}

static qboolean Cmd_Scale(scriptThread_t *thread, scriptArgs_t *args) {
    if (args->argc < 2) return qtrue;
    Com_DPrintf("Script[%d]: scale %s (entity %d)\n",
               thread->id, args->argv[1], thread->entityNum);
    return qtrue;
}

static qboolean Cmd_Hide(scriptThread_t *thread, scriptArgs_t *args) {
    (void)args;
    Com_DPrintf("Script[%d]: hide (entity %d)\n", thread->id, thread->entityNum);
    return qtrue;
}

static qboolean Cmd_Show(scriptThread_t *thread, scriptArgs_t *args) {
    (void)args;
    Com_DPrintf("Script[%d]: show (entity %d)\n", thread->id, thread->entityNum);
    return qtrue;
}

static qboolean Cmd_NotSolid(scriptThread_t *thread, scriptArgs_t *args) {
    (void)args;
    Com_DPrintf("Script[%d]: notsolid (entity %d)\n", thread->id, thread->entityNum);
    return qtrue;
}

static qboolean Cmd_Solid(scriptThread_t *thread, scriptArgs_t *args) {
    (void)args;
    Com_DPrintf("Script[%d]: solid (entity %d)\n", thread->id, thread->entityNum);
    return qtrue;
}

/* =========================================================================
 * Movement commands
 * ========================================================================= */

static qboolean Cmd_Origin(scriptThread_t *thread, scriptArgs_t *args) {
    if (args->argc < 4) return qtrue;
    vec3_t pos;
    Script_ParseVec3(args->argv[1], args->argv[2], args->argv[3], pos);
    Com_DPrintf("Script[%d]: origin (%.1f %.1f %.1f) (entity %d)\n",
               thread->id, pos[0], pos[1], pos[2], thread->entityNum);
    return qtrue;
}

static qboolean Cmd_Angles(scriptThread_t *thread, scriptArgs_t *args) {
    if (args->argc < 4) return qtrue;
    vec3_t ang;
    Script_ParseVec3(args->argv[1], args->argv[2], args->argv[3], ang);
    Com_DPrintf("Script[%d]: angles (%.1f %.1f %.1f) (entity %d)\n",
               thread->id, ang[0], ang[1], ang[2], thread->entityNum);
    return qtrue;
}

static qboolean Cmd_Velocity(scriptThread_t *thread, scriptArgs_t *args) {
    if (args->argc < 4) return qtrue;
    vec3_t vel;
    Script_ParseVec3(args->argv[1], args->argv[2], args->argv[3], vel);
    Com_DPrintf("Script[%d]: velocity (%.1f %.1f %.1f) (entity %d)\n",
               thread->id, vel[0], vel[1], vel[2], thread->entityNum);
    return qtrue;
}

/* =========================================================================
 * Sound commands
 * ========================================================================= */

static qboolean Cmd_PlaySound(scriptThread_t *thread, scriptArgs_t *args) {
    if (args->argc < 2) return qtrue;
    Com_DPrintf("Script[%d]: playsound %s (entity %d)\n",
               thread->id, args->argv[1], thread->entityNum);
    /* TODO: S_StartSound with entity origin */
    return qtrue;
}

static qboolean Cmd_StopSound(scriptThread_t *thread, scriptArgs_t *args) {
    (void)args;
    Com_DPrintf("Script[%d]: stopsound (entity %d)\n", thread->id, thread->entityNum);
    return qtrue;
}

static qboolean Cmd_LoopSound(scriptThread_t *thread, scriptArgs_t *args) {
    if (args->argc < 2) return qtrue;
    Com_DPrintf("Script[%d]: loopsound %s (entity %d)\n",
               thread->id, args->argv[1], thread->entityNum);
    return qtrue;
}

/* =========================================================================
 * Damage / combat commands
 * ========================================================================= */

static qboolean Cmd_Damage(scriptThread_t *thread, scriptArgs_t *args) {
    if (args->argc < 2) return qtrue;
    Com_DPrintf("Script[%d]: damage %s (entity %d)\n",
               thread->id, args->argv[1], thread->entityNum);
    return qtrue;
}

static qboolean Cmd_Kill(scriptThread_t *thread, scriptArgs_t *args) {
    (void)args;
    Com_DPrintf("Script[%d]: kill (entity %d)\n", thread->id, thread->entityNum);
    return qtrue;
}

static qboolean Cmd_Remove(scriptThread_t *thread, scriptArgs_t *args) {
    (void)args;
    Com_DPrintf("Script[%d]: remove (entity %d)\n", thread->id, thread->entityNum);
    return qfalse; /* stop thread -- entity is gone */
}

/* =========================================================================
 * Surface / rendering commands
 * ========================================================================= */

static qboolean Cmd_Surface(scriptThread_t *thread, scriptArgs_t *args) {
    if (args->argc < 3) return qtrue;
    Com_DPrintf("Script[%d]: surface %s %s (entity %d)\n",
               thread->id, args->argv[1], args->argv[2], thread->entityNum);
    return qtrue;
}

static qboolean Cmd_Shader(scriptThread_t *thread, scriptArgs_t *args) {
    if (args->argc < 2) return qtrue;
    Com_DPrintf("Script[%d]: shader %s (entity %d)\n",
               thread->id, args->argv[1], thread->entityNum);
    return qtrue;
}

/* =========================================================================
 * AI commands (engine-side stubs)
 * ========================================================================= */

static qboolean Cmd_TurnTo(scriptThread_t *thread, scriptArgs_t *args) {
    if (args->argc < 2) return qtrue;
    Com_DPrintf("Script[%d]: turnto %s (entity %d)\n",
               thread->id, args->argv[1], thread->entityNum);
    return qtrue;
}

static qboolean Cmd_LookAt(scriptThread_t *thread, scriptArgs_t *args) {
    if (args->argc < 2) return qtrue;
    Com_DPrintf("Script[%d]: lookat %s (entity %d)\n",
               thread->id, args->argv[1], thread->entityNum);
    return qtrue;
}

static qboolean Cmd_RunTo(scriptThread_t *thread, scriptArgs_t *args) {
    if (args->argc < 2) return qtrue;
    Com_DPrintf("Script[%d]: runto %s (entity %d)\n",
               thread->id, args->argv[1], thread->entityNum);
    return qtrue;
}

static qboolean Cmd_WalkTo(scriptThread_t *thread, scriptArgs_t *args) {
    if (args->argc < 2) return qtrue;
    Com_DPrintf("Script[%d]: walkto %s (entity %d)\n",
               thread->id, args->argv[1], thread->entityNum);
    return qtrue;
}

/* =========================================================================
 * Camera commands
 * ========================================================================= */

static qboolean Cmd_Cam(scriptThread_t *thread, scriptArgs_t *args) {
    (void)thread;
    if (args->argc < 2) return qtrue;
    Com_DPrintf("Script[%d]: cam %s\n", thread->id, args->argv[1]);
    return qtrue;
}

static qboolean Cmd_CamFOV(scriptThread_t *thread, scriptArgs_t *args) {
    if (args->argc < 2) return qtrue;
    Com_DPrintf("Script[%d]: camfov %s\n", thread->id, args->argv[1]);
    return qtrue;
}

/* =========================================================================
 * Cinematic commands
 * ========================================================================= */

static qboolean Cmd_Letterbox(scriptThread_t *thread, scriptArgs_t *args) {
    float amount = 0.0f;
    if (args->argc > 1) amount = (float)atof(args->argv[1]);
    Com_DPrintf("Script[%d]: letterbox %.2f\n", thread->id, amount);
    return qtrue;
}

static qboolean Cmd_MusicMood(scriptThread_t *thread, scriptArgs_t *args) {
    if (args->argc < 2) return qtrue;
    Com_DPrintf("Script[%d]: musicmood %s\n", thread->id, args->argv[1]);
    return qtrue;
}

static qboolean Cmd_FadeIn(scriptThread_t *thread, scriptArgs_t *args) {
    float time = 1.0f;
    if (args->argc > 1) time = (float)atof(args->argv[1]);
    Com_DPrintf("Script[%d]: fadein %.2f\n", thread->id, time);
    return qtrue;
}

static qboolean Cmd_FadeOut(scriptThread_t *thread, scriptArgs_t *args) {
    float time = 1.0f;
    if (args->argc > 1) time = (float)atof(args->argv[1]);
    Com_DPrintf("Script[%d]: fadeout %.2f\n", thread->id, time);
    return qtrue;
}

static qboolean Cmd_Freeze(scriptThread_t *thread, scriptArgs_t *args) {
    (void)args;
    Com_DPrintf("Script[%d]: freeze (entity %d)\n", thread->id, thread->entityNum);
    return qtrue;
}

static qboolean Cmd_Unfreeze(scriptThread_t *thread, scriptArgs_t *args) {
    (void)args;
    Com_DPrintf("Script[%d]: unfreeze (entity %d)\n", thread->id, thread->entityNum);
    return qtrue;
}

/* =========================================================================
 * TIKI-related commands
 * ========================================================================= */

static qboolean Cmd_TagSpawn(scriptThread_t *thread, scriptArgs_t *args) {
    if (args->argc < 3) return qtrue;
    Com_DPrintf("Script[%d]: tagspawn %s %s (entity %d)\n",
               thread->id, args->argv[1], args->argv[2], thread->entityNum);
    return qtrue;
}

static qboolean Cmd_TagAttach(scriptThread_t *thread, scriptArgs_t *args) {
    if (args->argc < 3) return qtrue;
    Com_DPrintf("Script[%d]: tagattach %s %s (entity %d)\n",
               thread->id, args->argv[1], args->argv[2], thread->entityNum);
    return qtrue;
}

static qboolean Cmd_TagDetach(scriptThread_t *thread, scriptArgs_t *args) {
    (void)args;
    Com_DPrintf("Script[%d]: tagdetach (entity %d)\n", thread->id, thread->entityNum);
    return qtrue;
}

/* =========================================================================
 * Trigger / event commands
 * ========================================================================= */

static qboolean Cmd_Trigger(scriptThread_t *thread, scriptArgs_t *args) {
    if (args->argc < 2) return qtrue;
    Com_DPrintf("Script[%d]: trigger %s (entity %d)\n",
               thread->id, args->argv[1], thread->entityNum);
    return qtrue;
}

static qboolean Cmd_Use(scriptThread_t *thread, scriptArgs_t *args) {
    if (args->argc < 2) return qtrue;
    Com_DPrintf("Script[%d]: use %s (entity %d)\n",
               thread->id, args->argv[1], thread->entityNum);
    return qtrue;
}

/* =========================================================================
 * Physics / property commands
 * ========================================================================= */

static qboolean Cmd_Gravity(scriptThread_t *thread, scriptArgs_t *args) {
    if (args->argc < 2) return qtrue;
    Com_DPrintf("Script[%d]: gravity %s (entity %d)\n",
               thread->id, args->argv[1], thread->entityNum);
    return qtrue;
}

static qboolean Cmd_Mass(scriptThread_t *thread, scriptArgs_t *args) {
    if (args->argc < 2) return qtrue;
    Com_DPrintf("Script[%d]: mass %s (entity %d)\n",
               thread->id, args->argv[1], thread->entityNum);
    return qtrue;
}

static qboolean Cmd_Health(scriptThread_t *thread, scriptArgs_t *args) {
    if (args->argc < 2) return qtrue;
    Com_DPrintf("Script[%d]: health %s (entity %d)\n",
               thread->id, args->argv[1], thread->entityNum);
    return qtrue;
}

/* =========================================================================
 * Registration
 *
 * Called from Script_Init to register all engine-side commands.
 * The game DLL registers its own commands via Script_RegisterCommand
 * after loading.
 * ========================================================================= */

void Script_RegisterBuiltins(void) {
    /* Debug / Print */
    Script_RegisterCommand("print",       Cmd_Print,      1, "print <text...>");
    Script_RegisterCommand("println",     Cmd_Print,      1, "println <text...>");
    Script_RegisterCommand("dprintln",    Cmd_DPrintln,   1, "dprintln <text...>");

    /* Timing */
    Script_RegisterCommand("pause",       Cmd_Pause,      1, "pause <seconds>");

    /* Entity */
    Script_RegisterCommand("model",       Cmd_Model,      1, "model <modelname>");
    Script_RegisterCommand("anim",        Cmd_Anim,       1, "anim <animname>");
    Script_RegisterCommand("upperanim",   Cmd_UpperAnim,  1, "upperanim <animname>");
    Script_RegisterCommand("scale",       Cmd_Scale,      1, "scale <factor>");
    Script_RegisterCommand("hide",        Cmd_Hide,       0, "hide");
    Script_RegisterCommand("show",        Cmd_Show,       0, "show");
    Script_RegisterCommand("notsolid",    Cmd_NotSolid,   0, "notsolid");
    Script_RegisterCommand("solid",       Cmd_Solid,      0, "solid");

    /* Movement */
    Script_RegisterCommand("origin",      Cmd_Origin,     3, "origin <x> <y> <z>");
    Script_RegisterCommand("angles",      Cmd_Angles,     3, "angles <p> <y> <r>");
    Script_RegisterCommand("velocity",    Cmd_Velocity,   3, "velocity <x> <y> <z>");

    /* Sound */
    Script_RegisterCommand("playsound",   Cmd_PlaySound,  1, "playsound <name>");
    Script_RegisterCommand("stopsound",   Cmd_StopSound,  0, "stopsound");
    Script_RegisterCommand("loopsound",   Cmd_LoopSound,  1, "loopsound <name>");

    /* Damage / Combat */
    Script_RegisterCommand("damage",      Cmd_Damage,     1, "damage <amount>");
    Script_RegisterCommand("kill",        Cmd_Kill,       0, "kill");
    Script_RegisterCommand("remove",      Cmd_Remove,     0, "remove");

    /* Surface / Rendering */
    Script_RegisterCommand("surface",     Cmd_Surface,    2, "surface <name> <flags>");
    Script_RegisterCommand("shader",      Cmd_Shader,     1, "shader <name>");

    /* AI */
    Script_RegisterCommand("turnto",      Cmd_TurnTo,     1, "turnto <target>");
    Script_RegisterCommand("lookat",      Cmd_LookAt,     1, "lookat <target>");
    Script_RegisterCommand("runto",       Cmd_RunTo,      1, "runto <target>");
    Script_RegisterCommand("walkto",      Cmd_WalkTo,     1, "walkto <target>");

    /* Camera */
    Script_RegisterCommand("cam",         Cmd_Cam,        1, "cam <mode>");
    Script_RegisterCommand("camfov",      Cmd_CamFOV,     1, "camfov <fov>");

    /* Cinematic */
    Script_RegisterCommand("letterbox",   Cmd_Letterbox,  1, "letterbox <amount>");
    Script_RegisterCommand("musicmood",   Cmd_MusicMood,  1, "musicmood <mood>");
    Script_RegisterCommand("fadein",      Cmd_FadeIn,     1, "fadein <time>");
    Script_RegisterCommand("fadeout",     Cmd_FadeOut,    1, "fadeout <time>");
    Script_RegisterCommand("freeze",      Cmd_Freeze,     0, "freeze");
    Script_RegisterCommand("unfreeze",    Cmd_Unfreeze,   0, "unfreeze");

    /* TIKI tag operations */
    Script_RegisterCommand("tagspawn",    Cmd_TagSpawn,   2, "tagspawn <model> <tag>");
    Script_RegisterCommand("tagattach",   Cmd_TagAttach,  2, "tagattach <target> <tag>");
    Script_RegisterCommand("tagdetach",   Cmd_TagDetach,  0, "tagdetach");

    /* Trigger / Event */
    Script_RegisterCommand("trigger",     Cmd_Trigger,    1, "trigger <target>");
    Script_RegisterCommand("use",         Cmd_Use,        1, "use <target>");

    /* Physics / Properties */
    Script_RegisterCommand("gravity",     Cmd_Gravity,    1, "gravity <value>");
    Script_RegisterCommand("mass",        Cmd_Mass,       1, "mass <value>");
    Script_RegisterCommand("health",      Cmd_Health,     1, "health <value>");

    Com_Printf("Script: %d built-in commands registered\n", 40);
}
