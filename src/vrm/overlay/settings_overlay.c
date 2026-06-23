/**
 * @file settings_overlay.c
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


int vrm_overlay_init(const vrm_overlay_cfg_t *cfg)
{
    if (!cfg) {
        return -1;
    }

    s_model_dir[0] = '\0';
    s_scene_dir[0] = '\0';
    if (cfg->model_dir) {
        strncpy(s_model_dir, cfg->model_dir, OVL_MAX_PATH - 1);
        s_model_dir[OVL_MAX_PATH - 1] = '\0';
    }
    if (cfg->scene_parent_dir) {
        strncpy(s_scene_dir, cfg->scene_parent_dir, OVL_MAX_PATH - 1);
        s_scene_dir[OVL_MAX_PATH - 1] = '\0';
    }

    s_anim_names = cfg->anim_names;
    s_anim_count = cfg->anim_count;
    s_anim_sel   = cfg->active_anim;
    s_cam_locked   = cfg->camera_locked;
    s_sub_enabled  = cfg->subtitle_enabled;
    s_fullscreen   = cfg->fullscreen;
    s_spectator    = cfg->spectator;
    s_subtitle[0]  = '\0';
    __subtitle_texture_reset();
    s_scroll       = 0.0f;
    s_visible      = 0;

    s_cb_model = cfg->on_model_change;
    s_cb_anim  = cfg->on_anim_change;
    s_cb_scene = cfg->on_scene_change;
    s_cb_cam   = cfg->on_camera_lock;
    s_cb_sub   = cfg->on_subtitle;
    s_cb_fs    = cfg->on_fullscreen;
    s_cb_spec  = cfg->on_spectator;
    s_cb_ud    = cfg->user_data;

    __scan_dir(s_models, &s_model_count, OVL_MAX_FILES,
               s_model_dir, __is_model_ext, 0);
    __scan_dir(s_scenes, &s_scene_count, OVL_MAX_FILES,
               s_scene_dir, NULL, 1);

    s_model_sel = -1;
    if (cfg->current_model) {
        for (int i = 0; i < s_model_count; i++) {
            if (strcmp(s_models[i].name, cfg->current_model) == 0) {
                s_model_sel = i;
                break;
            }
        }
    }
    s_scene_sel = -1;
    if (cfg->current_scene && cfg->current_scene[0] != '\0') {
        for (int i = 0; i < s_scene_count; i++) {
            if (strcmp(s_scenes[i].name, cfg->current_scene) == 0) {
                s_scene_sel = i;
                break;
            }
        }
    }

    __init_gl();

    printf("[settings_overlay] init: %d models, %d scenes, %d anims\n",
           s_model_count, s_scene_count, s_anim_count);
    return 0;
}

void vrm_overlay_destroy(void)
{
    if (s_sub_tex)  { glDeleteTextures(1, &s_sub_tex); s_sub_tex = 0; }
    if (s_font_tex) { glDeleteTextures(1, &s_font_tex); s_font_tex = 0; }
    if (s_vbo)      { glDeleteBuffers(1, &s_vbo);       s_vbo = 0; }
    if (s_vao)      { glDeleteVertexArrays(1, &s_vao);  s_vao = 0; }
    if (s_prog)     { glDeleteProgram(s_prog);           s_prog = 0; }
    free(s_sub_pixels); s_sub_pixels = NULL; s_sub_pixels_cap = 0;
    __overlay_font_destroy();
}

int vrm_overlay_handle_event(const SDL_Event *ev, int win_w, int win_h)
{
    if (!ev) {
        return 0;
    }

    if (ev->type == SDL_MOUSEBUTTONDOWN && ev->button.button == SDL_BUTTON_LEFT) {
        if (__icon_hit(ev->button.x, ev->button.y)) {
            s_visible = !s_visible;
            s_scroll  = 0.0f;
            return 1;
        }
    }

    if (!s_visible) {
        return 0;
    }

    if (ev->type == SDL_MOUSEBUTTONDOWN && ev->button.button == SDL_BUTTON_LEFT) {
        int mx = ev->button.x;
        int my = ev->button.y;
        if (mx >= OVL_PANEL_W) {
            return 0;
        }
        int section = -1, index = -1;
        if (__hit_panel_item(mx, my, win_h, &section, &index)) {
            switch (section) {
            case 0:
                if (index >= 0 && index < s_model_count && index != s_model_sel) {
                    s_model_sel = index;
                    if (s_cb_model) {
                        s_cb_model(s_models[index].path, s_cb_ud);
                    }
                }
                break;
            case 1:
                if (index >= 0 && index < s_anim_count && index != s_anim_sel) {
                    s_anim_sel = index;
                    if (s_cb_anim) {
                        s_cb_anim(index, s_cb_ud);
                    }
                }
                break;
            case 2:
                if (index != s_scene_sel) {
                    s_scene_sel = index;
                    if (s_cb_scene) {
                        s_cb_scene(index < 0 ? "" : s_scenes[index].path, s_cb_ud);
                    }
                }
                break;
            case 3:
                s_cam_locked = !s_cam_locked;
                if (s_cb_cam) {
                    s_cb_cam(s_cam_locked, s_cb_ud);
                }
                break;
            case 4:
                s_sub_enabled = !s_sub_enabled;
                if (s_cb_sub) {
                    s_cb_sub(s_sub_enabled, s_cb_ud);
                }
                break;
            case 5:
                s_fullscreen = !s_fullscreen;
                if (s_cb_fs) {
                    s_cb_fs(s_fullscreen, s_cb_ud);
                }
                break;
            case 6:
                s_spectator = !s_spectator;
                if (s_cb_spec) {
                    s_cb_spec(s_spectator, s_cb_ud);
                }
                break;
            case 7:
                s_visible = 0;
                break;
            }
        }
        return 1;
    }

    if (ev->type == SDL_MOUSEWHEEL && s_visible) {
        int mx, my;
        SDL_GetMouseState(&mx, &my);
        if (mx < OVL_PANEL_W) {
            s_scroll -= ev->wheel.y * OVL_ITEM_H;
            float max_scroll = __content_height() - (float)win_h;
            if (max_scroll < 0) max_scroll = 0;
            if (s_scroll < 0) s_scroll = 0;
            if (s_scroll > max_scroll) s_scroll = max_scroll;
            return 1;
        }
    }

    if (ev->type == SDL_MOUSEBUTTONDOWN || ev->type == SDL_MOUSEMOTION) {
        int mx = (ev->type == SDL_MOUSEBUTTONDOWN) ? ev->button.x : ev->motion.x;
        if (mx < OVL_PANEL_W) {
            return 1;
        }
    }

    return 0;
}

void vrm_overlay_render(int win_w, int win_h)
{
    int has_subtitle = (s_sub_enabled && s_subtitle[0] != '\0');

    gl_saved_state_t saved;
    __save_gl(&saved);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(s_prog);

    float proj[16];
    memset(proj, 0, sizeof(proj));
    proj[0]  =  2.0f / (float)win_w;
    proj[5]  = -2.0f / (float)win_h;
    proj[10] = -1.0f;
    proj[12] = -1.0f;
    proj[13] =  1.0f;
    proj[15] =  1.0f;
    glUniformMatrix4fv(s_u_proj, 1, GL_FALSE, proj);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_font_tex);
    glUniform1i(s_u_tex, 0);

    s_vert_count = 0;

    /* Pass 0: colored rectangles */
    if (s_visible) {
        __walk_panel(0, win_w, win_h);
    } else {
        __draw_settings_icon();
    }
    __walk_subtitle(0, win_w, win_h);

    s_text_batch_start = s_vert_count;

    /* Pass 1: text quads */
    if (s_visible) {
        __walk_panel(1, win_w, win_h);
    }
    __walk_subtitle(1, win_w, win_h);

    if (s_vert_count == 0) {
        __restore_gl(&saved);
        return;
    }

    /* Upload vertex data */
    glBindVertexArray(s_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    (GLsizeiptr)(s_vert_count * sizeof(ovl_vert_t)), s_verts);

    /* Draw colored rects (no texture), then text (with font texture) */
    if (s_text_batch_start > 0) {
        glUniform1i(s_u_use_tex, 0);
        glDrawArrays(GL_TRIANGLES, 0, s_text_batch_start);
    }
    if (s_vert_count > s_text_batch_start) {
        glUniform1i(s_u_use_tex, 1);
        glDrawArrays(GL_TRIANGLES, s_text_batch_start,
                     s_vert_count - s_text_batch_start);
    }

    if (has_subtitle) {
        __render_subtitle_texture(win_w, win_h);
    }

    glBindVertexArray(0);

    __restore_gl(&saved);
}

