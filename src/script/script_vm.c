/*
 * script_vm.c -- Morpheus script virtual machine
 *
 * Executes parsed Morpheus script commands. The VM processes
 * commands sequentially with support for:
 *   - Wait/delay between commands
 *   - Conditional branching
 *   - Entity targeting
 *   - Thread management (multiple scripts running concurrently)
 */

#include "script.h"
#include "../common/qcommon.h"

/* TODO: Implement Morpheus VM
 *
 * The script VM needs to:
 * 1. Parse script text into command tokens
 * 2. Match commands against the ~700 registered command handlers
 * 3. Execute commands with proper entity context
 * 4. Handle wait/delay for timed sequences
 * 5. Support multiple concurrent script threads
 * 6. Integrate with TIKI for animation-triggered scripts
 */
