/**
 * @file overlay_panel.c
 * @brief Settings overlay submodule (split from settings_overlay.c)
 * @version 0.1
 * @date 2025-06-23
 * @copyright Copyright (c) Tuya Inc. All Rights Reserved.
 */
#include "vrm_overlay.h"
#include "overlay_internal.h"

#include <GL/glew.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>


/* ---------------------------------------------------------------------------
 * GL resource management
 * --------------------------------------------------------------------------- */

static GLuint __compile(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    int ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "[ovl] shader error: %s\n", log);
    }
    return s;
}

void __create_font_texture(void)
{
    unsigned char atlas[48][128];
    memset(atlas, 0, sizeof(atlas));
    for (int ci = 0; ci < 95; ci++) {
        int col = ci % 16;
        int row = ci / 16;
        for (int r = 0; r < 8; r++) {
            unsigned char bits = FONT_8X8[ci][r];
            for (int c = 0; c < 8; c++) {
                if (bits & (0x80 >> c)) {
                    atlas[row * 8 + r][col * 8 + c] = 255;
                }
            }
        }
    }
    glGenTextures(1, &s_font_tex);
    glBindTexture(GL_TEXTURE_2D, s_font_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 128, 48, 0,
                 GL_RED, GL_UNSIGNED_BYTE, atlas);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void __init_gl(void)
{
    GLuint vs = __compile(GL_VERTEX_SHADER, OVL_VS);
    GLuint fs = __compile(GL_FRAGMENT_SHADER, OVL_FS);
    s_prog = glCreateProgram();
    glAttachShader(s_prog, vs);
    glAttachShader(s_prog, fs);
    glBindAttribLocation(s_prog, 0, "a_pos");
    glBindAttribLocation(s_prog, 1, "a_uv");
    glBindAttribLocation(s_prog, 2, "a_col");
    glLinkProgram(s_prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    s_u_proj    = glGetUniformLocation(s_prog, "u_proj");
    s_u_tex     = glGetUniformLocation(s_prog, "u_tex");
    s_u_use_tex = glGetUniformLocation(s_prog, "u_use_tex");

    glGenVertexArrays(1, &s_vao);
    glGenBuffers(1, &s_vbo);
    glBindVertexArray(s_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(s_verts), NULL, GL_DYNAMIC_DRAW);

    const GLsizei stride = sizeof(ovl_vert_t);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void *)(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void *)(4 * sizeof(float)));

    glBindVertexArray(0);

    __create_font_texture();
}

/* ---------------------------------------------------------------------------
 * Rendering — two-pass geometry build (rects first, then text)
 * --------------------------------------------------------------------------- */

float __content_height(void)
{
    float h = OVL_PAD;
    h += OVL_SECTION_H + OVL_SEP_H;
    h += OVL_SECTION_H + (s_model_count > 0 ? s_model_count : 1) * OVL_ITEM_H + OVL_SEP_H;
    h += OVL_SECTION_H + (s_anim_count > 0 ? s_anim_count : 1) * OVL_ITEM_H + OVL_SEP_H;
    h += OVL_SECTION_H + (1 + (s_scene_count > 0 ? s_scene_count : 0)) * OVL_ITEM_H + OVL_SEP_H;
    h += OVL_ITEM_H * 4 + OVL_SEP_H;
    h += OVL_ITEM_H + OVL_PAD;
    return h;
}

/**
 * @brief Shared layout walker — computes Y for each item in the panel.
 *
 * @param[in] pass  0 = push colored rects only, 1 = push text only.
 * @param[in] win_w Window width.
 * @param[in] win_h Window height.
 * @return none
 */
void __push_separator(float y, float pw)
{
    __push_rect(OVL_PAD, y + OVL_SEP_H * 0.5f - 0.5f,
                pw - OVL_PAD * 2, 1.0f, 0.28f, 0.28f, 0.38f, 0.5f);
}

