/**
 * @file mtoon_shaders.c
 * @brief MToon GLSL shaders and GPU mesh upload
 * @version 1.0
 * @date 2025-06-23
 * @copyright Copyright (c) Tuya Inc. All Rights Reserved.
 */
#include "mtoon_shaders.h"

#include <stdio.h>
#include <string.h>

/* Reuse compile helper from gl_renderer_shaders.c */
extern GLuint __link_program(const char *vs_src, const char *fs_src);

const char *s_mtoon_vs =
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
    "uniform samplerBuffer u_bone_tbo;\n"
    "out vec2 v_uv;\n"
    "out vec3 v_normal;\n"
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
    "    gl_Position = u_mvp * pos;\n"
    "    v_uv = a_uv;\n"
    "    v_normal = normalize(mat3(u_model) * norm4.xyz);\n"
    "}\n";

const char *s_mtoon_fs =
    "#version 140\n"
    "in vec2 v_uv;\n"
    "in vec3 v_normal;\n"
    "out vec4 frag_color;\n"
    "uniform vec4 u_color;\n"
    "uniform vec4 u_shade_color;\n"
    "uniform sampler2D u_main_tex;\n"
    "uniform sampler2D u_shade_tex;\n"
    "uniform int u_has_main;\n"
    "uniform int u_has_shade;\n"
    "uniform float u_shade_shift;\n"
    "uniform float u_shade_toony;\n"
    "uniform float u_cutoff;\n"
    "uniform int u_blend_mode;\n"
    "uniform vec3 u_light_dir;\n"
    "void main() {\n"
    "    vec4 main_c = (u_has_main != 0)\n"
    "        ? texture(u_main_tex, v_uv) * u_color : u_color;\n"
    "    vec4 shade_c = (u_has_shade != 0)\n"
    "        ? texture(u_shade_tex, v_uv) * u_shade_color : u_shade_color;\n"
    "    if (u_blend_mode == 1 && main_c.a < u_cutoff) discard;\n"
    "    if (u_blend_mode >= 2 && main_c.a < 0.01) discard;\n"
    "    vec3 n = normalize(v_normal);\n"
    "    vec3 l = normalize(u_light_dir);\n"
    "    float nl = dot(n, l);\n"
    "    float grad = nl * u_shade_toony + u_shade_shift;\n"
    "    grad = clamp(grad, 0.0, 1.0);\n"
    "    vec3 rgb = mix(shade_c.rgb, main_c.rgb, grad);\n"
    "    frag_color = vec4(rgb, main_c.a);\n"
    "}\n";

GLuint mtoon_create_program(void)
{
    return __link_program(s_mtoon_vs, s_mtoon_fs);
}

GLuint mtoon_upload_texture(const vrm_texture_t *tex)
{
    GLuint id = 0;

    if (tex == NULL || tex->pixels == NULL || tex->width <= 0 || tex->height <= 0) {
        return 0;
    }

    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 tex->width, tex->height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, tex->pixels);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    return id;
}

void mtoon_free_mesh_textures(gpu_mesh_t *gpu)
{
    if (gpu == NULL) {
        return;
    }
    if (gpu->tex_id) {
        glDeleteTextures(1, &gpu->tex_id);
        gpu->tex_id = 0;
    }
    if (gpu->tex_shade && gpu->tex_shade != gpu->tex_id) {
        glDeleteTextures(1, &gpu->tex_shade);
    }
    gpu->tex_shade = 0;
    gpu->has_texture = 0;
    gpu->has_shade_tex = 0;
}

void mtoon_upload_mesh(gpu_mesh_t *gpu, const vrm_mesh_t *mesh,
                       const vrm_model_t *model, uint32_t mesh_index)
{
    const vrm_material_t *mat;
    int                   tex_main = mesh->texture_index;
    int                   tex_shade = -1;

    memset(gpu, 0, sizeof(*gpu));
    gpu->material_index = mesh->material_index;
    gpu->render_queue = vrm_material_render_queue(model, mesh_index);
    gpu->use_mtoon = vrm_material_is_mtoon(model, mesh_index);
    memcpy(gpu->color, mesh->color, sizeof(float) * 4);
    gpu->has_bones = mesh->has_bones;
    gpu->has_morph = (mesh->morph_target_count > 0) ? 1 : 0;

    mat = vrm_mesh_material(model, mesh_index);
    if (mat != NULL) {
        if (mat->tex_main >= 0) {
            tex_main = mat->tex_main;
        }
        tex_shade = mat->tex_shade;
    }

    glGenVertexArrays(1, &gpu->vao);
    glGenBuffers(1, &gpu->vbo);
    glGenBuffers(1, &gpu->ebo);
    glBindVertexArray(gpu->vao);

    {
        const GLsizei stride = 16 * (GLsizei)sizeof(float);

        glBindBuffer(GL_ARRAY_BUFFER, gpu->vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     (GLsizeiptr)(mesh->vertex_count * 16 * sizeof(float)),
                     mesh->vertices,
                     mesh->morph_target_count > 0 ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void *)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                              (void *)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
                              (void *)(6 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride,
                              (void *)(8 * sizeof(float)));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride,
                              (void *)(12 * sizeof(float)));
        glEnableVertexAttribArray(4);
    }

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu->ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 (GLsizeiptr)(mesh->index_count * sizeof(uint32_t)),
                 mesh->indices, GL_STATIC_DRAW);

    gpu->index_count = mesh->index_count;
    gpu->vertex_count = mesh->vertex_count;

    if (tex_main >= 0 && model != NULL &&
        (uint32_t)tex_main < model->texture_count) {
        gpu->tex_id = mtoon_upload_texture(&model->textures[tex_main]);
        gpu->has_texture = (gpu->tex_id != 0) ? 1 : 0;
    }
    if (tex_shade >= 0 && model != NULL &&
        (uint32_t)tex_shade < model->texture_count) {
        if (tex_shade == tex_main && gpu->tex_id != 0) {
            gpu->tex_shade = gpu->tex_id;
            gpu->has_shade_tex = gpu->has_texture;
        } else {
            gpu->tex_shade = mtoon_upload_texture(&model->textures[tex_shade]);
            gpu->has_shade_tex = (gpu->tex_shade != 0) ? 1 : 0;
        }
    }

    glBindVertexArray(0);
}
