/**
 * @file vrm_spring_bone.h
 * @brief VRM Spring Bone physics simulation — Verlet integration for
 *        secondary animation (hair, clothing, accessories).
 * @version 1.0
 * @date 2025-06-23
 * @copyright Copyright (c) Tuya Inc. All Rights Reserved.
 */

#ifndef VRM_SPRING_BONE_H
#define VRM_SPRING_BONE_H

#include "vrm_loader.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int     bone_index;
    float   current_tail[3];
    float   prev_tail[3];
    float   spring_length;
    float   initial_local_rot[4];
    float   bone_axis[3];
    float   stiffness;
    float   gravity_power;
    float   gravity_dir[3];
    float   drag_force;
    float   hit_radius;
} spring_joint_state_t;

typedef struct {
    spring_joint_state_t *joints;
    int                   joint_count;
    int                   center_bone;
    int                  *collider_group_indices;
    int                   collider_group_count;
} spring_chain_t;

typedef struct {
    vrm_model_t   *model;
    spring_chain_t *chains;
    int             chain_count;
    int             initialized;
    int             enabled;
} spring_bone_ctx_t;

void spring_bone_init(spring_bone_ctx_t *ctx, vrm_model_t *model);
void spring_bone_update(spring_bone_ctx_t *ctx, float dt, float *bone_matrices);
void spring_bone_reset(spring_bone_ctx_t *ctx, const float *bone_matrices);
void spring_bone_shutdown(spring_bone_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* VRM_SPRING_BONE_H */
