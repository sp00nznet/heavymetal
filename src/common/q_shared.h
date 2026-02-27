/*
 * q_shared.h -- Definitions shared between engine and game modules
 *
 * Derived from the publicly released FAKK2 SDK (Ritual Entertainment)
 * and id Tech 3 GPL source. Clean-room reimplementation for recomp.
 */

#ifndef Q_SHARED_H
#define Q_SHARED_H

#include "fakk_types.h"
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

/* =========================================================================
 * Coordinate system
 * FAKK2 uses the standard Quake coordinate system:
 *   +X = forward, +Y = left, +Z = up
 * ========================================================================= */

/* Angle indices */
#define PITCH   0   /* up / down */
#define YAW     1   /* left / right */
#define ROLL    2   /* tilt */

/* =========================================================================
 * Math
 * ========================================================================= */

#ifndef M_PI
#define M_PI    3.14159265358979323846f
#endif

#define DEG2RAD(a)  ((a) * (M_PI / 180.0f))
#define RAD2DEG(a)  ((a) * (180.0f / M_PI))

#define DotProduct(x,y)         ((x)[0]*(y)[0]+(x)[1]*(y)[1]+(x)[2]*(y)[2])
#define VectorSubtract(a,b,c)   ((c)[0]=(a)[0]-(b)[0],(c)[1]=(a)[1]-(b)[1],(c)[2]=(a)[2]-(b)[2])
#define VectorAdd(a,b,c)        ((c)[0]=(a)[0]+(b)[0],(c)[1]=(a)[1]+(b)[1],(c)[2]=(a)[2]+(b)[2])
#define VectorCopy(a,b)         ((b)[0]=(a)[0],(b)[1]=(a)[1],(b)[2]=(a)[2])
#define VectorScale(v,s,o)      ((o)[0]=(v)[0]*(s),(o)[1]=(v)[1]*(s),(o)[2]=(v)[2]*(s))
#define VectorMA(v,s,b,o)       ((o)[0]=(v)[0]+(b)[0]*(s),(o)[1]=(v)[1]+(b)[1]*(s),(o)[2]=(v)[2]+(b)[2]*(s))
#define VectorClear(a)          ((a)[0]=(a)[1]=(a)[2]=0)
#define VectorLength(a)         (sqrtf(DotProduct((a),(a))))
#define VectorSet(v,x,y,z)      ((v)[0]=(x),(v)[1]=(y),(v)[2]=(z))
#define Vector4Copy(a,b)        ((b)[0]=(a)[0],(b)[1]=(a)[1],(b)[2]=(a)[2],(b)[3]=(a)[3])

#define SnapVector(v)           {(v)[0]=((int)(v)[0]);(v)[1]=((int)(v)[1]);(v)[2]=((int)(v)[2]);}

/* =========================================================================
 * Surface and content flags
 * ========================================================================= */

/* Surface flags -- from surfaceflags.h */
#define SURF_NODAMAGE       0x1
#define SURF_SLICK          0x2
#define SURF_SKY            0x4
#define SURF_LADDER         0x8
#define SURF_NOIMPACT       0x10
#define SURF_NOMARKS        0x20
#define SURF_FLESH          0x40
#define SURF_NODRAW         0x80
#define SURF_HINT           0x100
#define SURF_SKIP           0x200
#define SURF_NOLIGHTMAP     0x400
#define SURF_POINTLIGHT     0x800
#define SURF_METALSTEPS     0x1000
#define SURF_NOSTEPS        0x2000
#define SURF_NONSOLID       0x4000
#define SURF_LIGHTFILTER    0x8000
#define SURF_ALPHASHADOW    0x10000
#define SURF_NODLIGHT       0x20000
#define SURF_PAPER          0x40000     /* UberTools addition */
#define SURF_WOOD           0x80000     /* UberTools addition */
#define SURF_ROCK           0x100000    /* UberTools addition */
#define SURF_DIRT           0x200000    /* UberTools addition */
#define SURF_GRILL          0x400000    /* UberTools addition */
#define SURF_GRASS          0x800000    /* UberTools addition */
#define SURF_MUD            0x1000000   /* UberTools addition */
#define SURF_PUDDLE         0x2000000   /* UberTools addition */
#define SURF_GLASS          0x4000000   /* UberTools addition */
#define SURF_GRAVEL         0x8000000   /* UberTools addition */
#define SURF_SAND           0x10000000  /* UberTools addition */

/* Content flags */
#define CONTENTS_SOLID      0x1
#define CONTENTS_LAVA       0x8
#define CONTENTS_SLIME      0x10
#define CONTENTS_WATER      0x20
#define CONTENTS_FOG        0x40
#define CONTENTS_PLAYERCLIP 0x10000
#define CONTENTS_MONSTERCLIP 0x20000
#define CONTENTS_WEAPONCLIP 0x40000     /* UberTools addition */
#define CONTENTS_BODY       0x2000000
#define CONTENTS_CORPSE     0x4000000
#define CONTENTS_DETAIL     0x8000000
#define CONTENTS_STRUCTURAL 0x10000000
#define CONTENTS_TRANSLUCENT 0x20000000
#define CONTENTS_TRIGGER    0x40000000
#define CONTENTS_NODROP     0x80000000