void __walk_panel(int pass, int win_w, int win_h)
{
    float pw = OVL_PANEL_W;
    float ph = (float)win_h;
    int mc = (int)((pw - OVL_PAD * 2) / OVL_CHAR_W);
    int mc_item = mc - 2;
    float ix = OVL_PAD + 2 * OVL_CHAR_W;

    if (pass == 0) {
        __push_rect(0, 0, pw, ph, 0.06f, 0.06f, 0.12f, 0.94f);
        __push_rect(pw - OVL_BORDER_W, 0, OVL_BORDER_W, ph,
                    0.30f, 0.55f, 0.85f, 0.6f);
    }

    float y = OVL_PAD - s_scroll;

    /* Title */
    if (pass == 1) {
        __push_text(OVL_PAD, y, "SETTINGS", mc, 0.40f, 0.72f, 1.0f, 1.0f);
    }
    y += OVL_SECTION_H;

    if (pass == 0) { __push_separator(y, pw); }
    y += OVL_SEP_H;

    /* --- Models --- */
    if (pass == 1) {
        __push_text(OVL_PAD, y, "> MODEL", mc, 0.94f, 0.72f, 0.20f, 1.0f);
    }
    y += OVL_SECTION_H;
    if (s_model_count == 0) {
        if (pass == 1) {
            __push_text(ix, y + 5, "(empty)", mc_item, 0.40f,0.40f,0.45f,1.0f);
        }
        y += OVL_ITEM_H;
    }
    for (int i = 0; i < s_model_count; i++) {
        int sel = (i == s_model_sel);
        if (pass == 0 && sel) {
            __push_rect(OVL_PAD, y + 1, pw - OVL_PAD * 2, OVL_ITEM_H - 2,
                        0.14f, 0.33f, 0.62f, 0.90f);
        }
        if (pass == 1) {
            if (sel) {
                __push_text(OVL_PAD, y + 5, ">", 0, 0.50f,0.80f,1.0f,1.0f);
                __push_text(ix, y + 5, s_models[i].name, mc_item, 1.0f,1.0f,1.0f,1.0f);
            } else {
                __push_text(ix, y + 5, s_models[i].name, mc_item, 0.68f,0.68f,0.72f,1.0f);
            }
        }
        y += OVL_ITEM_H;
    }

    if (pass == 0) { __push_separator(y, pw); }
    y += OVL_SEP_H;

    /* --- Animations --- */
    if (pass == 1) {
        __push_text(OVL_PAD, y, "> ANIMATION", mc, 0.94f, 0.72f, 0.20f, 1.0f);
    }
    y += OVL_SECTION_H;
    if (s_anim_count == 0) {
        if (pass == 1) {
            __push_text(ix, y + 5, "(empty)", mc_item, 0.40f,0.40f,0.45f,1.0f);
        }
        y += OVL_ITEM_H;
    }
    for (int i = 0; i < s_anim_count; i++) {
        int sel = (i == s_anim_sel);
        if (pass == 0 && sel) {
            __push_rect(OVL_PAD, y + 1, pw - OVL_PAD * 2, OVL_ITEM_H - 2,
                        0.14f, 0.33f, 0.62f, 0.90f);
        }
        if (pass == 1) {
            const char *n = (s_anim_names && s_anim_names[i]) ? s_anim_names[i] : "???";
            if (sel) {
                __push_text(OVL_PAD, y + 5, ">", 0, 0.50f,0.80f,1.0f,1.0f);
                __push_text(ix, y + 5, n, mc_item, 1.0f,1.0f,1.0f,1.0f);
            } else {
                __push_text(ix, y + 5, n, mc_item, 0.68f,0.68f,0.72f,1.0f);
            }
        }
        y += OVL_ITEM_H;
    }

    if (pass == 0) { __push_separator(y, pw); }
    y += OVL_SEP_H;

    /* --- Scenes --- */
    if (pass == 1) {
        __push_text(OVL_PAD, y, "> SCENE", mc, 0.94f, 0.72f, 0.20f, 1.0f);
    }
    y += OVL_SECTION_H;
    {
        int sel = (s_scene_sel < 0);
        if (pass == 0 && sel) {
            __push_rect(OVL_PAD, y + 1, pw - OVL_PAD * 2, OVL_ITEM_H - 2,
                        0.14f, 0.33f, 0.62f, 0.90f);
        }
        if (pass == 1) {
            if (sel) {
                __push_text(OVL_PAD, y + 5, ">", 0, 0.50f,0.80f,1.0f,1.0f);
                __push_text(ix, y + 5, "(none)", mc_item, 1.0f,1.0f,1.0f,1.0f);
            } else {
                __push_text(ix, y + 5, "(none)", mc_item, 0.68f,0.68f,0.72f,1.0f);
            }
        }
        y += OVL_ITEM_H;
    }
    for (int i = 0; i < s_scene_count; i++) {
        int sel = (i == s_scene_sel);
        if (pass == 0 && sel) {
            __push_rect(OVL_PAD, y + 1, pw - OVL_PAD * 2, OVL_ITEM_H - 2,
                        0.14f, 0.33f, 0.62f, 0.90f);
        }
        if (pass == 1) {
            if (sel) {
                __push_text(OVL_PAD, y + 5, ">", 0, 0.50f,0.80f,1.0f,1.0f);
                __push_text(ix, y + 5, s_scenes[i].name, mc_item, 1.0f,1.0f,1.0f,1.0f);
            } else {
                __push_text(ix, y + 5, s_scenes[i].name, mc_item, 0.68f,0.68f,0.72f,1.0f);
            }
        }
        y += OVL_ITEM_H;
    }

    if (pass == 0) { __push_separator(y, pw); }
    y += OVL_SEP_H;

    /* --- Toggles --- */
    if (pass == 0 && s_cam_locked) {
        __push_rect(OVL_PAD, y + 1, pw - OVL_PAD * 2, OVL_ITEM_H - 2,
                    0.12f, 0.28f, 0.22f, 0.60f);
    }
    if (pass == 1) {
        float cr, cg, cb;
        if (s_cam_locked) { cr = 0.30f; cg = 0.85f; cb = 0.65f; }
        else              { cr = 0.50f; cg = 0.50f; cb = 0.55f; }
        __push_text(OVL_PAD, y + 5,
                    s_cam_locked ? "[#] Lock Camera" : "[ ] Lock Camera",
                    mc, cr, cg, cb, 1.0f);
    }
    y += OVL_ITEM_H;

    if (pass == 0 && s_sub_enabled) {
        __push_rect(OVL_PAD, y + 1, pw - OVL_PAD * 2, OVL_ITEM_H - 2,
                    0.12f, 0.28f, 0.22f, 0.60f);
    }
    if (pass == 1) {
        float cr, cg, cb;
        if (s_sub_enabled) { cr = 0.30f; cg = 0.85f; cb = 0.65f; }
        else               { cr = 0.50f; cg = 0.50f; cb = 0.55f; }
        __push_text(OVL_PAD, y + 5,
                    s_sub_enabled ? "[#] Subtitle" : "[ ] Subtitle",
                    mc, cr, cg, cb, 1.0f);
    }
    y += OVL_ITEM_H;

    if (pass == 0 && s_fullscreen) {
        __push_rect(OVL_PAD, y + 1, pw - OVL_PAD * 2, OVL_ITEM_H - 2,
                    0.12f, 0.28f, 0.22f, 0.60f);
    }
    if (pass == 1) {
        float cr, cg, cb;
        if (s_fullscreen) { cr = 0.30f; cg = 0.85f; cb = 0.65f; }
        else              { cr = 0.50f; cg = 0.50f; cb = 0.55f; }
        __push_text(OVL_PAD, y + 5,
                    s_fullscreen ? "[#] Fullscreen" : "[ ] Fullscreen",
                    mc, cr, cg, cb, 1.0f);
    }
    y += OVL_ITEM_H;

    if (pass == 0 && s_spectator) {
        __push_rect(OVL_PAD, y + 1, pw - OVL_PAD * 2, OVL_ITEM_H - 2,
                    0.12f, 0.28f, 0.22f, 0.60f);
    }
    if (pass == 1) {
        float cr, cg, cb;
        if (s_spectator) { cr = 0.30f; cg = 0.85f; cb = 0.65f; }
        else             { cr = 0.50f; cg = 0.50f; cb = 0.55f; }
        __push_text(OVL_PAD, y + 5,
                    s_spectator ? "[#] Spectator" : "[ ] Spectator",
                    mc, cr, cg, cb, 1.0f);
    }
    y += OVL_ITEM_H;

    if (pass == 0) { __push_separator(y, pw); }
    y += OVL_SEP_H;

    /* --- Close button --- */
    if (pass == 0) {
        __push_rect(OVL_PAD, y + 2, pw - OVL_PAD * 2, OVL_ITEM_H - 4,
                    0.20f, 0.28f, 0.38f, 1.0f);
    }
    if (pass == 1) {
        float tw = 9.0f * OVL_CHAR_W;
        __push_text((pw - tw) * 0.5f, y + 5, "[ Close ]", 0,
                    0.85f, 0.88f, 0.95f, 1.0f);
    }
}

