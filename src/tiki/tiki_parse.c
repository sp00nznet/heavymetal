/*
 * tiki_parse.c -- TIKI text file parser
 *
 * Parses .tik files into tiki_model_t structures. Handles:
 *   - $include directive (recursive file inclusion/inheritance)
 *   - $define directive (macro substitution as $name$ in tokens)
 *   - setup {} block (skelmodel, surfaces, scale, path, LOD)
 *   - init {} block with server {} and client {} subsections
 *   - animations {} block with per-frame server/client commands
 *
 * Uses COM_Parse() for tokenization (handles // and comments, quoted strings).
 * Variable expansion applied to every token after reading.
 *
 * Real TIKI examples from pak0.pk3 (772 files):
 *
 *   TIKI
 *   $include models/julie_base.tik
 *   $define birddir sound/monsters/bird
 *   setup {
 *       scale 1.5
 *       path models/animal/bird
 *       skelmodel julie_base.skb
 *       surface body shader julie_body
 *       surface holster flags nodraw
 *   }
 *   init {
 *       server {
 *           classname Actor
 *           setsize "-15 -15 0" "15 15 30"
 *           health 100
 *       }
 *       client {
 *           cache sound/player/footstep.wav
 *       }
 *   }
 *   animations {
 *       idle bird_idle.tan
 *       walk walk_norm.ska {
 *           server {
 *               4 soundevent 500
 *           }
 *           client {
 *               4 footstep .6
 *               10 footstep .6
 *           }
 *       }
 *   }
 */

#include "tiki.h"
#include "../common/qcommon.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/* =========================================================================
 * $define macro table
 * ========================================================================= */

typedef struct {
    char name[64];
    char value[MAX_QPATH];
} tiki_define_t;

static tiki_define_t    s_defines[MAX_TIKI_DEFINES];
static int              s_num_defines;

static void TIKI_ClearDefines(void) {
    s_num_defines = 0;
}

static void TIKI_AddDefine(const char *name, const char *value) {
    if (s_num_defines >= MAX_TIKI_DEFINES) {
        Com_Printf("TIKI_AddDefine: MAX_TIKI_DEFINES reached\n");
        return;
    }
    Q_strncpyz(s_defines[s_num_defines].name, name, sizeof(s_defines[0].name));
    Q_strncpyz(s_defines[s_num_defines].value, value, sizeof(s_defines[0].value));
    s_num_defines++;
}

static const char *TIKI_FindDefine(const char *name) {
    for (int i = 0; i < s_num_defines; i++) {
        if (!Q_stricmp(s_defines[i].name, name))
            return s_defines[i].value;
    }
    return NULL;
}

/* =========================================================================
 * Variable expansion: replace $name$ patterns with define values
 *
 * Example: "$birddir$/flap3.wav" with birddir="sound/monsters/bird"
 *       -> "sound/monsters/bird/flap3.wav"
 * ========================================================================= */

static void TIKI_ExpandDefines(const char *in, char *out, int outsize) {
    const char *p = in;
    char *dst = out;
    char *end = out + outsize - 1;

    while (*p && dst < end) {
        if (*p == '$') {
            /* Look for closing $ */
            const char *start = p + 1;
            const char *close = strchr(start, '$');
            if (close && close > start) {
                /* Extract variable name */
                char varname[64];
                int namelen = (int)(close - start);
                if (namelen >= (int)sizeof(varname))
                    namelen = (int)sizeof(varname) - 1;
                memcpy(varname, start, namelen);
                varname[namelen] = '\0';

                const char *value = TIKI_FindDefine(varname);
                if (value) {
                    /* Substitute */
                    int vlen = (int)strlen(value);
                    if (dst + vlen < end) {
                        memcpy(dst, value, vlen);
                        dst += vlen;
                    }
                    p = close + 1;
                    continue;
                }
            }
            /* No match -- copy the $ literally */
            *dst++ = *p++;
        } else {
            *dst++ = *p++;
        }
    }
    *dst = '\0';
}

