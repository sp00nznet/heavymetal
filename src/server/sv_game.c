/*
 * sv_game.c -- Server-side game module interface
 *
 * Loads and communicates with gamex86.dll (or built-in game module).
 *
 * Interface: GetGameAPI(game_import_t*) returns game_export_t*
 *   game_export_t callbacks:
 *     - Init, Shutdown, Cleanup
 *     - SpawnEntities
 *     - ClientConnect, ClientBegin, ClientThink, ClientCommand, ClientDisconnect
 *     - RunFrame, PrepFrame
 *     - ConsoleCommand
 *     - WriteSaveGame, ReadSaveGame
 *
 * game_import_t provides ~60+ engine functions to the game module:
 *     - Printf, Error, Malloc/Free
 *     - Cvar, FS (filesystem)
 *     - Trace, PointContents
 *     - LinkEntity, UnlinkEntity
 *     - Model/anim/tag/surface queries (TIKI integration)
 *     - Sound playback
 *     - Configstrings
 *     - Alias system
 */

#include "../common/qcommon.h"

/* TODO: Implement game module loading and interface */
