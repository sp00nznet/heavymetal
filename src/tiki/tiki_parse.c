/*
 * tiki_parse.c -- TIKI file parser
 *
 * Parses .tik text files into tiki_model_t structures.
 *
 * Example .tik file format (from FAKK2 SDK documentation):
 *
 *   TIKI
 *   setup
 *   {
 *       scale 1.0
 *       path models/julie
 *       skelmodel julie.skl
 *       surface body shader julie_body
 *       surface head shader julie_head
 *   }
 *   init
 *   {
 *       server
 *       {
 *           classname Player
 *       }
 *   }
 *   animations
 *   {
 *       idle  julie_idle.anm
 *       walk  julie_walk.anm
 *       {
 *           client
 *           {
 *               10 sound footstep
 *               25 sound footstep
 *           }
 *       }
 *   }
 */

#include "tiki.h"
#include "../common/qcommon.h"
#include <string.h>

tiki_model_t *TIKI_ParseFile(const char *filename) {
    void *buffer;
    long len;

    Com_DPrintf("TIKI_ParseFile: %s\n", filename);

    len = FS_ReadFile(filename, &buffer);
    if (len < 0 || !buffer) {
        Com_Printf("TIKI_ParseFile: couldn't load %s\n", filename);
        return NULL;
    }

    /* TODO: Parse the TIKI text format */
    /*
     * Implementation plan:
     * 1. Tokenize with COM_Parse
     * 2. Expect "TIKI" header token
     * 3. Parse "setup" block for model properties
     * 4. Parse "init" block for entity initialization
     * 5. Parse "animations" block for animation definitions
     * 6. For each animation, parse frame commands in client/server blocks
     * 7. Build and return tiki_model_t
     */

    FS_FreeFile(buffer);
    return NULL;
}
