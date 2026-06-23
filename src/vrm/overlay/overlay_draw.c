/**
 * @file overlay_draw.c
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


void __push_quad(float x0, float y0, float x1, float y1,
                        float u0, float v0, float u1, float v1,
                        float r, float g, float b, float a)
{
    if (s_vert_count + 6 > OVL_MAX_VERTS) {
        return;
    }
    ovl_vert_t *v = &s_verts[s_vert_count];
    v[0] = (ovl_vert_t){x0,y0, u0,v0, r,g,b,a};
    v[1] = (ovl_vert_t){x1,y0, u1,v0, r,g,b,a};
    v[2] = (ovl_vert_t){x1,y1, u1,v1, r,g,b,a};
    v[3] = (ovl_vert_t){x0,y0, u0,v0, r,g,b,a};
    v[4] = (ovl_vert_t){x1,y1, u1,v1, r,g,b,a};
    v[5] = (ovl_vert_t){x0,y1, u0,v1, r,g,b,a};
    s_vert_count += 6;
}

void __push_rect(float x, float y, float w, float h,
                        float r, float g, float b, float a)
{
    __push_quad(x, y, x + w, y + h, 0,0, 0,0, r, g, b, a);
}


static void __push_char(float x, float y, char c,
                        float r, float g, float b, float a)
{
    if (c < 32 || c > 126) {
        return;
    }
    int idx = c - 32;
    int col = idx % 16;
    int row = idx / 16;
    float u0 = col * 8.0f / 128.0f;
    float v0 = row * 8.0f / 48.0f;
    float u1 = u0 + 8.0f / 128.0f;
    float v1 = v0 + 8.0f / 48.0f;
    __push_quad(x, y, x + OVL_CHAR_W, y + OVL_CHAR_H,
                u0, v0, u1, v1, r, g, b, a);
}

float __push_text(float x, float y, const char *text, int max_chars,
                         float r, float g, float b, float a)
{
    float cx = x;
    for (int i = 0; text[i] && (max_chars <= 0 || i < max_chars); i++) {
        __push_char(cx, y, text[i], r, g, b, a);
        cx += OVL_CHAR_W;
    }
    return cx - x;
}

/**
 * @brief Check if coordinates hit the settings icon button
 * @param[in] mx mouse X
 * @param[in] my mouse Y
 * @return 1 if hit, 0 otherwise
 */
int __icon_hit(int mx, int my)
{
    return mx >= OVL_ICON_MARGIN &&
           mx <  OVL_ICON_MARGIN + OVL_ICON_SIZE &&
           my >= OVL_ICON_MARGIN &&
           my <  OVL_ICON_MARGIN + OVL_ICON_SIZE;
}

/**
 * @brief Draw the settings icon (hamburger menu, pass 0 only)
 * @return none
 */
void __draw_settings_icon(void)
{
    float x  = (float)OVL_ICON_MARGIN;
    float y  = (float)OVL_ICON_MARGIN;
    float sz = (float)OVL_ICON_SIZE;

    /* three bars only, no background */
    float bar_w = 20.0f;
    float bar_h = 3.0f;
    float gap   = 5.5f;
    float total = bar_h * 3.0f + gap * 2.0f;
    float bx    = x + (sz - bar_w) * 0.5f;
    float by    = y + (sz - total) * 0.5f;

    for (int i = 0; i < 3; i++) {
        float fy = by + (float)i * (bar_h + gap);
        __push_rect(bx, fy, bar_w, bar_h, 0.85f, 0.90f, 1.0f, 0.85f);
    }
}

/* ---------------------------------------------------------------------------
 * File scanning
 * --------------------------------------------------------------------------- */

int __is_model_ext(const char *name)
{
    const char *exts[] = {".vrm",".glb",".pmx",".fbx",".gltf",".obj",NULL};
    size_t nlen = strlen(name);
    for (int i = 0; exts[i]; i++) {
        size_t elen = strlen(exts[i]);
        if (nlen > elen && strcasecmp(name + nlen - elen, exts[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

void __scan_dir(ovl_file_t *out, int *count, int max,
                       const char *dir, int (*filter)(const char *),
                       int dirs_only)
{
    *count = 0;
    if (!dir || dir[0] == '\0') {
        return;
    }
    DIR *d = opendir(dir);
    if (!d) {
        return;
    }
    struct dirent *e;
    while ((e = readdir(d)) != NULL && *count < max) {
        if (e->d_name[0] == '.') {
            continue;
        }
        if (dirs_only) {
            char full[OVL_MAX_PATH];
            snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
            struct stat st;
            if (stat(full, &st) != 0 || !S_ISDIR(st.st_mode)) {
                continue;
            }
            snprintf(out[*count].name, OVL_MAX_NAME, "%s", e->d_name);
            snprintf(out[*count].path, OVL_MAX_PATH, "%s", full);
        } else {
            if (filter && !filter(e->d_name)) {
                continue;
            }
            snprintf(out[*count].name, OVL_MAX_NAME, "%s", e->d_name);
            snprintf(out[*count].path, OVL_MAX_PATH, "%s/%s", dir, e->d_name);
        }
        (*count)++;
    }
    closedir(d);
    for (int i = 0; i < *count - 1; i++) {
        for (int j = i + 1; j < *count; j++) {
            if (strcmp(out[i].name, out[j].name) > 0) {
                ovl_file_t tmp = out[i];
                out[i] = out[j];
                out[j] = tmp;
            }
        }
    }
}
