/**
 * @file mtoon_draw.c
 * @brief MToon render-queue sorted drawing
 * @version 1.0
 * @date 2025-06-23
 * @copyright Copyright (c) Tuya Inc. All Rights Reserved.
 */
#include "mtoon_draw.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t mesh_index;
    int      sort_key;
    int      render_queue;
    int      blend_mode;
} draw_item_t;

/**
 * @brief Compare draw items by sort key ascending, then mesh index.
 * @param[in] a  First item.
 * @param[in] b  Second item.
 * @return Comparison result for qsort.
 */
static int __draw_item_cmp(const void *a, const void *b)
{
    const draw_item_t *ia = (const draw_item_t *)a;
    const draw_item_t *ib = (const draw_item_t *)b;

    if (ia->sort_key < ib->sort_key) {
        return -1;
    }
    if (ia->sort_key > ib->sort_key) {
        return 1;
    }
    if (ia->blend_mode < ib->blend_mode) {
        return -1;
    }
    if (ia->blend_mode > ib->blend_mode) {
        return 1;
    }
    if (ia->render_queue < ib->render_queue) {
        return -1;
    }
    if (ia->render_queue > ib->render_queue) {
        return 1;
    }
    if (ia->mesh_index < ib->mesh_index) {
        return -1;
    }
    if (ia->mesh_index > ib->mesh_index) {
        return 1;
    }
    return 0;
}

/**
 * @brief UniVRM default sort bucket from MToon blend mode.
 * @param[in] blend_mode  vrm_mtoon_blend_mode_t value.
 * @return Bucket key (2000 / 2450 / 2501 / 3000).
 */
static int __blend_mode_bucket(int blend_mode)
{
    switch (blend_mode) {
    case VRM_MTOON_CUTOUT:
        return 2450;
    case VRM_MTOON_TRANSPARENT:
        return 2501;
    case VRM_MTOON_TRANSPARENT_ZWRITE:
        return 3000;
    case VRM_MTOON_OPAQUE:
    default:
        return 2000;
    }
}

/**
 * @brief Derive draw sort key from blend bucket and renderQueue.
 *
 * Uses max(bucket, renderQueue) as primary key. Within the same key, higher
 * blend_mode draws later (TransparentWithZWrite after Transparent), then
 * renderQueue ascending (Alicia hair 2501 before 2550).
 *
 * @param[in] mat  Material, or NULL for opaque default.
 * @return Sort key.
 */
static int __material_sort_key(const vrm_material_t *mat)
{
    int bucket;
    int rq;

    if (mat == NULL) {
        return 2000;
    }
    bucket = __blend_mode_bucket((int)mat->blend_mode);
    rq = mat->render_queue > 0 ? mat->render_queue : bucket;
    return (bucket > rq) ? bucket : rq;
}

/**
 * @brief Apply GL blend / depth state from MToon blend mode.
 *
 * Do NOT use renderQueue alone: many models set queue=2501 while _BlendMode is
 * TransparentWithZWrite (3), which must keep depth writes enabled.
 *
 * @param[in] blend_mode  vrm_mtoon_blend_mode_t value.
 * @return none
 */
static void __apply_blend_gl_state(int blend_mode)
{
    switch (blend_mode) {
    case VRM_MTOON_CUTOUT:
    case VRM_MTOON_OPAQUE:
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        break;
    case VRM_MTOON_TRANSPARENT:
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        break;
    case VRM_MTOON_TRANSPARENT_ZWRITE:
    default:
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_TRUE);
        break;
    }
}

/**
 * @brief Draw one mesh with the legacy toon shader.
 * @return none
 */
static void __draw_legacy_mesh(GLuint prog, const gpu_mesh_t *g,
                               GLuint bone_tbo_tex, GLuint white_tex)
{
    glUseProgram(prog);
    glUniform4fv(glGetUniformLocation(prog, "u_base_color"), 1, g->color);
    glUniform1i(glGetUniformLocation(prog, "u_has_texture"), g->has_texture);
    glUniform1i(glGetUniformLocation(prog, "u_skinned"), g->has_bones);

    glActiveTexture(GL_TEXTURE0);
    if (g->has_texture) {
        glBindTexture(GL_TEXTURE_2D, g->tex_id);
    } else {
        glBindTexture(GL_TEXTURE_2D, white_tex);
    }

    glBindVertexArray(g->vao);
    glDrawElements(GL_TRIANGLES, (GLsizei)g->index_count, GL_UNSIGNED_INT, 0);
}

/**
 * @brief Draw one mesh with the MToon shader.
 * @return none
 */