void __walk_subtitle(int pass, int win_w, int win_h)
{
    if (!s_sub_enabled || s_subtitle[0] == '\0') {
        return;
    }
    float box_x = (float)OVL_SUB_MARGIN_X;
    float box_w = (float)(win_w - OVL_SUB_MARGIN_X * 2);
    float bar_y = (float)win_h - OVL_SUBTITLE_H - OVL_SUB_BOTTOM_GAP;
    if (box_w <= 0.0f) {
        return;
    }
    if (pass == 0) {
        __push_rect(box_x, bar_y, box_w, OVL_SUBTITLE_H,
                    0.03f, 0.03f, 0.08f, 0.78f);
        __push_rect(box_x, bar_y, box_w, 1.0f,
                    0.30f, 0.55f, 0.85f, 0.42f);
    }
    if (pass == 1) {
        if (__subtitle_font_init()) {
            return;
        }
        char ascii[OVL_SUBTITLE_MAX];
        int len = __subtitle_ascii_fallback(ascii, sizeof(ascii), s_subtitle);
        int max_c_per_line = (int)((box_w - OVL_SUB_INNER_PAD_X * 2) / OVL_CHAR_W);
        if (max_c_per_line < 1) {
            return;
        }
        int max_chars = max_c_per_line * OVL_SUB_MAX_LINES;
        int start = len > max_chars ? (len - max_chars) : 0;
        float tx = box_x + OVL_SUB_INNER_PAD_X;
        float ty = bar_y + OVL_SUB_INNER_PAD_Y;
        for (int line = 0; line < OVL_SUB_MAX_LINES && start < len; line++) {
            __push_text(tx, ty + line * (OVL_CHAR_H + OVL_SUB_LINE_GAP),
                        ascii + start, max_c_per_line, 0.95f, 0.96f, 1.0f, 1.0f);
            start += max_c_per_line;
        }
    }
}

