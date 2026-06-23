/**
 * @file mtoon_shaders.h
 * @brief MToon GLSL shaders and GPU program helpers
 * @version 1.0
 * @date 2025-06-23
 * @copyright Copyright (c) Tuya Inc. All Rights Reserved.
 */
#ifndef MTOON_SHADERS_H
#define MTOON_SHADERS_H

#include "gl_renderer_internal.h"
#include "vrm_loader.h"

#include <GL/glew.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const char *s_mtoon_vs;
extern const char *s_mtoon_fs;

/**
 * @brief Compile and link the MToon shader program.
 * @return OpenGL program id (0 on failure).
 */
GLuint mtoon_create_program(void);

/**
 * @brief Upload a decoded CPU texture to a new GL texture object.
 * @param[in] tex  Decoded RGBA texture (may be NULL).
 * @return GL texture id, or 0 if unavailable.
 */
GLuint mtoon_upload_texture(const vrm_texture_t *tex);

/**
 * @brief Upload GPU mesh data including MToon main/shade textures.
 * @param[out] gpu    GPU mesh wrapper.
 * @param[in]  mesh   CPU mesh.
 * @param[in]  model  Loaded model.
 * @return none
 */
void mtoon_upload_mesh(gpu_mesh_t *gpu, const vrm_mesh_t *mesh,
                       const vrm_model_t *model, uint32_t mesh_index);

/**
 * @brief Delete GL textures owned by a gpu_mesh_t (not VAO/VBO).
 * @param[in,out] gpu  GPU mesh.
 * @return none
 */
void mtoon_free_mesh_textures(gpu_mesh_t *gpu);

#ifdef __cplusplus
}
#endif

#endif /* MTOON_SHADERS_H */
