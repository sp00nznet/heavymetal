/*
 * main.c -- Entry point for the FAKK2 recomp
 *
 * Original: fakk2.exe entry point at 0x004C4C73
 * Compiled: 2000-08-22 16:14:06 UTC, MSVC 6.0
 *
 * The original WinMain creates the game window, initializes DirectInput,
 * sets up the OpenGL context, and enters the main game loop.
 * In the recomp, SDL2 handles all platform-specific initialization.
 */

#include "qcommon.h"
#include "win32_compat.h"
#include <stdio.h>

/* =========================================================================
 * Global engine state
 * ========================================================================= */

static qboolean fakk_running = qtrue;

/* =========================================================================
 * Main entry point
 *
 * Original was WinMain (Win32 GUI subsystem).
 * Recomp uses standard main() with SDL2.
 * ========================================================================= */

int main(int argc, char **argv) {
    int     old_time, new_time, msec;

    Com_Printf("-------------------------------------------\n");
    Com_Printf(" Heavy Metal: FAKK2 v%s (recomp)\n", FAKK_VERSION);
    Com_Printf(" \"One girl. One sword. One hell of a ride.\"\n");
    Com_Printf("-------------------------------------------\n");
    Com_Printf("\n");

    /* Initialize all engine subsystems */
    Com_Init(argc, argv);

    old_time = Sys_Milliseconds();

    /* ---- Main game loop ---- */
    while (fakk_running) {
        /* Calculate frame time */
        new_time = Sys_Milliseconds();
        msec = new_time - old_time;
        old_time = new_time;

        /* Clamp frame time to prevent spiral of death */
        if (msec < 1) msec = 1;
        if (msec > 200) msec = 200;

        /* Process one frame */
        Com_Frame();
    }

    Com_Shutdown();
    return 0;
}
