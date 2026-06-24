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
    "out vec3 v_world_pos;\n"
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
    "    vec4 world = u_model * pos;\n"
    "    gl_Position = u_mvp * pos;\n"
    "    v_uv = a_uv;\n"
    "    v_world_pos = world.xyz;\n"
    "    v_normal = normalize(mat3(u_model) * norm4.xyz);\n"
    "}\n";

const char *s_mtoon_fs =
    "#version 140\n"
    "in vec2 v_uv;\n"
    "in vec3 v_normal;\n"
    "in vec3 v_world_pos;\n"
    "out vec4 frag_color;\n"
    "uniform vec4 u_color;\n"
    "uniform vec4 u_shade_color;\n"
    "uniform vec4 u_emission_color;\n"
    "uniform vec4 u_rim_color;\n"
    "uniform sampler2D u_main_tex;\n"
    "uniform sampler2D u_shade_tex;\n"
    "uniform sampler2D u_bump_tex;\n"
    "uniform sampler2D u_emission_tex;\n"
    "uniform sampler2D u_rim_tex;\n"
    "uniform sampler2D u_matcap_tex;\n"
    "uniform int u_has_main;\n"
    "uniform int u_has_shade;\n"
    "uniform int u_has_bump;\n"
    "uniform int u_has_emission;\n"
    "uniform int u_has_rim;\n"
    "uniform int u_has_matcap;\n"
    "uniform float u_shade_shift;\n"
    "uniform float u_shade_toony;\n"
    "uniform float u_bump_scale;\n"
    "uniform float u_rim_fresnel_power;\n"
    "uniform float u_rim_lift;\n"
    "uniform float u_rim_lighting_mix;\n"
    "uniform float u_cutoff;\n"
    "uniform int u_blend_mode;\n"
    "uniform int u_is_unlit;\n"
    "uniform vec3 u_light_dir;\n"
    "uniform vec3 u_camera_pos;\n"
    "vec3 mtoon_perturb_normal(vec2 uv, vec3 n, vec3 pos, float scale) {\n"
    "    vec3 dp1 = dFdx(pos);\n"
    "    vec3 dp2 = dFdy(pos);\n"
    "    vec2 duv1 = dFdx(uv);\n"
    "    vec2 duv2 = dFdy(uv);\n"
    "    vec3 t = dp2 * duv1.x - dp1 * duv2.x;\n"
    "    vec3 b = dp2 * duv1.y - dp1 * duv2.y;\n"
    "    float invmax = inversesqrt(max(dot(t, t), dot(b, b)));\n"
    "    t *= invmax;\n"
    "    b *= invmax;\n"
    "    vec3 nm = texture(u_bump_tex, uv).xyz * 2.0 - 1.0;\n"
    "    nm.xy *= scale;\n"
    "    return normalize(mat3(t, b, n) * nm);\n"
    "}\n"
    "vec3 mtoon_matcap(vec3 world_normal, vec3 world_pos) {\n"
    "    if (u_has_matcap == 0) {\n"
    "        return vec3(0.0);\n"
    "    }\n"
    "    vec3 world_view = normalize(u_camera_pos - world_pos);\n"
    "    vec3 world_camera_up = vec3(0.0, 1.0, 0.0);\n"
    "    vec3 world_view_up = normalize(world_camera_up\n"
    "        - world_view * dot(world_view, world_camera_up));\n"
    "    vec3 world_view_right = normalize(cross(world_view, world_view_up));\n"
    "    vec2 matcap_uv = vec2(dot(world_view_right, world_normal),\n"
    "                          dot(world_view_up, world_normal)) * 0.5 + 0.5;\n"
    "    return texture(u_matcap_tex, matcap_uv).rgb;\n"
    "}\n"
    "void main() {\n"
    "    vec4 main_c = (u_has_main != 0)\n"
    "        ? texture(u_main_tex, v_uv) * u_color : u_color;\n"
    "    vec4 shade_c = (u_has_shade != 0)\n"
    "        ? texture(u_shade_tex, v_uv) * u_shade_color : u_shade_color;\n"
    "    if (u_blend_mode == 1 && main_c.a < u_cutoff) discard;\n"
    "    if (u_blend_mode >= 2 && main_c.a < 0.01) discard;\n"
    "    vec3 n = normalize(v_normal);\n"
    "    vec3 n_shade = n;\n"
    "    vec3 l = normalize(u_light_dir);\n"
    "    vec3 rgb;\n"
    "    if (u_is_unlit != 0) {\n"
    "        /* KHR_materials_unlit: main albedo only, no shade tex / toony ramp.\n"
    "         * Soft warm fake-light using _ShadeColor tint on the shadow side. */\n"
    "        float nl = dot(n_shade, l);\n"
    "        float lit = smoothstep(-0.02, 0.05, nl - 0.1);\n"
    "        vec3 warm_shadow = main_c.rgb * u_shade_color.rgb;\n"
    "        rgb = mix(warm_shadow, main_c.rgb, lit);\n"
    "    } else {\n"
    "        if (u_has_bump != 0) {\n"
    "            n_shade = mtoon_perturb_normal(v_uv, n, v_world_pos, u_bump_scale);\n"
    "        }\n"
    "        float dot_nl = dot(n_shade, l);\n"
    "        float grad = dot_nl * u_shade_toony + u_shade_shift;\n"
    "        grad = clamp(grad, 0.0, 1.0);\n"
    "        rgb = mix(shade_c.rgb, main_c.rgb, grad);\n"
    "        vec3 view_dir = normalize(u_camera_pos - v_world_pos);\n"
    "        float rim_term = 1.0 - max(dot(n_shade, view_dir), 0.0) + u_rim_lift;\n"
    "        rim_term = pow(max(rim_term, 0.00001), max(u_rim_fresnel_power, 0.00001));\n"
    "        vec3 rim_tex = (u_has_rim != 0) ? texture(u_rim_tex, v_uv).rgb : vec3(1.0);\n"
    "        vec3 rim = rim_term * u_rim_color.rgb * rim_tex;\n"
    "        vec3 rim_light = mix(vec3(1.0), rgb, u_rim_lighting_mix);\n"
    "        rgb += rim * rim_light;\n"
    "    }\n"
    "    rgb += mtoon_matcap(n_shade, v_world_pos);\n"
    "    vec3 emit_tex = (u_has_emission != 0)\n"
    "        ? texture(u_emission_tex, v_uv).rgb : vec3(1.0);\n"
    "    rgb += emit_tex * u_emission_color.rgb;\n"
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

