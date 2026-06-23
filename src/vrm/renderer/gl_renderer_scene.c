/**
 * @file gl_renderer_scene.c
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

/* ---- ground grid ---- */
GLuint s_grid_vao, s_grid_vbo;
int    s_grid_vert_count;
GLuint s_bg_vao, s_bg_vbo;

/* ---- shadow map ---- */
GLuint s_shadow_fbo, s_shadow_depth_tex;
GLuint s_ground_vao, s_ground_vbo, s_ground_ebo;

void __create_grid(void)
{
    enum { DIVS = 40, MAX_VERTS = (DIVS + 1) * 4 };
    float buf[MAX_VERTS * 3];
    int n = 0;
    float half = 10.0f;
    float step = (half * 2.0f) / DIVS;

    for (int i = 0; i <= DIVS; i++) {
        float t = -half + i * step;
        buf[n++] = -half; buf[n++] = 0.0f; buf[n++] = t;
        buf[n++] =  half; buf[n++] = 0.0f; buf[n++] = t;
        buf[n++] = t; buf[n++] = 0.0f; buf[n++] = -half;
        buf[n++] = t; buf[n++] = 0.0f; buf[n++] =  half;
    }
    s_grid_vert_count = n / 3;

    glGenVertexArrays(1, &s_grid_vao);
    glGenBuffers(1, &s_grid_vbo);
    glBindVertexArray(s_grid_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_grid_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(n * sizeof(float)), buf, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * (GLsizei)sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

void __create_bg_quad(void)
{
    float verts[] = { -1,-1, 1,-1, -1,1, 1,1 };
    glGenVertexArrays(1, &s_bg_vao);
    glGenBuffers(1, &s_bg_vbo);
    glBindVertexArray(s_bg_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_bg_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

/**
 * @brief Create the shadow map FBO with a depth-only texture
 * @return none
 */
void __create_shadow_fbo(void)
{
    glGenTextures(1, &s_shadow_depth_tex);
    glBindTexture(GL_TEXTURE_2D, s_shadow_depth_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
                 SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float border[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &s_shadow_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, s_shadow_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                           GL_TEXTURE_2D, s_shadow_depth_tex, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "[gl] shadow FBO incomplete!\n");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

/**
 * @brief Create a unit XZ quad (Y=0, from -1..1) for ground shadow receiving
 * @return none
 */
void __create_ground_quad(void)
{
    float verts[] = {
        -1.0f, 0.0f, -1.0f,
         1.0f, 0.0f, -1.0f,
         1.0f, 0.0f,  1.0f,
        -1.0f, 0.0f,  1.0f,
    };
    uint32_t indices[] = { 0, 1, 2, 0, 2, 3 };

    glGenVertexArrays(1, &s_ground_vao);
    glGenBuffers(1, &s_ground_vbo);
    glGenBuffers(1, &s_ground_ebo);
    glBindVertexArray(s_ground_vao);

    glBindBuffer(GL_ARRAY_BUFFER, s_ground_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * (GLsizei)sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_ground_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glBindVertexArray(0);
}

/* ================================================================== */
/*  Public entry point                                                 */
/* ================================================================== */

/* Compare function for qsort of filename strings */
