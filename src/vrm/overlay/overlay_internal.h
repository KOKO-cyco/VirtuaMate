/**
 * @file overlay_internal.h
 * @brief Internal shared declarations for settings overlay submodules
 * @version 0.1
 * @date 2025-06-23
 * @copyright Copyright (c) Tuya Inc. All Rights Reserved.
 */
#ifndef OVERLAY_INTERNAL_H
#define OVERLAY_INTERNAL_H

#include "vrm_overlay.h"

#include <GL/glew.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#define OVL_MAX_FILES      64
#define OVL_MAX_PATH       512
#define OVL_MAX_NAME       128
#define OVL_MAX_VERTS      16384
#define OVL_SUBTITLE_MAX   2048
#define OVL_PANEL_W        340
#define OVL_FONT_SCALE     2
#define OVL_CHAR_W         (8 * OVL_FONT_SCALE)
#define OVL_CHAR_H         (8 * OVL_FONT_SCALE)
#define OVL_PAD            14
#define OVL_ITEM_H         (OVL_CHAR_H + 10)
#define OVL_SECTION_H      (OVL_CHAR_H + 14)
#define OVL_SEP_H          8
#define OVL_BORDER_W       2
#define OVL_SUB_FONT_PX    22.0f
#define OVL_SUB_MAX_GLYPHS 512
#define OVL_SUB_MAX_LINES  2
#define OVL_SUB_MAX_WRAP_LINES 32
#define OVL_SUB_LINE_GAP   6
#define OVL_SUB_MARGIN_X   36
#define OVL_ICON_SIZE      40
#define OVL_ICON_MARGIN    12
#define OVL_SUB_BOTTOM_GAP 26
#define OVL_SUB_INNER_PAD_X 18
#define OVL_SUB_INNER_PAD_Y 12
#define OVL_SUBTITLE_H     78
#define OVL_SUB_REPO_FONT \
    "TuyaOpen/src/liblvgl/v9/lvgl/tests/src/test_files/fonts/noto/NotoSansSC-Regular.ttf"
#define OVL_SUB_SYS_FONT   "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"
#define OVL_ICEILF(v)  ((int)((v) + 0.999f))
#define OVL_IROUNDF(v) ((int)(((v) >= 0.0f) ? ((v) + 0.5f) : ((v) - 0.5f)))

/* ---------------------------------------------------------------------------
 * Types
 * --------------------------------------------------------------------------- */
typedef struct {
    float x, y;
    float u, v;
    float r, g, b, a;
} ovl_vert_t;

typedef struct {
    char name[OVL_MAX_NAME];
    char path[OVL_MAX_PATH];
} ovl_file_t;

typedef struct {
    unsigned int cp;
    int x;
    int y;
    int line;
    int x0, y0, x1, y1;
} ovl_glyph_layout_t;

typedef struct {
    GLint     prog, vao, vbo, active_tex, tex_2d;
    GLint     blend_src_rgb, blend_dst_rgb, blend_src_a, blend_dst_a;
    GLint     viewport[4];
    GLint     scissor_box[4];
    GLboolean blend, depth, cull, scissor;
} gl_saved_state_t;

/* ---------------------------------------------------------------------------
 * Shared data (overlay_state.c)
 * --------------------------------------------------------------------------- */
extern const unsigned char FONT_8X8[95][8];
extern const char *OVL_VS;
extern const char *OVL_FS;

extern GLuint     s_prog;
extern GLint      s_u_proj;
extern GLint      s_u_tex;
extern GLint      s_u_use_tex;
extern GLuint     s_vao;
extern GLuint     s_vbo;
extern GLuint     s_font_tex;

extern ovl_vert_t s_verts[OVL_MAX_VERTS];
extern int        s_vert_count;
extern int        s_text_batch_start;

extern ovl_file_t s_models[OVL_MAX_FILES];
extern int        s_model_count;
extern int        s_model_sel;
extern char       s_model_dir[OVL_MAX_PATH];

extern ovl_file_t s_scenes[OVL_MAX_FILES];
extern int        s_scene_count;
extern int        s_scene_sel;
extern char       s_scene_dir[OVL_MAX_PATH];

extern char     **s_anim_names;
extern int        s_anim_count;
extern int        s_anim_sel;

extern int        s_visible;
extern int        s_cam_locked;
extern int        s_sub_enabled;
extern int        s_fullscreen;
extern int        s_spectator;
extern float      s_scroll;
extern char       s_subtitle[OVL_SUBTITLE_MAX];

extern vrm_overlay_str_cb_t s_cb_model;
extern vrm_overlay_int_cb_t s_cb_anim;
extern vrm_overlay_str_cb_t s_cb_scene;
extern vrm_overlay_int_cb_t s_cb_cam;
extern vrm_overlay_int_cb_t s_cb_sub;
extern vrm_overlay_int_cb_t s_cb_fs;
extern vrm_overlay_int_cb_t s_cb_spec;
extern void            *s_cb_ud;

extern GLuint          s_sub_tex;
extern int             s_sub_draw_w;
extern int             s_sub_draw_h;
extern int             s_sub_last_max_w;
extern int             s_sub_dirty;
extern unsigned char  *s_sub_pixels;
extern size_t          s_sub_pixels_cap;

/* ---------------------------------------------------------------------------
 * Draw helpers (overlay_draw.c)
 * --------------------------------------------------------------------------- */
void __push_quad(float x0, float y0, float x1, float y1,
                 float u0, float v0, float u1, float v1,
                 float r, float g, float b, float a);
void __push_rect(float x, float y, float w, float h,
                 float r, float g, float b, float a);
float __push_text(float x, float y, const char *text, int max_chars,
                  float r, float g, float b, float a);
int __icon_hit(int mx, int my);
void __draw_settings_icon(void);
int __is_model_ext(const char *name);
void __scan_dir(ovl_file_t *out, int *count, int max,
                const char *dir, int (*filter)(const char *), int dirs_only);

/* ---------------------------------------------------------------------------
 * Font / subtitle (overlay_font.c)
 * --------------------------------------------------------------------------- */
int __subtitle_font_init(void);
void __subtitle_texture_reset(void);
int __rebuild_subtitle_texture(int max_w);
int __subtitle_ascii_fallback(char *out, size_t out_sz, const char *in);
void __overlay_font_destroy(void);

/* ---------------------------------------------------------------------------
 * Panel / GL (overlay_panel.c)
 * --------------------------------------------------------------------------- */
void __init_gl(void);
float __content_height(void);
void __push_separator(float y, float pw);
void __walk_panel(int pass, int win_w, int win_h);
void __walk_subtitle(int pass, int win_w, int win_h);
void __render_subtitle_texture(int win_w, int win_h);
void __save_gl(gl_saved_state_t *st);
void __restore_gl(const gl_saved_state_t *st);
int __hit_panel_item(int mx, int my, int win_h, int *section, int *index);

#ifdef __cplusplus
}
#endif

#endif /* OVERLAY_INTERNAL_H */
