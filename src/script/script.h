/*
 * script.h -- Morpheus scripting engine
 *
 * Morpheus is Ritual's scripting language, evolved from SiN's system.
 * It provides ~700 commands for controlling game entities, cameras,
 * AI behaviors, and cinematic sequences.
 *
 * Key capabilities:
 *   - Entity manipulation (move, rotate, animate, damage)
 *   - AI behavior control (path, state, target)
 *   - Camera scripting (spline paths, FOV, tracking)
 *   - Cinematic sequences (Babble lip-sync integration)
 *   - Trigger/event responses
 *   - Variable/conditional logic
 *
 * Scripts are embedded in:
 *   - TIKI files (init blocks, frame events)
 *   - BSP entities (trigger scripts)
 *   - Standalone script files
 *   - Console commands
 */

#ifndef SCRIPT_H
#define SCRIPT_H

#include "../common/fakk_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Script system lifecycle */
void    Script_Init(void);
void    Script_Shutdown(void);

/* Script execution */
void    Script_Execute(const char *script_text);
void    Script_ExecuteFile(const char *filename);

/* Script command registration */
typedef void (*script_cmd_func_t)(void);
void    Script_RegisterCommand(const char *name, script_cmd_func_t func);

#ifdef __cplusplus
}
#endif

#endif /* SCRIPT_H */
