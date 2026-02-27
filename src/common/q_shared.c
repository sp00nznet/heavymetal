/*
 * q_shared.c -- Shared utility functions
 *
 * These functions are used by both the engine and game modules.
 * Reimplemented from the FAKK2 SDK and id Tech 3 GPL source.
 */

#include "q_shared.h"
#include <ctype.h>

/* =========================================================================
 * String utilities
 * ========================================================================= */

int Q_stricmp(const char *s1, const char *s2) {
    unsigned char c1, c2;
    do {
        c1 = (unsigned char)*s1++;
        c2 = (unsigned char)*s2++;
        if (c1 != c2) {
            if (c1 >= 'a' && c1 <= 'z') c1 -= 'a' - 'A';
            if (c2 >= 'a' && c2 <= 'z') c2 -= 'a' - 'A';
            if (c1 != c2) return (int)c1 - (int)c2;
        }
    } while (c1);
    return 0;
}

int Q_strncmp(const char *s1, const char *s2, int n) {
    return strncmp(s1, s2, n);
}

int Q_stricmpn(const char *s1, const char *s2, int n) {
    int c1, c2;
    do {
        c1 = *s1++;
        c2 = *s2++;
        if (!n--) return 0;
        if (c1 != c2) {
            if (c1 >= 'a' && c1 <= 'z') c1 -= 'a' - 'A';
            if (c2 >= 'a' && c2 <= 'z') c2 -= 'a' - 'A';
            if (c1 != c2) return c1 < c2 ? -1 : 1;
        }
    } while (c1);
    return 0;
}

void Q_strncpyz(char *dest, const char *src, int destsize) {
    if (!dest || !src || destsize < 1) return;
    strncpy(dest, src, destsize - 1);
    dest[destsize - 1] = '\0';
}

char *Q_strupr(char *s1) {
    char *s = s1;
    while (*s) {
        *s = (char)toupper((unsigned char)*s);
        s++;
    }
    return s1;
}

char *Q_strlwr(char *s1) {
    char *s = s1;
    while (*s) {
        *s = (char)tolower((unsigned char)*s);
        s++;
    }
    return s1;
}

/* =========================================================================
 * Math utilities
 * ========================================================================= */

void VectorNormalize(vec3_t v) {
    float length = sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
    if (length) {
        float ilength = 1.0f / length;
        v[0] *= ilength;
        v[1] *= ilength;
        v[2] *= ilength;
    }
}

void VectorNormalize2(const vec3_t v, vec3_t out) {
    float length = sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
    if (length) {
        float ilength = 1.0f / length;
        out[0] = v[0] * ilength;
        out[1] = v[1] * ilength;
        out[2] = v[2] * ilength;
    } else {
        VectorClear(out);
    }
}

void CrossProduct(const vec3_t v1, const vec3_t v2, vec3_t cross) {
    cross[0] = v1[1]*v2[2] - v1[2]*v2[1];
    cross[1] = v1[2]*v2[0] - v1[0]*v2[2];
    cross[2] = v1[0]*v2[1] - v1[1]*v2[0];
}

void AngleVectors(const vec3_t angles, vec3_t forward, vec3_t right, vec3_t up) {
    float sp, sy, sr, cp, cy, cr;
    float angle;

    angle = DEG2RAD(angles[YAW]);
    sy = sinf(angle);
    cy = cosf(angle);
    angle = DEG2RAD(angles[PITCH]);
    sp = sinf(angle);
    cp = cosf(angle);
    angle = DEG2RAD(angles[ROLL]);
    sr = sinf(angle);
    cr = cosf(angle);

    if (forward) {
        forward[0] = cp * cy;
        forward[1] = cp * sy;
        forward[2] = -sp;
    }
    if (right) {
        right[0] = (-1*sr*sp*cy + -1*cr*-sy);
        right[1] = (-1*sr*sp*sy + -1*cr*cy);
        right[2] = -1*sr*cp;
    }
    if (up) {
        up[0] = (cr*sp*cy + -sr*-sy);
        up[1] = (cr*sp*sy + -sr*cy);
        up[2] = cr*cp;
    }
}

float AngleMod(float a) {
    a = (360.0f / 65536) * ((int)(a * (65536 / 360.0f)) & 65535);
    return a;
}

float LerpAngle(float from, float to, float frac) {
    if (to - from > 180) to -= 360;
    if (to - from < -180) to += 360;
    return from + frac * (to - from);
}

/* =========================================================================
 * Token parsing -- the classic Quake token parser
 * ========================================================================= */

static char com_token[MAX_TOKEN_CHARS];
static char com_parsename[MAX_TOKEN_CHARS];
static int  com_lines;

void COM_BeginParseSession(const char *name) {
    com_lines = 0;
    Q_strncpyz(com_parsename, name, sizeof(com_parsename));
}

int COM_GetCurrentParseLine(void) {
    return com_lines;
}

char *COM_Parse(char **data_p) {
    return COM_ParseExt(data_p, qtrue);
}

char *COM_ParseExt(char **data_p, qboolean allowLineBreak) {
    int c, len;
    char *data;

    data = *data_p;
    len = 0;
    com_token[0] = '\0';

    if (!data) {
        *data_p = NULL;
        return com_token;
    }

    /* skip whitespace */
skipwhite:
    while ((c = *data) <= ' ') {
        if (!c) {
            *data_p = NULL;
            return com_token;
        }
        if (c == '\n') {
            com_lines++;
            if (!allowLineBreak) {
                *data_p = data;
                return com_token;
            }
        }
        data++;
    }

    /* skip // comments */
    if (c == '/' && data[1] == '/') {
        while (*data && *data != '\n') data++;
        goto skipwhite;
    }

    /* skip /* comments */
    if (c == '/' && data[1] == '*') {
        data += 2;
        while (*data) {
            if (*data == '\n') com_lines++;
            if (data[0] == '*' && data[1] == '/') {
                data += 2;
                break;
            }
            data++;
        }
        goto skipwhite;
    }

    /* handle quoted strings */
    if (c == '"') {
        data++;
        while (1) {
            c = *data++;
            if (c == '"' || !c) {
                com_token[len] = '\0';
                *data_p = data;
                return com_token;
            }
            if (c == '\n') com_lines++;
            if (len < MAX_TOKEN_CHARS - 1) {
                com_token[len++] = (char)c;
            }
        }
    }

    /* parse a regular word */
    do {
        if (len < MAX_TOKEN_CHARS - 1) {
            com_token[len++] = (char)c;
        }
        data++;
        c = *data;
    } while (c > 32);

    com_token[len] = '\0';
    *data_p = data;
    return com_token;
}
