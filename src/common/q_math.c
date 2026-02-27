/*
 * q_math.c -- Extended math functions
 */

#include "q_shared.h"
#include "q_math.h"

float Com_Clamp(float min, float max, float value) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

int Com_AbsClampi(int min, int max, int value) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

void ClearBounds(vec3_t mins, vec3_t maxs) {
    mins[0] = mins[1] = mins[2] = 99999;
    maxs[0] = maxs[1] = maxs[2] = -99999;
}

void AddPointToBounds(const vec3_t v, vec3_t mins, vec3_t maxs) {
    for (int i = 0; i < 3; i++) {
        if (v[i] < mins[i]) mins[i] = v[i];
        if (v[i] > maxs[i]) maxs[i] = v[i];
    }
}

void MatrixMultiply(const mat3_t in1, const mat3_t in2, mat3_t out) {
    out[0][0] = in1[0][0]*in2[0][0] + in1[0][1]*in2[1][0] + in1[0][2]*in2[2][0];
    out[0][1] = in1[0][0]*in2[0][1] + in1[0][1]*in2[1][1] + in1[0][2]*in2[2][1];
    out[0][2] = in1[0][0]*in2[0][2] + in1[0][1]*in2[1][2] + in1[0][2]*in2[2][2];
    out[1][0] = in1[1][0]*in2[0][0] + in1[1][1]*in2[1][0] + in1[1][2]*in2[2][0];
    out[1][1] = in1[1][0]*in2[0][1] + in1[1][1]*in2[1][1] + in1[1][2]*in2[2][1];
    out[1][2] = in1[1][0]*in2[0][2] + in1[1][1]*in2[1][2] + in1[1][2]*in2[2][2];
    out[2][0] = in1[2][0]*in2[0][0] + in1[2][1]*in2[1][0] + in1[2][2]*in2[2][0];
    out[2][1] = in1[2][0]*in2[0][1] + in1[2][1]*in2[1][1] + in1[2][2]*in2[2][1];
    out[2][2] = in1[2][0]*in2[0][2] + in1[2][1]*in2[1][2] + in1[2][2]*in2[2][2];
}

void AnglesToAxis(const vec3_t angles, vec3_t axis[3]) {
    AngleVectors(angles, axis[0], axis[1], axis[2]);
    /* negate right to get a proper right-handed coordinate system */
    VectorScale(axis[1], -1, axis[1]);
}

void AxisClear(vec3_t axis[3]) {
    axis[0][0] = 1; axis[0][1] = 0; axis[0][2] = 0;
    axis[1][0] = 0; axis[1][1] = 1; axis[1][2] = 0;
    axis[2][0] = 0; axis[2][1] = 0; axis[2][2] = 1;
}

void AxisCopy(const vec3_t in[3], vec3_t out[3]) {
    VectorCopy(in[0], out[0]);
    VectorCopy(in[1], out[1]);
    VectorCopy(in[2], out[2]);
}

float crandom(void) {
    return 2.0f * ((float)rand() / (float)RAND_MAX) - 1.0f;
}

float flrandom(float min, float max) {
    return min + (max - min) * ((float)rand() / (float)RAND_MAX);
}