/* =========================================================================
 * Token helper -- COM_Parse + variable expansion
 * ========================================================================= */

static char s_expanded[MAX_TOKEN_CHARS];

static char *TIKI_Token(char **data) {
    char *tok = COM_Parse(data);
    if (tok[0]) {
        TIKI_ExpandDefines(tok, s_expanded, sizeof(s_expanded));
        return s_expanded;
    }
    return tok;
}

/* Peek at next token without consuming it (save/restore parse pointer) */
static const char *TIKI_PeekToken(char **data) {
    char *save = *data;
    char *tok = TIKI_Token(data);
    *data = save;
    return tok;
}

/* Skip a brace-delimited block (including nested blocks) */
static void TIKI_SkipBlock(char **data) {
    int depth = 1;
    char *tok;
    while (depth > 0) {
        tok = COM_Parse(data);
        if (!tok[0]) return;
        if (!strcmp(tok, "{")) depth++;
        else if (!strcmp(tok, "}")) depth--;
    }
}

/* Duplicate a string using engine allocator */
static char *TIKI_CopyString(const char *s) {
    int len = (int)strlen(s) + 1;
    char *out = (char *)Z_Malloc(len);
    memcpy(out, s, len);
    return out;
}

/* =========================================================================
 * Parse surface flags from token
 * ========================================================================= */

static int TIKI_ParseSurfaceFlags(const char *token) {
    if (!Q_stricmp(token, "nodraw"))    return MDL_SURFACE_NODRAW;
    if (!Q_stricmp(token, "nomipmaps")) return MDL_SURFACE_NOMIPMAPS;
    if (!Q_stricmp(token, "nopicmip"))  return MDL_SURFACE_NOPICMIP;
    if (!Q_stricmp(token, "crossfade")) return MDL_SURFACE_CROSSFADE_SKINS;
    return 0;
}

/* =========================================================================
 * Parse frame number from token (integer or special keyword)
 * ========================================================================= */

static int TIKI_ParseFrameNum(const char *token) {
    if (!Q_stricmp(token, "first") || !Q_stricmp(token, "enter"))
        return TIKI_FRAME_ENTRY;
    if (!Q_stricmp(token, "last"))
        return TIKI_FRAME_LAST;
    if (!Q_stricmp(token, "entry"))
        return TIKI_FRAME_ENTRY;
    if (!Q_stricmp(token, "exit"))
        return TIKI_FRAME_EXIT;
    if (!Q_stricmp(token, "every"))
        return TIKI_FRAME_EVERY;
    return atoi(token);
}

/* =========================================================================
 * Parse setup {} block
 * ========================================================================= */

