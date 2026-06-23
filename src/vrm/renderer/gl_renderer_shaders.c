/**
 * @file gl_renderer_shaders.c
 * @brief VRM renderer submodule (split from gl_renderer.c)
 * @version 0.1
 * @date 2025-06-23
 * @copyright Copyright (c) Tuya Inc. All Rights Reserved.
 */
#include "vrm_renderer.h"
#include "gl_renderer_internal.h"
#include "vrm_loader.h"
#include "mat4_util.h"
#include "vrm_emotion.h"
#include "vrm_spring_bone.h"
#include "vrm_lip_sync.h"
#include "vrm_skybox.h"
#include "vrm_overlay.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <dirent.h>
#include <string.h>
#include <libgen.h>

#include "tuya_kconfig.h"
#include "svc_ai_player.h"
#include "mtoon_shaders.h"

/* ================================================================== */
/*  Shader sources                                                     */
/* ================================================================== */

const char *s_model_vs =
    "#version 140\n"
    "#extension GL_ARB_explicit_attrib_location : enable\n"
    "layout(location=0) in vec3 a_pos;\n"
    "layout(location=1) in vec3 a_normal;\n"
    "layout(location=2) in vec2 a_uv;\n"
    "layout(location=3) in vec4 a_bone_ids;\n"
    "layout(location=4) in vec4 a_bone_weights;\n"
    "\n"
    "uniform mat4 u_mvp;\n"
    "uniform mat4 u_model;\n"
    "uniform int  u_skinned;\n"
    "uniform samplerBuffer u_bone_tbo;\n"
    "\n"
    "out vec2 v_uv;\n"
    "out vec3 v_normal;\n"
    "\n"
    "mat4 getBoneMatrix(int idx) {\n"
    "    int base = idx * 4;\n"
    "    return mat4(\n"
    "        texelFetch(u_bone_tbo, base + 0),\n"
    "        texelFetch(u_bone_tbo, base + 1),\n"
    "        texelFetch(u_bone_tbo, base + 2),\n"
    "        texelFetch(u_bone_tbo, base + 3)\n"
    "    );\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    vec4 pos = vec4(a_pos, 1.0);\n"
    "    vec4 norm4 = vec4(a_normal, 0.0);\n"
    "\n"
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
    "\n"
    "    gl_Position = u_mvp * pos;\n"
    "    v_uv        = a_uv;\n"
    "    v_normal    = normalize(mat3(u_model) * norm4.xyz);\n"
    "}\n";

const char *s_model_fs =
    "#version 140\n"
    "in vec2 v_uv;\n"
    "in vec3 v_normal;\n"
    "out vec4 frag_color;\n"
    "uniform vec4      u_base_color;\n"
    "uniform sampler2D u_texture;\n"
    "uniform int       u_has_texture;\n"
    "uniform vec3      u_light_dir;\n"
    "void main() {\n"
    "    vec4 base;\n"
    "    if (u_has_texture != 0) {\n"
    "        base = texture(u_texture, v_uv) * u_base_color;\n"
    "    } else {\n"
    "        base = u_base_color;\n"
    "    }\n"
    "    if (base.a < 0.1) discard;\n"
    "\n"
    "    vec3 n = normalize(v_normal);\n"
    "    float NdotL = dot(n, u_light_dir);\n"
    "    float lit = smoothstep(-0.02, 0.05, NdotL - 0.1);\n"
    "    vec3 shadow = base.rgb * vec3(0.62, 0.58, 0.72);\n"
    "    frag_color = vec4(mix(shadow, base.rgb, lit), base.a);\n"
    "}\n";

/* ---- grid shader ---- */
const char *s_grid_vs =
    "#version 140\n"
    "#extension GL_ARB_explicit_attrib_location : enable\n"
    "layout(location=0) in vec3 a_pos;\n"
    "uniform mat4 u_mvp;\n"
    "out vec3 v_pos;\n"
    "void main() {\n"
    "    gl_Position = u_mvp * vec4(a_pos, 1.0);\n"
    "    v_pos = a_pos;\n"
    "}\n";

const char *s_grid_fs =
    "#version 140\n"
    "in vec3 v_pos;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    "    float dist  = length(v_pos.xz) / 16.0;\n"
    "    float alpha = clamp(1.0 - dist * 0.7, 0.0, 1.0);\n"
    "    frag_color  = vec4(0.35, 0.40, 0.55, alpha * 0.35);\n"
    "}\n";

/* ---- background gradient shader ---- */
const char *s_bg_vs =
    "#version 140\n"
    "#extension GL_ARB_explicit_attrib_location : enable\n"
    "layout(location=0) in vec2 a_pos;\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_pos, 0.999, 1.0);\n"
    "    v_uv = a_pos * 0.5 + 0.5;\n"
    "}\n";

const char *s_bg_fs =
    "#version 140\n"
    "in vec2 v_uv;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    "    vec3 col_bot = vec3(0.08, 0.08, 0.14);\n"
    "    vec3 col_top = vec3(0.12, 0.13, 0.22);\n"
    "    vec3 bg = mix(col_bot, col_top, v_uv.y);\n"
    "    frag_color = vec4(bg, 1.0);\n"
    "}\n";

