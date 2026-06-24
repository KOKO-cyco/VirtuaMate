/**
 * @file mtoon_outline.c
 * @brief MToon outline pass (world / screen width modes)
 * @version 1.0
 * @date 2025-06-23
 * @copyright Copyright (c) Tuya Inc. All Rights Reserved.
 */
#include "mtoon_outline.h"

#include <math.h>
#include <string.h>

extern GLuint __link_program(const char *vs_src, const char *fs_src);

static const char *s_outline_vs =
    "#version 140\n"
    "#extension GL_ARB_explicit_attrib_location : enable\n"
    "layout(location=0) in vec3 a_pos;\n"
    "layout(location=1) in vec3 a_normal;\n"
    "layout(location=2) in vec2 a_uv;\n"
    "layout(location=3) in vec4 a_bone_ids;\n"
    "layout(location=4) in vec4 a_bone_weights;\n"
    "uniform mat4 u_mvp;\n"
    "uniform mat4 u_model;\n"
    "uniform int  u_skinned;\n"
    "uniform int  u_width_mode;\n"
    "uniform float u_outline_width;\n"
    "uniform int u_has_width_tex;\n"
    "uniform sampler2D u_width_tex;\n"
    "uniform samplerBuffer u_bone_tbo;\n"
    "out vec2 v_uv;\n"
    "mat4 getBoneMatrix(int idx) {\n"
    "    int base = idx * 4;\n"
    "    return mat4(\n"
    "        texelFetch(u_bone_tbo, base + 0),\n"
    "        texelFetch(u_bone_tbo, base + 1),\n"
    "        texelFetch(u_bone_tbo, base + 2),\n"
    "        texelFetch(u_bone_tbo, base + 3)\n"
    "    );\n"
    "}\n"
    "void main() {\n"
    "    vec4 pos = vec4(a_pos, 1.0);\n"
    "    vec4 norm4 = vec4(a_normal, 0.0);\n"
    "    if (u_skinned != 0) {\n"
    "        mat4 skin = mat4(0.0);\n"
    "        ivec4 ids = ivec4(a_bone_ids);\n"
    "        skin += a_bone_weights.x * getBoneMatrix(ids.x);\n"
    "        skin += a_bone_weights.y * getBoneMatrix(ids.y);\n"
    "        skin += a_bone_weights.z * getBoneMatrix(ids.z);\n"
    "        skin += a_bone_weights.w * getBoneMatrix(ids.w);\n"
    "        pos  = skin * pos;\n"
    "        norm4 = skin * norm4;\n"
    "    }\n"
    "    float width = u_outline_width;\n"
    "    if (u_has_width_tex != 0) {\n"
    "        width *= texture(u_width_tex, a_uv).r;\n"
    "    }\n"
    "    if (u_width_mode == 1) {\n"
    "        vec3 n = normalize(norm4.xyz);\n"
    "        pos.xyz += n * width;\n"
    "        gl_Position = u_mvp * pos;\n"
    "    } else {\n"
    "        vec4 clip = u_mvp * pos;\n"
    "        vec3 cn = mat3(u_mvp) * normalize(norm4.xyz);\n"
    "        vec2 d = normalize(cn.xy) * width * clip.w;\n"
    "        clip.xy += d;\n"
    "        gl_Position = clip;\n"
    "    }\n"
    "    v_uv = a_uv;\n"
    "}\n";

static const char *s_outline_fs =
    "#version 140\n"
    "out vec4 frag_color;\n"
    "uniform vec4 u_outline_color;\n"
    "void main() {\n"
    "    frag_color = u_outline_color;\n"
    "}\n";

/* Unity MToon base is 0.01; VirtuaMate boosts for clearer outlines in the viewer. */
#define MTOON_OUTLINE_WIDTH_SCALE 0.01f

/**
 * @brief Compute world-space outline width from MToon material.
 * @param[in] mat    Material.
 * @param[in] model  Model (unused; kept for API symmetry).
 * @return Width in object units for the vertex shader.
 */
static float __outline_world_width(const vrm_material_t *mat,
                                   const vrm_model_t *model)
{
    (void)model;
    if (mat == NULL) {
        return 0.0f;
    }
    /* Unity MToon: object-space offset = 0.01 * _OutlineWidth * tex. */
    return MTOON_OUTLINE_WIDTH_SCALE * mat->outline_width;
}

/**
 * @brief Compute screen-space outline width factor.
 * @param[in] mat    Material.
 * @param[in] model  Model (unused).
 * @return Clip-space width multiplier (before clip.w).
 */
static float __outline_screen_width(const vrm_material_t *mat,
                                    const vrm_model_t *model)
{
    (void)model;
    if (mat == NULL) {
        return 0.0f;
    }
    /* Unity MToon screen mode uses the same 0.01 factor on _OutlineWidth. */
    return MTOON_OUTLINE_WIDTH_SCALE * mat->outline_width;
}

GLuint mtoon_outline_create_program(void)
{
    return __link_program(s_outline_vs, s_outline_fs);
}

void mtoon_outline_draw_mesh(GLuint prog,
                             const gpu_mesh_t *g,
                             const vrm_material_t *mat,
                             const vrm_model_t *model,
                             const float mvp[16],
                             const float model_mat[16],
                             GLuint bone_tbo_tex,
                             GLuint white_tex)
{
    float width;

    if (prog == 0 || g == NULL || mat == NULL || !vrm_material_has_outline(mat)) {
        return;
    }

    glUseProgram(prog);
    glUniformMatrix4fv(glGetUniformLocation(prog, "u_mvp"), 1, GL_FALSE, mvp);
    glUniformMatrix4fv(glGetUniformLocation(prog, "u_model"), 1, GL_FALSE, model_mat);
    glUniform4fv(glGetUniformLocation(prog, "u_outline_color"), 1, mat->outline_color);
    glUniform1i(glGetUniformLocation(prog, "u_skinned"), g->has_bones);
    glUniform1i(glGetUniformLocation(prog, "u_width_mode"), mat->outline_width_mode);
    glUniform1i(glGetUniformLocation(prog, "u_has_width_tex"), g->has_outline_width_tex);
    glUniform1i(glGetUniformLocation(prog, "u_bone_tbo"), 1);

    if (mat->outline_width_mode == VRM_MTOON_OUTLINE_WORLD) {
        width = __outline_world_width(mat, model);
    } else {
        width = __outline_screen_width(mat, model);
    }
    glUniform1f(glGetUniformLocation(prog, "u_outline_width"), width);

    glActiveTexture(GL_TEXTURE0);
    if (g->has_outline_width_tex) {
        glBindTexture(GL_TEXTURE_2D, g->tex_outline_width);
    } else {
        glBindTexture(GL_TEXTURE_2D, white_tex);
    }
    glUniform1i(glGetUniformLocation(prog, "u_width_tex"), 0);

    if (bone_tbo_tex) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_BUFFER, bone_tbo_tex);
    }

    glBindVertexArray(g->vao);
    glDrawElements(GL_TRIANGLES, (GLsizei)g->index_count, GL_UNSIGNED_INT, 0);
}
