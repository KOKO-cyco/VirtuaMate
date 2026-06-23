/**
 * @file mtoon_draw.h
 * @brief MToon-aware mesh drawing sorted by renderQueue
 * @version 1.0
 * @date 2025-06-23
 * @copyright Copyright (c) Tuya Inc. All Rights Reserved.
 */
#ifndef MTOON_DRAW_H
#define MTOON_DRAW_H

#include "gl_renderer_internal.h"
#include "vrm_loader.h"

#include <GL/glew.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Draw all model meshes sorted by MToon renderQueue.
 *
 * Meshes with MToon materials use mtoon_prog; others use legacy_prog.
 *
 * @param[in] model         Loaded VRM model.
 * @param[in] gpu           GPU mesh array (mesh_count entries).
 * @param[in] mtoon_prog    MToon shader program.
 * @param[in] legacy_prog   Legacy toon shader program.
 * @param[in] mvp           Model-view-projection matrix.
 * @param[in] model_mat     Model matrix.
 * @param[in] light_dir     Normalized light direction (3 floats).
 * @param[in] bone_tbo_tex  Bone matrix texture buffer object (0 if none).
 * @param[in] white_tex     1x1 white fallback texture.
 * @return none
 */
void mtoon_draw_model(const vrm_model_t *model,
                      const gpu_mesh_t *gpu,
                      GLuint mtoon_prog,
                      GLuint legacy_prog,
                      const float mvp[16],
                      const float model_mat[16],
                      const float light_dir[3],
                      GLuint bone_tbo_tex,
                      GLuint white_tex);

#ifdef __cplusplus
}
#endif

#endif /* MTOON_DRAW_H */