static qboolean TIKI_ParseSetup(char **data, tiki_model_t *model) {
    char *tok;

    tok = TIKI_Token(data);
    if (strcmp(tok, "{")) {
        Com_Printf("TIKI_ParseSetup: expected '{', got '%s'\n", tok);
        return qfalse;
    }

    while (1) {
        tok = TIKI_Token(data);
        if (!tok[0]) return qfalse;
        if (!strcmp(tok, "}")) break;

        if (!Q_stricmp(tok, "scale")) {
            tok = TIKI_Token(data);
            model->scale = (float)atof(tok);
        }
        else if (!Q_stricmp(tok, "lod_scale")) {
            tok = TIKI_Token(data);
            model->lod_scale = (float)atof(tok);
        }
        else if (!Q_stricmp(tok, "lod_bias")) {
            tok = TIKI_Token(data);
            model->lod_bias = (float)atof(tok);
        }
        else if (!Q_stricmp(tok, "path")) {
            tok = TIKI_Token(data);
            Q_strncpyz(model->path, tok, sizeof(model->path));
        }
        else if (!Q_stricmp(tok, "skelmodel")) {
            tok = TIKI_Token(data);
            /* Prepend path if set and filename is relative */
            if (model->path[0] && tok[0] != '/') {
                char fullpath[MAX_QPATH];
                snprintf(fullpath, sizeof(fullpath), "%s/%s", model->path, tok);
                Q_strncpyz(model->skelmodel, fullpath, sizeof(model->skelmodel));
            } else {
                Q_strncpyz(model->skelmodel, tok, sizeof(model->skelmodel));
            }
            model->is_character = qtrue;
        }
        else if (!Q_stricmp(tok, "radius")) {
            tok = TIKI_Token(data);
            model->radius = (float)atof(tok);
        }
        else if (!Q_stricmp(tok, "surface")) {
            /* surface <name> shader|flags <value> */
            char surfname[MAX_QPATH];
            tok = TIKI_Token(data);
            Q_strncpyz(surfname, tok, sizeof(surfname));

            tok = TIKI_Token(data);
            if (!Q_stricmp(tok, "shader")) {
                tok = TIKI_Token(data);
                /* Find or create surface entry */
                int idx = -1;
                for (int i = 0; i < model->num_surfaces; i++) {
                    if (!Q_stricmp(model->surfaces[i].name, surfname)) {
                        idx = i;
                        break;
                    }
                }
                if (idx < 0 && model->num_surfaces < TIKI_MAX_SURFACES) {
                    idx = model->num_surfaces++;
                    Q_strncpyz(model->surfaces[idx].name, surfname, MAX_QPATH);
                }
                if (idx >= 0) {
                    Q_strncpyz(model->surfaces[idx].shader, tok, MAX_QPATH);
                }
            }
            else if (!Q_stricmp(tok, "flags")) {
                tok = TIKI_Token(data);
                int idx = -1;
                for (int i = 0; i < model->num_surfaces; i++) {
                    if (!Q_stricmp(model->surfaces[i].name, surfname)) {
                        idx = i;
                        break;
                    }
                }
                if (idx < 0 && model->num_surfaces < TIKI_MAX_SURFACES) {
                    idx = model->num_surfaces++;
                    Q_strncpyz(model->surfaces[idx].name, surfname, MAX_QPATH);
                }
                if (idx >= 0) {
                    model->surfaces[idx].flags |= TIKI_ParseSurfaceFlags(tok);
                }
            }
        }
        else if (!Q_stricmp(tok, "light_offset")) {
            /* light_offset "x y z" or three separate tokens */
            tok = TIKI_Token(data);
            if (sscanf(tok, "%f %f %f", &model->light_offset[0],
                        &model->light_offset[1], &model->light_offset[2]) < 3) {
                model->light_offset[0] = (float)atof(tok);
                tok = TIKI_Token(data);
                model->light_offset[1] = (float)atof(tok);
                tok = TIKI_Token(data);
                model->light_offset[2] = (float)atof(tok);
            }
        }
        else if (!Q_stricmp(tok, "load_origin")) {
            tok = TIKI_Token(data);
            if (sscanf(tok, "%f %f %f", &model->load_origin[0],
                        &model->load_origin[1], &model->load_origin[2]) < 3) {
                model->load_origin[0] = (float)atof(tok);
                tok = TIKI_Token(data);
                model->load_origin[1] = (float)atof(tok);
                tok = TIKI_Token(data);
                model->load_origin[2] = (float)atof(tok);
            }
        }
        else {
            Com_DPrintf("TIKI_ParseSetup: unknown keyword '%s'\n", tok);
        }
    }

    return qtrue;
}

/* =========================================================================
 * Parse init command list (server or client subsection)
 * ========================================================================= */