/**
 * @brief Upload one material texture, reusing an existing GL id when indices match.
 * @param[in] model         Loaded model.
 * @param[in] tex_idx       Resolved texture index.
 * @param[in] reuse_ids     Optional existing ids to deduplicate (may be NULL).
 * @param[in] reuse_count   Number of entries in reuse_ids.
 * @return GL texture id, or 0 if unavailable.
 */
static GLuint __upload_material_texture(const vrm_model_t *model,
                                        int tex_idx,
                                        const GLuint *reuse_ids,
                                        const int *reuse_indices,
                                        int reuse_count)
{
    int i;

    if (tex_idx < 0 || model == NULL
        || (uint32_t)tex_idx >= model->texture_count) {
        return 0;
    }

    if (reuse_ids != NULL && reuse_indices != NULL) {
        for (i = 0; i < reuse_count; i++) {
            if (reuse_indices[i] == tex_idx && reuse_ids[i] != 0) {
                return reuse_ids[i];
            }
        }
    }

    return mtoon_upload_texture(&model->textures[tex_idx]);
}

/**
 * @brief Delete one GL texture if it is not shared with other slots.
 * @param[in,out] tex_id   Texture id pointer.
 * @param[in]     owned    1 if this slot owns the texture.
 * @return none
 */
static void __free_owned_texture(GLuint *tex_id, int owned)
{
    if (tex_id == NULL || *tex_id == 0) {
        return;
    }
    if (owned) {
        glDeleteTextures(1, tex_id);
    }
    *tex_id = 0;
}

