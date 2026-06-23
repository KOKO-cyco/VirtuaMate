/**
 * @file vrm_loader_anim_eval.c
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

int vrm_loader_find_keyframe(const float *times, uint32_t count, float t, float *out_t)
{
    if (count == 0) { *out_t = 0; return 0; }
    if (t <= times[0]) { *out_t = 0; return 0; }
    if (t >= times[count - 1]) { *out_t = 0; return (int)count - 1; }

    for (uint32_t i = 0; i < count - 1; i++) {
        if (t >= times[i] && t < times[i + 1]) {
            float dt = times[i + 1] - times[i];
            *out_t = (dt > 1e-8f) ? (t - times[i]) / dt : 0.0f;
            return (int)i;
        }
    }
    *out_t = 0;
    return (int)count - 1;
}

void vrm_loader_lerp3(float out[3], const float a[3], const float b[3], float t)
{
    out[0] = a[0] + t * (b[0] - a[0]);
    out[1] = a[1] + t * (b[1] - a[1]);
    out[2] = a[2] + t * (b[2] - a[2]);
}

void vrm_evaluate_animation(const vrm_model_t *model, uint32_t anim_index,
                             float time_sec, float *out_matrices)
{
    if (anim_index >= model->animation_count) {
        vrm_rest_pose_matrices(model, out_matrices);
        return;
    }

    const vrm_animation_t *anim = &model->animations[anim_index];

    /* Wrap time */
    float t = time_sec;
    if (anim->duration > 0) {
        t = fmodf(t, anim->duration);
        if (t < 0) t += anim->duration;
    }

    /* Start with rest-pose local transforms for all bones */
    uint32_t nb = model->bone_count;
    float *local_transforms = (float *)malloc(nb * 16 * sizeof(float));
    for (uint32_t i = 0; i < nb; i++)
        memcpy(&local_transforms[i * 16], model->bones[i].local_transform, 16 * sizeof(float));

    /* Apply animation channels — overwrite local transforms for animated bones */
    for (uint32_t ai = 0; ai < anim->bone_anim_count; ai++) {
        const vrm_bone_anim_t *ba = &anim->bone_anims[ai];
        int bone_idx = ba->bone_index;
        if (bone_idx < 0 || (uint32_t)bone_idx >= nb) continue;

        /* Decompose current local transform into T, R, S */
        float pos[3], rot[4], scl[3];
        vrm_loader_mat4_decompose(&local_transforms[bone_idx * 16], pos, rot, scl);

        /* Apply each channel */
        for (uint32_t ci = 0; ci < ba->channel_count; ci++) {
            const vrm_anim_channel_t *ch = &ba->channels[ci];
            if (ch->count == 0) continue;

            float interp;
            int ki = vrm_loader_find_keyframe(ch->times, ch->count, t, &interp);

            switch (ch->path) {
            case 0: /* translation */ {
                if (ki < (int)ch->count - 1) {
                    vrm_loader_lerp3(pos, &ch->values[ki*3], &ch->values[(ki+1)*3], interp);
                } else {
                    memcpy(pos, &ch->values[ki*3], 3*sizeof(float));
                }
                break;
            }
            case 1: /* rotation (quaternion) */ {
                if (ki < (int)ch->count - 1) {
                    vrm_loader_quat_slerp(rot, &ch->values[ki*4], &ch->values[(ki+1)*4], interp);
                } else {
                    memcpy(rot, &ch->values[ki*4], 4*sizeof(float));
                }
                break;
            }
            case 2: /* scale */ {
                if (ki < (int)ch->count - 1) {
                    vrm_loader_lerp3(scl, &ch->values[ki*3], &ch->values[(ki+1)*3], interp);
                } else {
                    memcpy(scl, &ch->values[ki*3], 3*sizeof(float));
                }
                break;
            }
            }
        }

        /* Recompose local transform */
        vrm_loader_mat4_compose(&local_transforms[bone_idx * 16], pos, rot, scl);
    }

    /* ---- Evaluate expression animation channels ---- */
    if (anim->expr_anims && anim->expr_anim_count > 0) {
        /* Cast away const for expression weight update — these are mutable state */
        vrm_model_t *mut_model = (vrm_model_t *)model;
        for (uint32_t ei = 0; ei < anim->expr_anim_count; ei++) {
            const vrm_expr_anim_t *ea = &anim->expr_anims[ei];
            if (ea->expression_index < 0 || (uint32_t)ea->expression_index >= model->expression_count)
                continue;
            const vrm_anim_channel_t *ch = &ea->channel;
            if (ch->count == 0) continue;

            float interp;
            int ki = vrm_loader_find_keyframe(ch->times, ch->count, t, &interp);
            float w;
            if (ki < (int)ch->count - 1) {
                w = ch->values[ki] + interp * (ch->values[ki + 1] - ch->values[ki]);
            } else {
                w = ch->values[ki];
            }
            if (w < 0.0f) w = 0.0f;
            if (w > 1.0f) w = 1.0f;
            mut_model->expression_weights[ea->expression_index] = w;
        }
    }

    /* ---- Compute global transforms via hierarchy ---- */
    float *global_transforms = (float *)malloc(nb * 16 * sizeof(float));

    for (uint32_t i = 0; i < nb; i++) {
        if (model->bones[i].parent < 0) {
            memcpy(&global_transforms[i * 16], &local_transforms[i * 16], 16 * sizeof(float));
        } else {
            int p = model->bones[i].parent;
            mat4_multiply(&global_transforms[i * 16],
                          &global_transforms[p * 16],
                          &local_transforms[i * 16]);
        }
    }

    /* ---- Apply VRMC_node_constraint (Aim / Roll) ---- */
    for (uint32_t ci = 0; ci < model->constraint_count; ci++) {
        const vrm_node_constraint_t *nc = &model->constraints[ci];
        int bi = nc->bone_index;
        int si = nc->source_index;
        if (bi < 0 || (uint32_t)bi >= nb || si < 0 || (uint32_t)si >= nb) continue;
        if (nc->weight < 1e-6f) continue;

        float *dst_global = &global_transforms[bi * 16];
        const float *src_global = &global_transforms[si * 16];

        if (nc->type == 0) {
            /* ---- Aim constraint ---- */
            /* Make the constrained bone point its aim axis toward the source bone's
             * world-space position. */
            float dst_pos[3] = { dst_global[12], dst_global[13], dst_global[14] };
            float src_pos[3] = { src_global[12], src_global[13], src_global[14] };

            /* Direction from constrained to source in world space */
            float dir[3] = { src_pos[0]-dst_pos[0], src_pos[1]-dst_pos[1], src_pos[2]-dst_pos[2] };
            float dir_len = sqrtf(dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2]);
            if (dir_len < 1e-8f) continue;
            dir[0] /= dir_len; dir[1] /= dir_len; dir[2] /= dir_len;

            /* Transform direction into parent's local space */
            int par = model->bones[bi].parent;
            if (par >= 0) {
                float par_t[3], par_q[4], par_s[3];
                vrm_loader_mat4_decompose(&global_transforms[par * 16], par_t, par_q, par_s);
                float inv_par_q[4];
                vrm_loader_quat_conjugate(inv_par_q, par_q);
                /* Rotate dir by inv_par_q */
                float tx2 = 2.0f*(inv_par_q[1]*dir[2] - inv_par_q[2]*dir[1]);
                float ty2 = 2.0f*(inv_par_q[2]*dir[0] - inv_par_q[0]*dir[2]);
                float tz2 = 2.0f*(inv_par_q[0]*dir[1] - inv_par_q[1]*dir[0]);
                float lx = dir[0] + inv_par_q[3]*tx2 + (inv_par_q[1]*tz2 - inv_par_q[2]*ty2);
                float ly = dir[1] + inv_par_q[3]*ty2 + (inv_par_q[2]*tx2 - inv_par_q[0]*tz2);
                float lz = dir[2] + inv_par_q[3]*tz2 + (inv_par_q[0]*ty2 - inv_par_q[1]*tx2);
                dir[0] = lx; dir[1] = ly; dir[2] = lz;
            }

            /* The aim axis in local rest space */
            float aim[3] = {0,0,0};
            switch (nc->axis) {
                case VRM_AIM_POSITIVE_X: aim[0] =  1; break;
                case VRM_AIM_NEGATIVE_X: aim[0] = -1; break;
                case VRM_AIM_POSITIVE_Y: aim[1] =  1; break;
                case VRM_AIM_NEGATIVE_Y: aim[1] = -1; break;
                case VRM_AIM_POSITIVE_Z: aim[2] =  1; break;
                case VRM_AIM_NEGATIVE_Z: aim[2] = -1; break;
            }

            /* Compute rotation from aim to dir using cross + dot */
            float dot = aim[0]*dir[0] + aim[1]*dir[1] + aim[2]*dir[2];
            float cross[3] = {
                aim[1]*dir[2] - aim[2]*dir[1],
                aim[2]*dir[0] - aim[0]*dir[2],
                aim[0]*dir[1] - aim[1]*dir[0]
            };
            float cross_len = sqrtf(cross[0]*cross[0]+cross[1]*cross[1]+cross[2]*cross[2]);

            float aim_q[4] = {0, 0, 0, 1}; /* identity */
            if (cross_len > 1e-8f) {
                /* axis = cross / |cross|, angle = atan2(|cross|, dot) */
                float half_angle = atan2f(cross_len, dot) * 0.5f;
                float s_ha = sinf(half_angle) / cross_len;
                aim_q[0] = cross[0] * s_ha;
                aim_q[1] = cross[1] * s_ha;
                aim_q[2] = cross[2] * s_ha;
                aim_q[3] = cosf(half_angle);
            } else if (dot < -0.999f) {
                /* 180° rotation: pick a perpendicular axis */
                float perp[3] = {1,0,0};
                if (fabsf(aim[0]) > 0.9f) { perp[0]=0; perp[1]=1; }
                aim_q[0] = perp[0]; aim_q[1] = perp[1]; aim_q[2] = perp[2]; aim_q[3] = 0;
            }

            /* Apply weight via slerp with identity */
            if (nc->weight < 0.999f) {
                float id_q[4] = {0,0,0,1};
                vrm_loader_quat_slerp(aim_q, id_q, aim_q, nc->weight);
            }

            /* New local transform = aim_q (replaces rest rotation) */
            float rest_t[3], rest_q[4], rest_s[3];
            vrm_loader_mat4_decompose(&local_transforms[bi * 16], rest_t, rest_q, rest_s);
            vrm_loader_mat4_compose(&local_transforms[bi * 16], rest_t, aim_q, rest_s);

        } else {
            /* ---- Roll constraint ---- */
            /* Extract source bone's local rotation */
            float src_t[3], src_q[4], src_s[3];
            vrm_loader_mat4_decompose(&local_transforms[si * 16], src_t, src_q, src_s);

            /* Decompose source rotation into roll (twist) around the specified axis */
            float twist_q[4] = {0, 0, 0, 1};
            int roll_axis = nc->axis; /* 0=X, 1=Y, 2=Z */
            float proj = src_q[roll_axis]; /* projection onto twist axis */
            float twist_w = src_q[3];
            float twist_len = sqrtf(proj*proj + twist_w*twist_w);

            if (twist_len > 1e-8f) {
                twist_q[roll_axis] = proj / twist_len;
                twist_q[3] = twist_w / twist_len;
            }

            /* Apply weight */
            if (nc->weight < 0.999f) {
                float id_q[4] = {0,0,0,1};
                vrm_loader_quat_slerp(twist_q, id_q, twist_q, nc->weight);
            }

            /* Compose with rest rotation: roll_applied = twist_q * rest_q */
            float rest_t[3], rest_q[4], rest_s[3];
            vrm_loader_mat4_decompose(&local_transforms[bi * 16], rest_t, rest_q, rest_s);
            float new_q[4];
            vrm_loader_quat_multiply(new_q, twist_q, rest_q);
            vrm_loader_mat4_compose(&local_transforms[bi * 16], rest_t, new_q, rest_s);
        }

        /* Recompute global transform for this bone and its descendants */
        for (uint32_t j = (uint32_t)bi; j < nb; j++) {
            if (j == (uint32_t)bi || model->bones[j].parent >= bi) {
                if (model->bones[j].parent < 0) {
                    memcpy(&global_transforms[j * 16], &local_transforms[j * 16], 16 * sizeof(float));
                } else {
                    int p = model->bones[j].parent;
                    mat4_multiply(&global_transforms[j * 16],
                                  &global_transforms[p * 16],
                                  &local_transforms[j * 16]);
                }
            }
        }
    }

    /* ---- Final bone matrices = global * inverseBindMatrix ---- */
    for (uint32_t i = 0; i < nb; i++) {
        mat4_multiply(&out_matrices[i * 16],
                      &global_transforms[i * 16],
                      model->bones[i].offset_matrix);
    }

    free(local_transforms);
    free(global_transforms);
}

