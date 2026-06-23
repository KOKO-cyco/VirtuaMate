/**
 * @file vrm_loader_internal.h
 * @brief Internal shared declarations for VRM loader submodules
 * @version 0.1
 * @date 2025-06-23
 * @copyright Copyright (c) Tuya Inc. All Rights Reserved.
 */
#ifndef VRM_LOADER_INTERNAL_H
#define VRM_LOADER_INTERNAL_H

#include "vrm_loader.h"

#include <assimp/scene.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Shared loader state
 * --------------------------------------------------------------------------- */
extern int s_bone_map_count;
extern int s_tex_cache_count;

/* ---------------------------------------------------------------------------
 * Math helpers (vrm_loader_math.c)
 * --------------------------------------------------------------------------- */
void vrm_loader_normalize_path(char *p);
void vrm_loader_ai_mat4_to_float16(const struct aiMatrix4x4 *ai, float *out);
void vrm_loader_quat_identity(float q[4]);
void vrm_loader_quat_normalize(float q[4]);
void vrm_loader_quat_slerp(float out[4], const float a[4], const float b[4], float t);
void vrm_loader_quat_to_mat4(const float q[4], float m[16]);
void vrm_loader_quat_conjugate(float out[4], const float q[4]);
void vrm_loader_quat_multiply(float out[4], const float a[4], const float b[4]);
void vrm_loader_mat4_compose(float out[16], const float t[3], const float q[4], const float s[3]);
void vrm_loader_mat4_decompose(const float m[16], float t[3], float q[4], float s[3]);

/* ---------------------------------------------------------------------------
 * Mesh / skeleton (vrm_loader_mesh.c)
 * --------------------------------------------------------------------------- */
int vrm_loader_find_bone_by_name(const char *name);
void vrm_loader_register_node_bones(const struct aiNode *node, int parent_idx,
                                    vrm_model_t *model);
void vrm_loader_fill_offset_matrices(const struct aiScene *scene, vrm_model_t *model);
uint32_t vrm_loader_count_meshes(const struct aiNode *node);
void vrm_loader_extract_meshes(const struct aiNode *node,
                               const struct aiScene *scene,
                               vrm_model_t *model, uint32_t *out_idx,
                               const char *model_dir,
                               const float node_world[16]);
void vrm_loader_compute_bbox(vrm_model_t *model);
int vrm_loader_find_vrm_mesh_by_assimp(const vrm_model_t *m, int assimp_mi);

/* ---------------------------------------------------------------------------
 * Animation (vrm_loader_anim_extract.c / vrm_loader_anim_eval.c)
 * --------------------------------------------------------------------------- */
void vrm_loader_extract_animations(const struct aiScene *scene, vrm_model_t *model);
int vrm_loader_find_keyframe(const float *times, uint32_t count, float t, float *out_t);
void vrm_loader_lerp3(float out[3], const float a[3], const float b[3], float t);

/* ---------------------------------------------------------------------------
 * VRM extensions
 * --------------------------------------------------------------------------- */
void vrm_loader_extract_vrm_humanoid_full(vrm_model_t *model, const char *path);
void vrm_loader_extract_vrm_expressions(vrm_model_t *model, const char *path);
void vrm_loader_extract_vrm_spring_bones(vrm_model_t *model, const char *path);
void vrm_loader_extract_vrm_node_constraints(vrm_model_t *model, const char *path);
int vrm_loader_find_bone_by_node_index(const vrm_model_t *model,
                                       const char *json_str, int node_index);
int vrm_loader_build_bone_chain(const vrm_model_t *model, int root_bone,
                                int *out_bones, int max_bones);

/* ---------------------------------------------------------------------------
 * Textures (vrm_loader_texture.c)
 * --------------------------------------------------------------------------- */
int __decode_embedded_texture(const struct aiTexture *ai_tex, vrm_texture_t *out);
int __resolve_texture_index(const struct aiMaterial *mat,
                            const struct aiScene *scene,
                            char *out_path, int path_size);
int __load_external_texture(vrm_model_t *model, const char *model_dir,
                            const char *rel_path);
int vrm_loader_load_gltf_images(vrm_model_t *model, const char *path,
                                const char *model_dir);

#ifdef __cplusplus
}
#endif

#endif /* VRM_LOADER_INTERNAL_H */
