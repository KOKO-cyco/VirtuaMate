/**
 * @file vrm_loader_math.c
 * @brief VRM loader submodule (split from vrm_loader.c)
 * @version 0.1
 * @date 2025-06-23
 * @copyright Copyright (c) Tuya Inc. All Rights Reserved.
 */
#include "vrm_loader.h"
#include "vrm_loader_internal.h"
#include "mat4_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <float.h>
#include <libgen.h>
#include <math.h>

#include <assimp/cimport.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/material.h>

void vrm_loader_normalize_path(char *p)
{
    for (char *c = p; *c; c++)
        if (*c == '\\') *c = '/';
}

void vrm_loader_ai_mat4_to_float16(const struct aiMatrix4x4 *ai, float *out)
{
    /* Assimp is row-major, OpenGL/our convention is column-major → transpose */
    out[ 0] = ai->a1; out[ 1] = ai->b1; out[ 2] = ai->c1; out[ 3] = ai->d1;
    out[ 4] = ai->a2; out[ 5] = ai->b2; out[ 6] = ai->c2; out[ 7] = ai->d2;
    out[ 8] = ai->a3; out[ 9] = ai->b3; out[10] = ai->c3; out[11] = ai->d3;
    out[12] = ai->a4; out[13] = ai->b4; out[14] = ai->c4; out[15] = ai->d4;
}

/* ---- quaternion helpers ---- */

void vrm_loader_quat_identity(float q[4])
{
    q[0] = 0.0f; q[1] = 0.0f; q[2] = 0.0f; q[3] = 1.0f;
}

void vrm_loader_quat_normalize(float q[4])
{
    float len = sqrtf(q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3]);
    if (len > 1e-8f) { q[0]/=len; q[1]/=len; q[2]/=len; q[3]/=len; }
}

void vrm_loader_quat_slerp(float out[4], const float a[4], const float b[4], float t)
{
    float dot = a[0]*b[0] + a[1]*b[1] + a[2]*b[2] + a[3]*b[3];
    float nb[4] = { b[0], b[1], b[2], b[3] };
    if (dot < 0.0f) {
        dot = -dot;
        nb[0] = -nb[0]; nb[1] = -nb[1]; nb[2] = -nb[2]; nb[3] = -nb[3];
    }
    if (dot > 0.9995f) {
        /* Linear interpolation for very close quaternions */
        for (int i = 0; i < 4; i++) out[i] = a[i] + t * (nb[i] - a[i]);
    } else {
        float theta = acosf(dot);
        float sin_theta = sinf(theta);
        float wa = sinf((1.0f - t) * theta) / sin_theta;
        float wb = sinf(t * theta) / sin_theta;
        for (int i = 0; i < 4; i++) out[i] = wa * a[i] + wb * nb[i];
    }
    vrm_loader_quat_normalize(out);
}

void vrm_loader_quat_to_mat4(const float q[4], float m[16])
{
    float x = q[0], y = q[1], z = q[2], w = q[3];
    float x2 = x+x, y2 = y+y, z2 = z+z;
    float xx = x*x2, xy = x*y2, xz = x*z2;
    float yy = y*y2, yz = y*z2, zz = z*z2;
    float wx = w*x2, wy = w*y2, wz = w*z2;

    memset(m, 0, 16 * sizeof(float));
    m[ 0] = 1.0f - (yy + zz);
    m[ 1] = xy + wz;
    m[ 2] = xz - wy;
    m[ 4] = xy - wz;
    m[ 5] = 1.0f - (xx + zz);
    m[ 6] = yz + wx;
    m[ 8] = xz + wy;
    m[ 9] = yz - wx;
    m[10] = 1.0f - (xx + yy);
    m[15] = 1.0f;
}

/** Quaternion conjugate (inverse for unit quaternions): q* = (-x,-y,-z,w) */
void vrm_loader_quat_conjugate(float out[4], const float q[4])
{
    out[0] = -q[0]; out[1] = -q[1]; out[2] = -q[2]; out[3] = q[3];
}