/* ---- shadow depth shader (renders model from light's view) ---- */
const char *s_shadow_vs =
    "#version 140\n"
    "#extension GL_ARB_explicit_attrib_location : enable\n"
    "layout(location=0) in vec3 a_pos;\n"
    "layout(location=3) in vec4 a_bone_ids;\n"
    "layout(location=4) in vec4 a_bone_weights;\n"
    "uniform mat4 u_light_mvp;\n"
    "uniform int  u_skinned;\n"
    "uniform samplerBuffer u_bone_tbo;\n"
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
    "    if (u_skinned != 0) {\n"
    "        mat4 skin = mat4(0.0);\n"
    "        ivec4 ids = ivec4(a_bone_ids);\n"
    "        skin += a_bone_weights.x * getBoneMatrix(ids.x);\n"
    "        skin += a_bone_weights.y * getBoneMatrix(ids.y);\n"
    "        skin += a_bone_weights.z * getBoneMatrix(ids.z);\n"
    "        skin += a_bone_weights.w * getBoneMatrix(ids.w);\n"
    "        pos = skin * pos;\n"
    "    }\n"
    "    gl_Position = u_light_mvp * pos;\n"
    "}\n";

const char *s_shadow_fs =
    "#version 140\n"
    "void main() {}\n";

/* ---- ground shadow receiver shader ---- */
const char *s_ground_shadow_vs =
    "#version 140\n"
    "#extension GL_ARB_explicit_attrib_location : enable\n"
    "layout(location=0) in vec3 a_pos;\n"
    "uniform mat4 u_mvp;\n"
    "uniform mat4 u_light_vp;\n"
    "uniform mat4 u_ground_mat;\n"
    "out vec4 v_light_pos;\n"
    "out vec3 v_world;\n"
    "void main() {\n"
    "    vec4 world = u_ground_mat * vec4(a_pos, 1.0);\n"
    "    gl_Position = u_mvp * vec4(a_pos, 1.0);\n"
    "    v_light_pos = u_light_vp * world;\n"
    "    v_world = world.xyz;\n"
    "}\n";

const char *s_ground_shadow_fs =
    "#version 140\n"
    "in vec4 v_light_pos;\n"
    "in vec3 v_world;\n"
    "out vec4 frag_color;\n"
    "uniform sampler2D u_shadow_map;\n"
    "uniform vec3 u_center;\n"
    "uniform float u_radius;\n"
    "void main() {\n"
    "    vec3 proj = v_light_pos.xyz / v_light_pos.w;\n"
    "    proj = proj * 0.5 + 0.5;\n"
    "    if (proj.x < 0.0 || proj.x > 1.0 ||\n"
    "        proj.y < 0.0 || proj.y > 1.0 ||\n"
    "        proj.z > 1.0) discard;\n"
    "    float shadow = 0.0;\n"
    "    vec2 texel = 1.0 / vec2(textureSize(u_shadow_map, 0));\n"
    "    float bias = 0.003;\n"
    "    for (int x = -2; x <= 2; x++) {\n"
    "        for (int y = -2; y <= 2; y++) {\n"
    "            float d = texture(u_shadow_map, proj.xy + vec2(x,y)*texel).r;\n"
    "            shadow += (proj.z - bias > d) ? 1.0 : 0.0;\n"
    "        }\n"
    "    }\n"
    "    shadow /= 25.0;\n"
    "    if (shadow < 0.01) discard;\n"
    "    float dist = length(v_world.xz - u_center.xz) / u_radius;\n"
    "    float fade = smoothstep(1.0, 0.2, dist);\n"
    "    frag_color = vec4(0.0, 0.0, 0.05, shadow * 0.45 * fade);\n"
    "}\n";

/* ---- inverted-hull outline shader ---- */
static const char *s_outline_vs =
    "#version 140\n"
    "#extension GL_ARB_explicit_attrib_location : enable\n"
    "layout(location=0) in vec3 a_pos;\n"
    "layout(location=1) in vec3 a_normal;\n"
    "layout(location=3) in vec4 a_bone_ids;\n"
    "layout(location=4) in vec4 a_bone_weights;\n"
    "\n"
    "uniform mat4 u_mvp;\n"
    "uniform int  u_skinned;\n"
    "uniform float u_outline_width;\n"
    "uniform samplerBuffer u_bone_tbo;\n"
    "\n"
    "mat4 getBoneMatrix(int idx) {\n"
    "    int base = idx * 4;\n"
    "    return mat4(\n"
    "        texelFetch(u_bone_tbo, base + 0),\n"
    "        texelFetch(u_bone_tbo, base + 1),\n"
    "        texelFetch(u_bone_tbo, base + 2),\n"
    "        texelFetch(u_bone_tbo, base + 3)\n"
    "    );\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    vec4 pos = vec4(a_pos, 1.0);\n"
    "    vec4 norm4 = vec4(a_normal, 0.0);\n"
    "\n"
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
    "\n"
    "    vec4 clip = u_mvp * pos;\n"
    "    vec3 cn = mat3(u_mvp) * normalize(norm4.xyz);\n"
    "    vec2 d = normalize(cn.xy) * u_outline_width * clip.w;\n"
    "    clip.xy += d;\n"
    "    gl_Position = clip;\n"
    "}\n";

static const char *s_outline_fs =
    "#version 140\n"
    "out vec4 frag_color;\n"
    "uniform vec4 u_outline_color;\n"
    "void main() {\n"
    "    frag_color = u_outline_color;\n"
    "}\n";

/* ================================================================== */
/*  GL helpers                                                         */
/* ================================================================== */

static GLuint __compile_shader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    int ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "[gl] shader compile error:\n%s\n", log);
    }
    return s;
}

GLuint __link_program(const char *vs_src, const char *fs_src)
{
    GLuint vs = __compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = __compile_shader(GL_FRAGMENT_SHADER, fs_src);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    int ok;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        fprintf(stderr, "[gl] program link error:\n%s\n", log);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

void __upload_mesh(gpu_mesh_t *gpu, const vrm_mesh_t *mesh,
                   const vrm_model_t *model, uint32_t mesh_index)
{
    mtoon_upload_mesh(gpu, mesh, model, mesh_index);
}