static qboolean TIKI_ParseInitSection(char **data, tiki_initcmd_t **out_cmds, int *out_num) {
    char *tok;
    tiki_initcmd_t cmds[MAX_TIKI_INITCMDS];
    int count = 0;

    tok = TIKI_Token(data);
    if (strcmp(tok, "{")) {
        Com_Printf("TIKI_ParseInitSection: expected '{'\n");
        return qfalse;
    }

    while (count < MAX_TIKI_INITCMDS) {
        /* First token of each command -- COM_Parse skips newlines */
        tok = TIKI_Token(data);
        if (!tok[0]) break;
        if (!strcmp(tok, "}")) break;

        tiki_initcmd_t *cmd = &cmds[count];
        memset(cmd, 0, sizeof(*cmd));
        cmd->args[0] = TIKI_CopyString(tok);
        cmd->num_args = 1;

        /* Read remaining args on same line (COM_ParseExt stops at newline) */
        while (cmd->num_args < TIKI_CMD_MAX_ARGS) {
            tok = COM_ParseExt(data, qfalse);
            if (!tok[0]) break;     /* hit newline -- command complete */
            TIKI_ExpandDefines(tok, s_expanded, sizeof(s_expanded));
            cmd->args[cmd->num_args++] = TIKI_CopyString(s_expanded);
        }

        count++;
    }

    if (count > 0) {
        *out_cmds = (tiki_initcmd_t *)Z_Malloc(count * sizeof(tiki_initcmd_t));
        memcpy(*out_cmds, cmds, count * sizeof(tiki_initcmd_t));
    }
    *out_num = count;
    return qtrue;
}

/* =========================================================================
 * Parse init {} block (contains server {} and client {} subsections)
 * ========================================================================= */

static qboolean TIKI_ParseInit(char **data, tiki_model_t *model) {
    char *tok;

    tok = TIKI_Token(data);
    if (strcmp(tok, "{")) {
        Com_Printf("TIKI_ParseInit: expected '{'\n");
        return qfalse;
    }

    while (1) {
        tok = TIKI_Token(data);
        if (!tok[0]) return qfalse;
        if (!strcmp(tok, "}")) break;

        if (!Q_stricmp(tok, "server")) {
            TIKI_ParseInitSection(data, &model->server_initcmds,
                                  &model->num_server_initcmds);
        }
        else if (!Q_stricmp(tok, "client")) {
            TIKI_ParseInitSection(data, &model->client_initcmds,
                                  &model->num_client_initcmds);
        }
        else {
            Com_DPrintf("TIKI_ParseInit: unknown section '%s'\n", tok);
            /* Try to skip a block if there is one */
            const char *peek = TIKI_PeekToken(data);
            if (!strcmp(peek, "{")) {
                TIKI_Token(data);   /* consume '{' */
                TIKI_SkipBlock(data);
            }
        }
    }

    return qtrue;
}

/* =========================================================================
 * Parse frame commands inside an animation's server {} or client {} block
 *
 * Format: <frame_num|first|last|entry|exit> <command> [args...]
 * Each line is one command. Frame number comes first.
 * ========================================================================= */

static int TIKI_ParseFrameCmds(char **data, tiki_frame_cmd_t *cmds, int maxcmds) {
    char *tok;
    int count = 0;

    tok = TIKI_Token(data);
    if (strcmp(tok, "{")) return 0;

    while (1) {
        tok = TIKI_Token(data);
        if (!tok[0]) return count;
        if (!strcmp(tok, "}")) return count;

        if (count >= maxcmds) {
            TIKI_SkipBlock(data);
            return count;
        }

        tiki_frame_cmd_t *cmd = &cmds[count];
        memset(cmd, 0, sizeof(*cmd));

        /* First token is frame number or special keyword */
        cmd->frame_num = TIKI_ParseFrameNum(tok);

        /* Remaining tokens on this line are command name + args */
        while (cmd->num_args < TIKI_CMD_MAX_ARGS) {
            char *save = *data;
            tok = COM_ParseExt(data, qfalse);
            if (!tok[0]) {
                /* Hit line break -- command is complete */
                break;
            }
            TIKI_ExpandDefines(tok, s_expanded, sizeof(s_expanded));
            cmd->args[cmd->num_args++] = TIKI_CopyString(s_expanded);
        }

        if (cmd->num_args > 0) {
            count++;
        }
    }
}

