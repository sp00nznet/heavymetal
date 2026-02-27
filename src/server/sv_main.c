/*
 * sv_main.c -- Server main loop
 *
 * FAKK2's server runs the game simulation, entity logic, AI,
 * physics, and Morpheus script execution. Even in single-player,
 * the local server runs and communicates with the client via
 * loopback messages.
 */

#include "../common/qcommon.h"

void SV_Init(void) {
    Com_Printf("--- SV_Init ---\n");
    Com_Printf("Server initialized\n");
}

void SV_Shutdown(const char *finalmsg) {
    if (finalmsg && finalmsg[0]) {
        Com_Printf("Server shutdown: %s\n", finalmsg);
    }
}

void SV_Frame(int msec) {
    (void)msec;
    /* TODO: Run server frame
     *   1. Process client messages
     *   2. Execute pending commands
     *   3. Run game frame (call game module RunFrame)
     *   4. Send snapshots to clients
     */
}