void vrm_rest_pose_matrices(const vrm_model_t *model, float *out_matrices)
{
    uint32_t nb = model->bone_count;

    /* Compute global transforms from rest-pose local transforms */
    float *global_transforms = (float *)malloc(nb * 16 * sizeof(float));

    for (uint32_t i = 0; i < nb; i++) {
        if (model->bones[i].parent < 0) {
            memcpy(&global_transforms[i * 16], model->bones[i].local_transform, 16 * sizeof(float));
        } else {
            int p = model->bones[i].parent;
            mat4_multiply(&global_transforms[i * 16],
                          &global_transforms[p * 16],
                          model->bones[i].local_transform);
        }
    }

    for (uint32_t i = 0; i < nb; i++) {
        mat4_multiply(&out_matrices[i * 16],
                      &global_transforms[i * 16],
                      model->bones[i].offset_matrix);
    }

    free(global_transforms);
}

/* ================================================================== */
/*  VRM Expression / BlendShape extraction from glTF JSON              */
/* ================================================================== */

/** Helper: find vrm_mesh_t index by Assimp mesh index */
int vrm_loader_find_vrm_mesh_by_assimp(const vrm_model_t *m, int assimp_mi)
{
    for (uint32_t i = 0; i < m->mesh_count; i++) {
        if (m->meshes[i].assimp_mesh_index == assimp_mi) return (int)i;
    }
    return -1;
}

/**
 * Parse VRM expressions (BlendShape groups) from VRM JSON.
 * Supports both VRM 0.x (blendShapeMaster.blendShapeGroups) and
 * VRM 1.0 (VRMC_vrm.expressions.preset / custom).
 */