/* =========================================================================
 * Trace results
 * ========================================================================= */

typedef struct {
    qboolean    allsolid;       /* if true, plane is not valid */
    qboolean    startsolid;     /* if true, the initial point was in a solid area */
    float       fraction;       /* time completed, 1.0 = didn't hit anything */
    vec3_t      endpos;         /* final position */
    struct {
        float   normal[3];
        float   dist;
        int     type;
        int     signbits;
    } plane;
    int         surfaceFlags;   /* surface flags of hit surface */
    int         contents;       /* contents on other side of surface hit */
    int         entityNum;      /* entity the surface is a part of */
    int         location;       /* body location for damage */
} trace_t;

/* =========================================================================
 * Cvar flags
 * ========================================================================= */

#define CVAR_ARCHIVE        0x0001  /* set to cause it to be saved to config */
#define CVAR_USERINFO       0x0002  /* sent to server on connect or change */
#define CVAR_SERVERINFO     0x0004  /* sent in response to front end requests */
#define CVAR_SYSTEMINFO     0x0008  /* these cvars will be duplicated on all clients */
#define CVAR_INIT           0x0010  /* don't allow change from console at all */
#define CVAR_LATCH          0x0020  /* will only change at map restart */
#define CVAR_ROM            0x0040  /* display only, cannot be set by user */
#define CVAR_USER_CREATED   0x0080  /* created by a set command */
#define CVAR_TEMP           0x0100  /* can be set even when cheats are disabled */
#define CVAR_CHEAT          0x0200  /* can not be changed if cheats are disabled */
#define CVAR_NORESTART      0x0400  /* do not clear when a cvar_restart is issued */

typedef struct cvar_s {
    char        *name;
    char        *string;
    char        *resetString;
    char        *latchedString;
    int         flags;
    qboolean    modified;
    int         modificationCount;
    float       value;
    int         integer;
    struct cvar_s *next;
    struct cvar_s *hashNext;
} cvar_t;

/* =========================================================================
 * Key/button definitions (from keys.h)
 * ========================================================================= */

typedef enum {
    K_TAB = 9,
    K_ENTER = 13,
    K_ESCAPE = 27,
    K_SPACE = 32,
    K_BACKSPACE = 127,
    K_UPARROW = 128,
    K_DOWNARROW,
    K_LEFTARROW,
    K_RIGHTARROW,
    K_ALT,
    K_CTRL,
    K_SHIFT,
    K_F1,
    K_F2,
    K_F3,
    K_F4,
    K_F5,
    K_F6,
    K_F7,
    K_F8,
    K_F9,
    K_F10,
    K_F11,
    K_F12,
    K_INS,
    K_DEL,
    K_PGDN,
    K_PGUP,
    K_HOME,
    K_END,
    K_MOUSE1 = 178,
    K_MOUSE2,
    K_MOUSE3,
    K_MOUSE4,
    K_MOUSE5,
    K_MWHEELDOWN = 184,
    K_MWHEELUP,
    K_JOY1 = 186,
    K_PAUSE = 255,
    K_LAST
} keyNum_t;

/* =========================================================================
 * Filesystem
 * ========================================================================= */

typedef enum {
    FS_READ,
    FS_WRITE,
    FS_APPEND,
    FS_APPEND_SYNC
} fsMode_t;

typedef enum {
    FS_SEEK_CUR,
    FS_SEEK_END,
    FS_SEEK_SET
} fsOrigin_t;

/* =========================================================================
 * Utility functions (implemented in q_shared.c)
 * ========================================================================= */

#ifdef __cplusplus
extern "C" {
#endif

/* String utilities */
int     Q_stricmp(const char *s1, const char *s2);
int     Q_strncmp(const char *s1, const char *s2, int n);
int     Q_stricmpn(const char *s1, const char *s2, int n);
void    Q_strncpyz(char *dest, const char *src, int destsize);
char    *Q_strupr(char *s1);
char    *Q_strlwr(char *s1);

/* Info string parsing */
char    *Info_ValueForKey(const char *s, const char *key);
void    Info_SetValueForKey(char *s, const char *key, const char *value);

/* Math utilities */
void    VectorNormalize(vec3_t v);
void    VectorNormalize2(const vec3_t v, vec3_t out);
float   VectorNormalizeFast(vec3_t v);
void    CrossProduct(const vec3_t v1, const vec3_t v2, vec3_t cross);
void    RotatePointAroundVector(vec3_t dst, const vec3_t dir, const vec3_t point, float degrees);
void    AngleVectors(const vec3_t angles, vec3_t forward, vec3_t right, vec3_t up);
void    PerpendicularVector(vec3_t dst, const vec3_t src);
float   AngleMod(float a);
float   LerpAngle(float from, float to, float frac);

/* Color utilities */
float   *ColorForIndex(int i);

/* Token parsing */
char    *COM_Parse(char **data_p);
char    *COM_ParseExt(char **data_p, qboolean allowLineBreak);
int     COM_Compress(char *data_p);
void    COM_BeginParseSession(const char *name);
int     COM_GetCurrentParseLine(void);

#ifdef __cplusplus
}
#endif

#endif /* Q_SHARED_H */