void __render_subtitle_texture(int win_w, int win_h)
{
    int box_x = OVL_SUB_MARGIN_X;
    int box_w = win_w - OVL_SUB_MARGIN_X * 2;
    int box_y = win_h - OVL_SUBTITLE_H - OVL_SUB_BOTTOM_GAP;
    int max_w = box_w - OVL_SUB_INNER_PAD_X * 2;
    if (max_w <= 0) {
        return;
    }
    if ((s_sub_dirty || s_sub_last_max_w != max_w) &&
        !__rebuild_subtitle_texture(max_w)) {
        return;
    }
    if (!s_sub_tex || s_sub_draw_w <= 0 || s_sub_draw_h <= 0) {
        return;
    }

    float x = (float)box_x + (float)OVL_SUB_INNER_PAD_X +
              ((float)max_w - (float)s_sub_draw_w) * 0.5f;
    if (x < (float)(box_x + OVL_SUB_INNER_PAD_X)) {
        x = (float)(box_x + OVL_SUB_INNER_PAD_X);
    }
    float content_h = (float)OVL_SUBTITLE_H - (float)(OVL_SUB_INNER_PAD_Y * 2);
    float y = (float)box_y + (float)OVL_SUB_INNER_PAD_Y +
              (content_h - (float)s_sub_draw_h) * 0.5f;

    ovl_vert_t quad[6];
    quad[0] = (ovl_vert_t){x, y, 0.0f, 0.0f, 0.95f, 0.96f, 1.0f, 1.0f};
    quad[1] = (ovl_vert_t){x + (float)s_sub_draw_w, y, 1.0f, 0.0f, 0.95f, 0.96f, 1.0f, 1.0f};
    quad[2] = (ovl_vert_t){x + (float)s_sub_draw_w, y + (float)s_sub_draw_h, 1.0f, 1.0f, 0.95f, 0.96f, 1.0f, 1.0f};
    quad[3] = (ovl_vert_t){x, y, 0.0f, 0.0f, 0.95f, 0.96f, 1.0f, 1.0f};
    quad[4] = (ovl_vert_t){x + (float)s_sub_draw_w, y + (float)s_sub_draw_h, 1.0f, 1.0f, 0.95f, 0.96f, 1.0f, 1.0f};
    quad[5] = (ovl_vert_t){x, y + (float)s_sub_draw_h, 0.0f, 1.0f, 0.95f, 0.96f, 1.0f, 1.0f};

    glBindTexture(GL_TEXTURE_2D, s_sub_tex);
    glBindVertexArray(s_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(quad), quad);
    glUniform1i(s_u_use_tex, 1);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

/* ---------------------------------------------------------------------------
 * GL state save / restore
 * --------------------------------------------------------------------------- */

void __save_gl(gl_saved_state_t *st)
{
    glGetIntegerv(GL_CURRENT_PROGRAM,      &st->prog);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &st->vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &st->vbo);
    glGetIntegerv(GL_ACTIVE_TEXTURE,       &st->active_tex);
    glGetIntegerv(GL_TEXTURE_BINDING_2D,   &st->tex_2d);
    glGetIntegerv(GL_BLEND_SRC_RGB,        &st->blend_src_rgb);
    glGetIntegerv(GL_BLEND_DST_RGB,        &st->blend_dst_rgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA,      &st->blend_src_a);
    glGetIntegerv(GL_BLEND_DST_ALPHA,      &st->blend_dst_a);
    glGetIntegerv(GL_VIEWPORT,              st->viewport);
    glGetIntegerv(GL_SCISSOR_BOX,           st->scissor_box);
    st->blend   = glIsEnabled(GL_BLEND);
    st->depth   = glIsEnabled(GL_DEPTH_TEST);
    st->cull    = glIsEnabled(GL_CULL_FACE);
    st->scissor = glIsEnabled(GL_SCISSOR_TEST);
}

