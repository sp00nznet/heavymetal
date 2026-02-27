/*
 * cl_main.c -- Client main loop
 *
 * The client handles rendering, input, sound, and communication
 * with the server. In FAKK2, the client is always running (no
 * dedicated server mode was common for this single-player focused game).
 */

#include "../common/qcommon.h"
#include "../renderer/tr_types.h"
#include "../sound/snd_local.h"

/* Forward declarations from renderer */
extern void R_Init(void);
extern void R_Shutdown(void);
extern void R_BeginFrame(void);
extern void R_EndFrame(void);

void CL_Init(void) {
    Com_Printf("--- CL_Init ---\n");

    /* Initialize renderer */
    R_Init();

    /* Initialize sound */
    S_Init();

    /* Initialize input */
    IN_Init();

    /* Create window */
    /* TODO: Read r_mode, r_fullscreen cvars */
    Win_Create(1024, 768, qfalse);

    Com_Printf("Client initialized\n");
}

void CL_Shutdown(void) {
    Com_Printf("--- CL_Shutdown ---\n");
    IN_Shutdown();
    S_Shutdown();
    R_Shutdown();
    Win_Destroy();
}

void CL_Frame(int msec) {
    (void)msec;

    /* Process input */
    IN_Frame();

    /* Process window events */
    Win_ProcessEvents();

    /* Update sound */
    S_Update();

    /* TODO: Send commands to server */
    /* TODO: Process snapshots from server */
    /* TODO: Render scene */

    R_BeginFrame();
    /* TODO: R_RenderScene() */
    R_EndFrame();
}
