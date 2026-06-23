/**
 * @file vrm_skybox.h
 * @brief Cubemap skybox renderer for the VRM viewer.
 * @version 1.0
 * @date 2025-06-23
 * @copyright Copyright (c) Tuya Inc. All Rights Reserved.
 */

#ifndef VRM_SKYBOX_H
#define VRM_SKYBOX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <GL/glew.h>

typedef struct {
    GLuint cubemap_tex;
    GLuint vao;
    GLuint vbo;
    GLuint program;
    GLint  u_vp;
    int    loaded;
} skybox_t;

int skybox_init(skybox_t *skybox, const char *dir);
void skybox_draw(const skybox_t *skybox, const float view_mat[16],
                 const float proj_mat[16]);
void skybox_destroy(skybox_t *skybox);

#ifdef __cplusplus
}
#endif

#endif /* VRM_SKYBOX_H */
