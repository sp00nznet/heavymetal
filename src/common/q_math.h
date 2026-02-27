/*
 * q_math.h -- Extended math functions for FAKK2
 */

#ifndef Q_MATH_H
#define Q_MATH_H

#include "fakk_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Matrix operations */
void    MatrixMultiply(const mat3_t in1, const mat3_t in2, mat3_t out);
void    AnglesToAxis(const vec3_t angles, vec3_t axis[3]);
void    AxisClear(vec3_t axis[3]);
void    AxisCopy(const vec3_t in[3], vec3_t out[3]);

/* Bounds */
void    ClearBounds(vec3_t mins, vec3_t maxs);
void    AddPointToBounds(const vec3_t v, vec3_t mins, vec3_t maxs);

/* Plane classification */
int     BoxOnPlaneSide(const vec3_t mins, const vec3_t maxs, const struct cplane_s *plane);

/* Interpolation */
float   Com_Clamp(float min, float max, float value);
int     Com_AbsClampi(int min, int max, int value);

/* Random */
float   crandom(void);     /* -1.0 to 1.0 */
float   flrandom(float min, float max);

/* Plane type */
typedef struct cplane_s {
    vec3_t  normal;
    float   dist;
    byte    type;
    byte    signbits;
    byte    pad[2];
} cplane_t;

#ifdef __cplusplus
}
#endif

#endif /* Q_MATH_H */