/* =========================================================================
 * Parse animations {} block
 *
 * Format:
 *   animations {
 *       <alias> <filename.ska|.tan> [{ server { cmds } client { cmds } }]
 *       ...
 *   }
 * ========================================================================= */

/* Temporary storage for building animation list */
#define MAX_PARSE_ANIMS     512
#define MAX_PARSE_FRAMECMDS 128

static qboolean TIKI_ParseAnimations(char **data, tiki_model_t *model) {
    char *tok;
    tiki_animdef_t anims[MAX_PARSE_ANIMS];
    int count = 0;

    tok = TIKI_Token(data);
    if (strcmp(tok, "{")) {
        Com_Printf("TIKI_ParseAnimations: expected '{'\n");
        return qfalse;
    }

    while (1) {
        tok = TIKI_Token(data);
        if (!tok[0]) break;
        if (!strcmp(tok, "}")) break;

        if (count >= MAX_PARSE_ANIMS) {
            Com_Printf("TIKI_ParseAnimations: MAX_PARSE_ANIMS reached\n");
            TIKI_SkipBlock(data);
            break;
        }

        tiki_animdef_t *anim = &anims[count];
        memset(anim, 0, sizeof(*anim));
        anim->weight = 1.0f;

        /* Animation alias (name) */
        Q_strncpyz(anim->alias, tok, sizeof(anim->alias));

        /* Animation filename */
        tok = TIKI_Token(data);
        if (model->path[0] && tok[0] != '/') {
            snprintf(anim->filename, sizeof(anim->filename), "%s/%s",
                     model->path, tok);
        } else {
            Q_strncpyz(anim->filename, tok, sizeof(anim->filename));
        }

        /* Check for optional command block */
        const char *peek = TIKI_PeekToken(data);
        if (!strcmp(peek, "{")) {
            TIKI_Token(data);   /* consume '{' */

            while (1) {
                tok = TIKI_Token(data);
                if (!tok[0]) break;
                if (!strcmp(tok, "}")) break;

                if (!Q_stricmp(tok, "server")) {
                    tiki_frame_cmd_t framecmds[MAX_PARSE_FRAMECMDS];
                    int ncmds = TIKI_ParseFrameCmds(data, framecmds,
                                                     MAX_PARSE_FRAMECMDS);
                    if (ncmds > 0) {
                        anim->num_server_cmds = ncmds;
                        anim->server_cmds = (tiki_frame_cmd_t *)Z_Malloc(
                            ncmds * sizeof(tiki_frame_cmd_t));
                        memcpy(anim->server_cmds, framecmds,
                               ncmds * sizeof(tiki_frame_cmd_t));
                    }
                }
                else if (!Q_stricmp(tok, "client")) {
                    tiki_frame_cmd_t framecmds[MAX_PARSE_FRAMECMDS];
                    int ncmds = TIKI_ParseFrameCmds(data, framecmds,
                                                     MAX_PARSE_FRAMECMDS);
                    if (ncmds > 0) {
                        anim->num_client_cmds = ncmds;
                        anim->client_cmds = (tiki_frame_cmd_t *)Z_Malloc(
                            ncmds * sizeof(tiki_frame_cmd_t));
                        memcpy(anim->client_cmds, framecmds,
                               ncmds * sizeof(tiki_frame_cmd_t));
                    }
                }
                else if (!Q_stricmp(tok, "weight")) {
                    tok = TIKI_Token(data);
                    anim->weight = (float)atof(tok);
                }
                else if (!Q_stricmp(tok, "blendtime") ||
                         !Q_stricmp(tok, "crossblend")) {
                    tok = TIKI_Token(data);
                    anim->blendtime = atoi(tok);
                }
                else if (!Q_stricmp(tok, "deltadriven")) {
                    anim->flags |= MDL_ANIM_DELTA_DRIVEN;
                }
                else if (!Q_stricmp(tok, "default_angles")) {
                    anim->flags |= MDL_ANIM_DEFAULT_ANGLES;
                }
                else {
                    Com_DPrintf("TIKI_ParseAnimations: unknown anim keyword '%s'\n", tok);
                }
            }
        }

        count++;
    }

    if (count > 0) {
        /* Merge with existing animations (from $include) */
        if (model->num_anims > 0 && model->anims) {
            int total = model->num_anims + count;
            tiki_animdef_t *merged = (tiki_animdef_t *)Z_Malloc(
                total * sizeof(tiki_animdef_t));
            memcpy(merged, model->anims, model->num_anims * sizeof(tiki_animdef_t));
            memcpy(merged + model->num_anims, anims,
                   count * sizeof(tiki_animdef_t));
            Z_Free(model->anims);
            model->anims = merged;
            model->num_anims = total;
        } else {
            model->anims = (tiki_animdef_t *)Z_Malloc(
                count * sizeof(tiki_animdef_t));
            memcpy(model->anims, anims, count * sizeof(tiki_animdef_t));
            model->num_anims = count;
        }
    }

    return qtrue;
}

