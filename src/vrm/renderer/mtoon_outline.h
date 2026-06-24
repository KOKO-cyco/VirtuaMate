/**
 * @file mtoon_outline.h
 * @brief MToon outline pass shaders and draw helpers
 * @version 1.0
 * @date 2025-06-23
 * @copyright Copyright (c) Tuya Inc. All Rights Reserved.
 */
#ifndef MTOON_OUTLINE_H
#define MTOON_OUTLINE_H

#include "gl_renderer_internal.h"
#include "vrm_loader.h"

#include <GL/glew.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Compile and link the MToon outline shader program.
 * @return OpenGL program id (0 on failure).
 */
GLuint mtoon_outline_create_program(void);

/**
 * @brief Draw outline for one mesh (caller sets GL cull/depth state).
 * @param[in] prog          Outline shader program.
 * @param[in] g             GPU mesh.
 * @param[in] mat           MToon material with outline params.
 * @param[in] model         Loaded model (for extent scale).
 * @param[in] mvp           Model-view-projection matrix.
 * @param[in] model_mat      Model matrix.
 * @param[in] bone_tbo_tex    Bone TBO texture (0 if none).
 * @param[in] white_tex       1x1 white fallback texture.
 * @return none
 */
void mtoon_outline_draw_mesh(GLuint prog,
                             const gpu_mesh_t *g,
                             const vrm_material_t *mat,
                             const vrm_model_t *model,
                             const float mvp[16],
                             const float model_mat[16],
                             GLuint bone_tbo_tex,
                             GLuint white_tex);

#ifdef __cplusplus
}
#endif

#endif /* MTOON_OUTLINE_H */