void __restore_gl(const gl_saved_state_t *st)
{
    glUseProgram(st->prog);
    glBindVertexArray(st->vao);
    glBindBuffer(GL_ARRAY_BUFFER, st->vbo);
    glActiveTexture(st->active_tex);
    glBindTexture(GL_TEXTURE_2D, st->tex_2d);
    glBlendFuncSeparate(st->blend_src_rgb, st->blend_dst_rgb,
                        st->blend_src_a, st->blend_dst_a);
    glViewport(st->viewport[0], st->viewport[1],
               st->viewport[2], st->viewport[3]);
    glScissor(st->scissor_box[0], st->scissor_box[1],
              st->scissor_box[2], st->scissor_box[3]);
    if (st->blend)   glEnable(GL_BLEND);   else glDisable(GL_BLEND);
    if (st->depth)   glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (st->cull)    glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (st->scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
}

/* ---------------------------------------------------------------------------
 * Event handling
 * --------------------------------------------------------------------------- */

int __hit_panel_item(int mx, int my, int win_h,
                            int *section, int *index)
{
    if (mx < 0 || mx >= OVL_PANEL_W) {
        return 0;
    }

    float y = OVL_PAD - s_scroll;
    y += OVL_SECTION_H;    /* title */
    y += OVL_SEP_H;        /* separator */

    /* Models */
    *section = 0;
    y += OVL_SECTION_H;
    int mc = s_model_count > 0 ? s_model_count : 1;
    for (int i = 0; i < mc; i++) {
        if (my >= y && my < y + OVL_ITEM_H) {
            *index = (s_model_count > 0) ? i : -1;
            return 1;
        }
        y += OVL_ITEM_H;
    }
    y += OVL_SEP_H;

    /* Animations */
    *section = 1;
    y += OVL_SECTION_H;
    int ac = s_anim_count > 0 ? s_anim_count : 1;
    for (int i = 0; i < ac; i++) {
        if (my >= y && my < y + OVL_ITEM_H) {
            *index = (s_anim_count > 0) ? i : -1;
            return 1;
        }
        y += OVL_ITEM_H;
    }
    y += OVL_SEP_H;

    /* Scenes */
    *section = 2;
    y += OVL_SECTION_H;
    if (my >= y && my < y + OVL_ITEM_H) {
        *index = -1;
        return 1;
    }
    y += OVL_ITEM_H;
    for (int i = 0; i < s_scene_count; i++) {
        if (my >= y && my < y + OVL_ITEM_H) {
            *index = i;
            return 1;
        }
        y += OVL_ITEM_H;
    }
    y += OVL_SEP_H;

    /* Camera toggle */
    *section = 3;
    *index = 0;
    if (my >= y && my < y + OVL_ITEM_H) {
        return 1;
    }
    y += OVL_ITEM_H;

    /* Subtitle toggle */
    *section = 4;
    if (my >= y && my < y + OVL_ITEM_H) {
        return 1;
    }
    y += OVL_ITEM_H;

    /* Fullscreen toggle */
    *section = 5;
    if (my >= y && my < y + OVL_ITEM_H) {
        return 1;
    }
    y += OVL_ITEM_H;

    /* Spectator toggle */
    *section = 6;
    if (my >= y && my < y + OVL_ITEM_H) {
        return 1;
    }
    y += OVL_ITEM_H;
    y += OVL_SEP_H;

    /* Close button */
    *section = 7;
    if (my >= y && my < y + OVL_ITEM_H) {
        return 1;
    }

    return 0;
}