/** Quaternion multiplication: out = a * b  (Hamilton product, x,y,z,w order) */
void vrm_loader_quat_multiply(float out[4], const float a[4], const float b[4])
{
    float ax = a[0], ay = a[1], az = a[2], aw = a[3];
    float bx = b[0], by = b[1], bz = b[2], bw = b[3];
    out[0] = aw*bx + ax*bw + ay*bz - az*by;
    out[1] = aw*by - ax*bz + ay*bw + az*bx;
    out[2] = aw*bz + ax*by - ay*bx + az*bw;
    out[3] = aw*bw - ax*bx - ay*by - az*bz;
}

void vrm_loader_mat4_compose(float out[16], const float t[3], const float q[4], const float s[3])
{
    vrm_loader_quat_to_mat4(q, out);
    /* Apply scale */
    out[0] *= s[0]; out[1] *= s[0]; out[2]  *= s[0];
    out[4] *= s[1]; out[5] *= s[1]; out[6]  *= s[1];
    out[8] *= s[2]; out[9] *= s[2]; out[10] *= s[2];
    /* Set translation */
    out[12] = t[0]; out[13] = t[1]; out[14] = t[2];
}

/* Decompose a 4x4 col-major matrix into T, R(quat), S */
void vrm_loader_mat4_decompose(const float m[16], float t[3], float q[4], float s[3])
{
    t[0] = m[12]; t[1] = m[13]; t[2] = m[14];

    /* Extract scale from column lengths */
    s[0] = sqrtf(m[0]*m[0] + m[1]*m[1] + m[2]*m[2]);
    s[1] = sqrtf(m[4]*m[4] + m[5]*m[5] + m[6]*m[6]);
    s[2] = sqrtf(m[8]*m[8] + m[9]*m[9] + m[10]*m[10]);

    if (s[0] < 1e-6f) s[0] = 1.0f;
    if (s[1] < 1e-6f) s[1] = 1.0f;
    if (s[2] < 1e-6f) s[2] = 1.0f;

    /* Rotation matrix (remove scale) */
    float r[9];
    r[0] = m[0]/s[0]; r[1] = m[1]/s[0]; r[2] = m[2]/s[0];
    r[3] = m[4]/s[1]; r[4] = m[5]/s[1]; r[5] = m[6]/s[1];
    r[6] = m[8]/s[2]; r[7] = m[9]/s[2]; r[8] = m[10]/s[2];

    /* Rotation matrix to quaternion */
    float trace = r[0] + r[4] + r[8];
    if (trace > 0.0f) {
        float ss = sqrtf(trace + 1.0f) * 2.0f;
        q[3] = 0.25f * ss;
        q[0] = (r[5] - r[7]) / ss;
        q[1] = (r[6] - r[2]) / ss;
        q[2] = (r[1] - r[3]) / ss;
    } else if (r[0] > r[4] && r[0] > r[8]) {
        float ss = sqrtf(1.0f + r[0] - r[4] - r[8]) * 2.0f;
        q[3] = (r[5] - r[7]) / ss;
        q[0] = 0.25f * ss;
        q[1] = (r[1] + r[3]) / ss;
        q[2] = (r[6] + r[2]) / ss;
    } else if (r[4] > r[8]) {
        float ss = sqrtf(1.0f + r[4] - r[0] - r[8]) * 2.0f;
        q[3] = (r[6] - r[2]) / ss;
        q[0] = (r[1] + r[3]) / ss;
        q[1] = 0.25f * ss;
        q[2] = (r[5] + r[7]) / ss;
    } else {
        float ss = sqrtf(1.0f + r[8] - r[0] - r[4]) * 2.0f;
        q[3] = (r[1] - r[3]) / ss;
        q[0] = (r[6] + r[2]) / ss;
        q[1] = (r[5] + r[7]) / ss;
        q[2] = 0.25f * ss;
    }
    vrm_loader_quat_normalize(q);
}