void mtoon_free_mesh_textures(gpu_mesh_t *gpu)
{
    GLuint shared[6];
    int    shared_owned[6];
    int    i;

    if (gpu == NULL) {
        return;
    }

    shared[0] = gpu->tex_id;
    shared[1] = gpu->tex_shade;
    shared[2] = gpu->tex_bump;
    shared[3] = gpu->tex_emission;
    shared[4] = gpu->tex_rim;
    shared[5] = gpu->tex_matcap;

    for (i = 0; i < 6; i++) {
        shared_owned[i] = 1;
    }

    for (i = 0; i < 6; i++) {
        int j;
        for (j = 0; j < i; j++) {
            if (shared[i] != 0 && shared[i] == shared[j]) {
                shared_owned[i] = 0;
                break;
            }
        }
    }

    if (shared_owned[0]) {
        __free_owned_texture(&gpu->tex_id, 1);
    } else {
        gpu->tex_id = 0;
    }
    if (shared_owned[1]) {
        __free_owned_texture(&gpu->tex_shade, 1);
    } else {
        gpu->tex_shade = 0;
    }
    if (shared_owned[2]) {
        __free_owned_texture(&gpu->tex_bump, 1);
    } else {
        gpu->tex_bump = 0;
    }
    if (shared_owned[3]) {
        __free_owned_texture(&gpu->tex_emission, 1);
    } else {
        gpu->tex_emission = 0;
    }
    if (shared_owned[4]) {
        __free_owned_texture(&gpu->tex_rim, 1);
    } else {
        gpu->tex_rim = 0;
    }
    if (shared_owned[5]) {
        __free_owned_texture(&gpu->tex_matcap, 1);
    } else {
        gpu->tex_matcap = 0;
    }

    if (gpu->tex_outline_width != 0) {
        int outline_shared = 0;
        for (i = 0; i < 6; i++) {
            if (gpu->tex_outline_width == shared[i]) {
                outline_shared = 1;
                break;
            }
        }
        if (!outline_shared) {
            glDeleteTextures(1, &gpu->tex_outline_width);
        }
    }

    gpu->tex_outline_width = 0;
    gpu->has_texture = 0;
    gpu->has_shade_tex = 0;
    gpu->has_bump_tex = 0;
    gpu->has_emission_tex = 0;
    gpu->has_rim_tex = 0;
    gpu->has_matcap_tex = 0;
    gpu->has_outline_width_tex = 0;
}

void mtoon_upload_mesh(gpu_mesh_t *gpu, const vrm_mesh_t *mesh,
                       const vrm_model_t *model, uint32_t mesh_index)
{
    const vrm_material_t *mat;
    int                   tex_main = mesh->texture_index;
    int                   tex_shade = -1;
    int                   tex_bump = -1;
    int                   tex_emission = -1;
    int                   tex_rim = -1;
    int                   tex_matcap = -1;
    int                   tex_outline_w = -1;
    GLuint                reuse_ids[6];
    int                   reuse_indices[6];

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
        tex_bump = mat->tex_bump;
        tex_emission = mat->tex_emission;
        tex_rim = mat->tex_rim;
        tex_matcap = mat->tex_matcap;
        tex_outline_w = mat->tex_outline_width;
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

    gpu->tex_id = __upload_material_texture(model, tex_main, NULL, NULL, 0);
    gpu->has_texture = (gpu->tex_id != 0) ? 1 : 0;

    reuse_ids[0] = gpu->tex_id;
    reuse_indices[0] = tex_main;

    gpu->tex_shade = __upload_material_texture(model, tex_shade,
                                               reuse_ids, reuse_indices, 1);
    gpu->has_shade_tex = (gpu->tex_shade != 0) ? 1 : 0;
    reuse_ids[1] = gpu->tex_shade;
    reuse_indices[1] = tex_shade;

    gpu->tex_bump = __upload_material_texture(model, tex_bump,
                                              reuse_ids, reuse_indices, 2);
    gpu->has_bump_tex = (gpu->tex_bump != 0) ? 1 : 0;
    reuse_ids[2] = gpu->tex_bump;
    reuse_indices[2] = tex_bump;

    gpu->tex_emission = __upload_material_texture(model, tex_emission,
                                                  reuse_ids, reuse_indices, 3);
    gpu->has_emission_tex = (gpu->tex_emission != 0) ? 1 : 0;
    reuse_ids[3] = gpu->tex_emission;
    reuse_indices[3] = tex_emission;

    gpu->tex_rim = __upload_material_texture(model, tex_rim,
                                             reuse_ids, reuse_indices, 4);
    gpu->has_rim_tex = (gpu->tex_rim != 0) ? 1 : 0;
    reuse_ids[4] = gpu->tex_rim;
    reuse_indices[4] = tex_rim;

    gpu->tex_matcap = __upload_material_texture(model, tex_matcap,
                                                reuse_ids, reuse_indices, 5);
    gpu->has_matcap_tex = (gpu->tex_matcap != 0) ? 1 : 0;
    reuse_ids[5] = gpu->tex_matcap;
    reuse_indices[5] = tex_matcap;

    gpu->tex_outline_width = __upload_material_texture(model, tex_outline_w,
                                                       reuse_ids, reuse_indices, 6);
    gpu->has_outline_width_tex = (gpu->tex_outline_width != 0) ? 1 : 0;

    glBindVertexArray(0);
}