static void __draw_mtoon_mesh(GLuint prog, const gpu_mesh_t *g,
                              const vrm_material_t *mat,
                              GLuint bone_tbo_tex, GLuint white_tex)
{
    float shade_color[4];

    glUseProgram(prog);
    glUniform4fv(glGetUniformLocation(prog, "u_color"), 1, g->color);
    if (mat != NULL) {
        glUniform4fv(glGetUniformLocation(prog, "u_shade_color"), 1, mat->shade_color);
        glUniform1f(glGetUniformLocation(prog, "u_shade_shift"), mat->shade_shift);
        glUniform1f(glGetUniformLocation(prog, "u_shade_toony"), mat->shade_toony);
        glUniform1f(glGetUniformLocation(prog, "u_cutoff"), mat->cutoff);
        glUniform1i(glGetUniformLocation(prog, "u_blend_mode"), (int)mat->blend_mode);
    } else {
        shade_color[0] = 0.9f;
        shade_color[1] = 0.8f;
        shade_color[2] = 0.7f;
        shade_color[3] = 1.0f;
        glUniform4fv(glGetUniformLocation(prog, "u_shade_color"), 1, shade_color);
        glUniform1f(glGetUniformLocation(prog, "u_shade_shift"), 0.0f);
        glUniform1f(glGetUniformLocation(prog, "u_shade_toony"), 0.9f);
        glUniform1f(glGetUniformLocation(prog, "u_cutoff"), 0.5f);
        glUniform1i(glGetUniformLocation(prog, "u_blend_mode"), 0);
    }

    glUniform1i(glGetUniformLocation(prog, "u_has_main"), g->has_texture);
    glUniform1i(glGetUniformLocation(prog, "u_has_shade"), g->has_shade_tex);
    glUniform1i(glGetUniformLocation(prog, "u_skinned"), g->has_bones);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g->has_texture ? g->tex_id : white_tex);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, g->has_shade_tex ? g->tex_shade : white_tex);
    glUniform1i(glGetUniformLocation(prog, "u_main_tex"), 0);
    glUniform1i(glGetUniformLocation(prog, "u_shade_tex"), 2);

    glBindVertexArray(g->vao);
    glDrawElements(GL_TRIANGLES, (GLsizei)g->index_count, GL_UNSIGNED_INT, 0);
}

void mtoon_draw_model(const vrm_model_t *model,
                      const gpu_mesh_t *gpu,
                      GLuint mtoon_prog,
                      GLuint legacy_prog,
                      const float mvp[16],
                      const float model_mat[16],
                      const float light_dir[3],
                      GLuint bone_tbo_tex,
                      GLuint white_tex)
{
    draw_item_t *items;
    uint32_t     i;
    int          last_blend = -1;
    GLuint       active_prog = 0;

    if (model == NULL || gpu == NULL || model->mesh_count == 0) {
        return;
    }

    items = (draw_item_t *)malloc(model->mesh_count * sizeof(draw_item_t));
    if (items == NULL) {
        return;
    }

    for (i = 0; i < model->mesh_count; i++) {
        const vrm_material_t *mat = vrm_mesh_material(model, i);

        items[i].mesh_index = i;
        items[i].blend_mode = mat ? (int)mat->blend_mode : VRM_MTOON_OPAQUE;
        items[i].render_queue = mat && mat->render_queue > 0
            ? mat->render_queue : __blend_mode_bucket(items[i].blend_mode);
        items[i].sort_key = __material_sort_key(mat);
    }
    qsort(items, model->mesh_count, sizeof(draw_item_t), __draw_item_cmp);

    if (bone_tbo_tex) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_BUFFER, bone_tbo_tex);
    }

    for (i = 0; i < model->mesh_count; i++) {
        uint32_t              mi = items[i].mesh_index;
        const gpu_mesh_t     *g = &gpu[mi];
        const vrm_material_t *mat = vrm_mesh_material(model, mi);
        GLuint                prog;

        if (items[i].blend_mode != last_blend) {
            __apply_blend_gl_state(items[i].blend_mode);
            last_blend = items[i].blend_mode;
        }

        if (g->use_mtoon && mtoon_prog != 0) {
            prog = mtoon_prog;
            if (active_prog != prog) {
                glUseProgram(prog);
                glUniformMatrix4fv(glGetUniformLocation(prog, "u_mvp"),
                                     1, GL_FALSE, mvp);
                glUniformMatrix4fv(glGetUniformLocation(prog, "u_model"),
                                     1, GL_FALSE, model_mat);
                glUniform3fv(glGetUniformLocation(prog, "u_light_dir"), 1, light_dir);
                glUniform1i(glGetUniformLocation(prog, "u_bone_tbo"), 1);
                active_prog = prog;
            }
            __draw_mtoon_mesh(prog, g, mat, bone_tbo_tex, white_tex);
        } else {
            prog = legacy_prog;
            if (active_prog != prog) {
                __apply_blend_gl_state(VRM_MTOON_OPAQUE);
                last_blend = VRM_MTOON_OPAQUE;
                glUseProgram(prog);
                glUniformMatrix4fv(glGetUniformLocation(prog, "u_mvp"),
                                     1, GL_FALSE, mvp);
                glUniformMatrix4fv(glGetUniformLocation(prog, "u_model"),
                                     1, GL_FALSE, model_mat);
                glUniform3fv(glGetUniformLocation(prog, "u_light_dir"), 1, light_dir);
                glUniform1i(glGetUniformLocation(prog, "u_texture"), 0);
                glUniform1i(glGetUniformLocation(prog, "u_bone_tbo"), 1);
                active_prog = prog;
            }
            __draw_legacy_mesh(prog, g, bone_tbo_tex, white_tex);
        }
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    free(items);
}