/* =========================================================================
 * Process $include directive -- recursively parse another TIKI file
 * and merge its properties as defaults
 * ========================================================================= */

static int s_include_depth;
#define MAX_INCLUDE_DEPTH   8

static qboolean TIKI_ProcessInclude(const char *path, tiki_model_t *model) {
    if (s_include_depth >= MAX_INCLUDE_DEPTH) {
        Com_Printf("TIKI_ProcessInclude: max include depth reached (%s)\n", path);
        return qfalse;
    }

    void *buffer;
    long len = FS_ReadFile(path, &buffer);
    if (len < 0 || !buffer) {
        Com_Printf("TIKI_ProcessInclude: couldn't load '%s'\n", path);
        return qfalse;
    }

    /* Make a writable copy (FS_ReadFile buffer may not be writable) */
    char *text = (char *)Z_Malloc(len + 1);
    memcpy(text, buffer, len);
    text[len] = '\0';
    FS_FreeFile(buffer);

    s_include_depth++;
    Com_DPrintf("TIKI_ProcessInclude: %s (depth %d)\n", path, s_include_depth);

    char *data = text;
    char *tok;

    while (1) {
        tok = TIKI_Token(&data);
        if (!tok[0]) break;

        if (!Q_stricmp(tok, "TIKI")) {
            continue;   /* skip header */
        }
        else if (!Q_stricmp(tok, "$include")) {
            tok = TIKI_Token(&data);
            TIKI_ProcessInclude(tok, model);
        }
        else if (!Q_stricmp(tok, "$define")) {
            char name[64];
            tok = TIKI_Token(&data);
            Q_strncpyz(name, tok, sizeof(name));
            tok = TIKI_Token(&data);
            TIKI_AddDefine(name, tok);
        }
        else if (!Q_stricmp(tok, "setup")) {
            TIKI_ParseSetup(&data, model);
        }
        else if (!Q_stricmp(tok, "init")) {
            TIKI_ParseInit(&data, model);
        }
        else if (!Q_stricmp(tok, "animations")) {
            TIKI_ParseAnimations(&data, model);
        }
    }

    s_include_depth--;
    Z_Free(text);
    return qtrue;
}

/* =========================================================================
 * Free all dynamic data owned by a tiki_model_t
 * ========================================================================= */