void vrm_overlay_toggle(void)
{
    s_visible = !s_visible;
    s_scroll  = 0.0f;
}

int vrm_overlay_is_visible(void)
{
    return s_visible;
}

int vrm_overlay_camera_locked(void)
{
    return s_cam_locked;
}

int vrm_overlay_subtitle_enabled(void)
{
    return s_sub_enabled;
}

int vrm_overlay_fullscreen(void)
{
    return s_fullscreen;
}

int vrm_overlay_spectator(void)
{
    return s_spectator;
}

void vrm_overlay_set_subtitle(const char *text)
{
    if (!text || text[0] == '\0') {
        s_subtitle[0] = '\0';
        __subtitle_texture_reset();
        return;
    }

    size_t text_len = strlen(text);
    const char *end = text + text_len;
    const char *scan = end;
    int cp_back = 0;

    while (scan > text && cp_back < OVL_SUB_MAX_GLYPHS) {
        scan--;
        if (((unsigned char)*scan & 0xC0) != 0x80) {
            cp_back++;
        }
    }

    const char *start = (cp_back >= OVL_SUB_MAX_GLYPHS) ? scan : text;

    size_t j = 0;
    for (size_t i = 0; start[i] != '\0' && j + 1 < OVL_SUBTITLE_MAX; i++) {
        char ch = start[i];
        if (ch == '\r' || ch == '\n' || ch == '\t') {
            ch = ' ';
        }
        s_subtitle[j++] = ch;
    }
    s_subtitle[j] = '\0';
    __subtitle_texture_reset();
}

void vrm_overlay_update_anims(char **names, int count, int active)
{
    s_anim_names = names;
    s_anim_count = count;
    s_anim_sel   = active;
}
