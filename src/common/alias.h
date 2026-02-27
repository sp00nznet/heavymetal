/*
 * alias.h -- FAKK2 alias system
 *
 * The alias system maps logical names to actual resource names,
 * supporting random selection from multiple candidates. Used for:
 *   - Sound aliases (footstep_dirt -> dirt1.wav, dirt2.wav, dirt3.wav)
 *   - Animation aliases
 *   - Dialogue aliases with play tracking
 *
 * Two scopes:
 *   - Per-model aliases (indexed by TIKI model handle)
 *   - Global aliases (shared across all models)
 */

#ifndef ALIAS_H
#define ALIAS_H

#include "fakk_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Global alias API (not tied to a specific model)
 * ========================================================================= */

void        Alias_Init(void);
void        Alias_Shutdown(void);

qboolean    Alias_GlobalAdd(const char *alias, const char *name, const char *parameters);
const char  *Alias_GlobalFindRandom(const char *alias);
void        Alias_GlobalDump(void);
void        Alias_GlobalClear(void);

/* =========================================================================
 * Per-model alias API (indexed by TIKI model handle)
 * ========================================================================= */

qboolean    Alias_ModelAdd(int modelindex, const char *alias, const char *name, const char *parameters);
const char  *Alias_ModelFindRandom(int modelindex, const char *alias);
void        Alias_ModelDump(int modelindex);
void        Alias_ModelClear(int modelindex);
const char  *Alias_ModelFindDialog(int modelindex, const char *alias, int random, int entity_number);
void        *Alias_ModelGetList(int modelindex);
void        Alias_ModelUpdateDialog(int modelindex, const char *alias, int number_of_times_played,
                                    byte been_played_this_loop, int last_time_played);
void        Alias_ModelAddActorDialog(int modelindex, const char *alias, int actor_number,
                                      int number_of_times_played, byte been_played_this_loop,
                                      int last_time_played);

#ifdef __cplusplus
}
#endif

#endif /* ALIAS_H */
