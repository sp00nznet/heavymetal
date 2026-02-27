/*
 * cl_cgame.c -- Client game module interface
 *
 * Loads and communicates with cgamex86.dll (or built-in cgame module).
 *
 * Interface: GetCGameAPI() returns clientGameExport_t*
 *   - CG_Init
 *   - CG_Shutdown
 *   - CG_DrawActiveFrame
 *   - CG_ConsoleCommand
 *   - CG_GetRendererConfig
 *   - CG_Draw2D
 *
 * The engine provides clientGameImport_t with ~90+ function pointers.
 */

#include "../common/qcommon.h"

/* TODO: Implement cgame module loading and interface */