void TIKI_FreeModelData(tiki_model_t *model) {
    if (!model) return;

    /* Free animation data */
    if (model->anims) {
        for (int i = 0; i < model->num_anims; i++) {
            tiki_animdef_t *a = &model->anims[i];
            /* Free frame command strings */
            for (int j = 0; j < a->num_server_cmds; j++) {
                for (int k = 0; k < a->server_cmds[j].num_args; k++)
                    Z_Free(a->server_cmds[j].args[k]);
            }
            Z_Free(a->server_cmds);
            for (int j = 0; j < a->num_client_cmds; j++) {
                for (int k = 0; k < a->client_cmds[j].num_args; k++)
                    Z_Free(a->client_cmds[j].args[k]);
            }
            Z_Free(a->client_cmds);
        }
        Z_Free(model->anims);
    }

    /* Free init command strings */
    if (model->server_initcmds) {
        for (int i = 0; i < model->num_server_initcmds; i++) {
            for (int j = 0; j < model->server_initcmds[i].num_args; j++)
                Z_Free(model->server_initcmds[i].args[j]);
        }
        Z_Free(model->server_initcmds);
    }
    if (model->client_initcmds) {
        for (int i = 0; i < model->num_client_initcmds; i++) {
            for (int j = 0; j < model->client_initcmds[i].num_args; j++)
                Z_Free(model->client_initcmds[i].args[j]);
        }
        Z_Free(model->client_initcmds);
    }
}

/* =========================================================================
 * TIKI_ParseFile -- main entry point
 *
 * Loads a .tik file, processes directives, and returns a fully populated
 * tiki_model_t. Returns NULL on failure.
 * ========================================================================= */

tiki_model_t *TIKI_ParseFile(const char *filename) {
    void *buffer;
    long len;

    Com_DPrintf("TIKI_ParseFile: %s\n", filename);

    len = FS_ReadFile(filename, &buffer);
    if (len < 0 || !buffer) {
        Com_Printf("TIKI_ParseFile: couldn't load '%s'\n", filename);
        return NULL;
    }

    /* Make a writable copy for parsing */
    char *text = (char *)Z_Malloc(len + 1);
    memcpy(text, buffer, len);
    text[len] = '\0';
    FS_FreeFile(buffer);

    /* Allocate model */
    tiki_model_t *model = (tiki_model_t *)Z_Malloc(sizeof(tiki_model_t));
    memset(model, 0, sizeof(*model));
    Q_strncpyz(model->name, filename, sizeof(model->name));
    model->scale = 1.0f;
    model->lod_scale = 1.0f;

    /* Reset parser state */
    TIKI_ClearDefines();
    s_include_depth = 0;
    COM_BeginParseSession(filename);

    char *data = text;
    char *tok;
    qboolean found_header = qfalse;

    while (1) {
        tok = TIKI_Token(&data);
        if (!tok[0]) break;

        if (!Q_stricmp(tok, "TIKI")) {
            found_header = qtrue;
            continue;
        }
        else if (!Q_stricmp(tok, "$include")) {
            tok = TIKI_Token(&data);
            TIKI_ProcessInclude(tok, model);
        }
        else if (!Q_stricmp(tok, "$define")) {
            char name[64];
            tok = TIKI_Token(&data);
            Q_strncpyz(name, tok, sizeof(name));
            tok = TIKI_Token(&data);
            TIKI_AddDefine(name, tok);
        }
        else if (!Q_stricmp(tok, "setup")) {
            TIKI_ParseSetup(&data, model);
        }
        else if (!Q_stricmp(tok, "init")) {
            TIKI_ParseInit(&data, model);
        }
        else if (!Q_stricmp(tok, "animations")) {
            TIKI_ParseAnimations(&data, model);
        }
        else {
            Com_DPrintf("TIKI_ParseFile: unknown top-level token '%s' in %s\n",
                       tok, filename);
        }
    }

    Z_Free(text);

    if (!found_header) {
        Com_Printf("TIKI_ParseFile: '%s' missing TIKI header\n", filename);
        /* Still return the model -- some included files may not have headers */
    }

    Com_DPrintf("TIKI_ParseFile: %s -- %d surfaces, %d anims, "
               "%d server initcmds, %d client initcmds\n",
               filename, model->num_surfaces, model->num_anims,
               model->num_server_initcmds, model->num_client_initcmds);

    return model;
}
