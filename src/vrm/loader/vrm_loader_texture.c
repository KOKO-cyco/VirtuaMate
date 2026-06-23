/**
 * @file vrm_loader_texture.c
 * @brief VRM loader submodule (split from vrm_loader.c)
 * @version 0.1
 * @date 2025-06-23
 * @copyright Copyright (c) Tuya Inc. All Rights Reserved.
 */
#include "vrm_loader.h"
#include "vrm_loader_internal.h"
#include "mat4_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <float.h>
#include <libgen.h>
#include <math.h>

#include <assimp/cimport.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/material.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

/* ================================================================== */
/*  Texture loading helpers                                            */
/* ================================================================== */

int __decode_embedded_texture(const struct aiTexture *ai_tex, vrm_texture_t *out)
{
    if (!ai_tex || !out) return -1;

    if (ai_tex->mHeight == 0) {
        int w = 0, h = 0, ch = 0;
        stbi_set_flip_vertically_on_load(0);
        uint8_t *pixels = stbi_load_from_memory(
            (const unsigned char *)ai_tex->pcData,
            (int)ai_tex->mWidth, &w, &h, &ch, 4);
        if (!pixels) return -1;
        out->pixels = pixels; out->width = w; out->height = h; out->channels = 4;
        return 0;
    } else {
        int w = (int)ai_tex->mWidth, h = (int)ai_tex->mHeight;
        uint8_t *pixels = (uint8_t *)malloc((size_t)(w * h * 4));
        if (!pixels) return -1;
        const struct aiTexel *src = ai_tex->pcData;
        for (int i = 0; i < w * h; i++) {
            pixels[i*4+0] = src[i].r; pixels[i*4+1] = src[i].g;
            pixels[i*4+2] = src[i].b; pixels[i*4+3] = src[i].a;
        }
        out->pixels = pixels; out->width = w; out->height = h; out->channels = 4;
        return 0;
    }
}

int __resolve_texture_index(const struct aiMaterial *mat,
                                   const struct aiScene *scene,
                                   char *out_path, int path_size)
{
    struct aiString tex_path;
    out_path[0] = '\0';
    enum aiTextureType types[] = { aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE };
    for (int t = 0; t < 2; t++) {
        if (aiGetMaterialTexture(mat, types[t], 0, &tex_path,
                                 NULL, NULL, NULL, NULL, NULL, NULL) == aiReturn_SUCCESS) {
            if (tex_path.length > 0) {
                if (tex_path.data[0] == '*') {
                    int idx = atoi(tex_path.data + 1);
                    if (idx >= 0 && (unsigned)idx < scene->mNumTextures) return idx;
                }
                for (unsigned i = 0; i < scene->mNumTextures; i++) {
                    if (scene->mTextures[i]->mFilename.length > 0 &&
                        strcmp(scene->mTextures[i]->mFilename.data, tex_path.data) == 0)
                        return (int)i;
                }
                snprintf(out_path, path_size, "%s", tex_path.data);
                return -2;
            }
        }
    }
    return -1;
}

#define MAX_CACHED_TEXTURES 256
static struct { char path[1024]; int index; } s_tex_cache[MAX_CACHED_TEXTURES];
int s_tex_cache_count = 0;

int __load_external_texture(vrm_model_t *model, const char *model_dir,
                                   const char *rel_path)
{
    char full_path[1024], norm_rel[512];
    snprintf(norm_rel, sizeof(norm_rel), "%s", rel_path);
    vrm_loader_normalize_path(norm_rel);
    if (norm_rel[0] == '/')
        snprintf(full_path, sizeof(full_path), "%s", norm_rel);
    else
        snprintf(full_path, sizeof(full_path), "%s/%s", model_dir, norm_rel);

    for (int i = 0; i < s_tex_cache_count; i++)
        if (strcmp(s_tex_cache[i].path, full_path) == 0)
            return s_tex_cache[i].index;

    int w = 0, h = 0, ch = 0;
    stbi_set_flip_vertically_on_load(0);
    uint8_t *pixels = stbi_load(full_path, &w, &h, &ch, 4);
    if (!pixels) return -1;

    uint32_t new_idx = model->texture_count;
    vrm_texture_t *new_arr = (vrm_texture_t *)realloc(
        model->textures, (size_t)(new_idx + 1) * sizeof(vrm_texture_t));
    if (!new_arr) { stbi_image_free(pixels); return -1; }
    model->textures = new_arr;
    model->texture_count = new_idx + 1;
    model->textures[new_idx] = (vrm_texture_t){ pixels, w, h, 4 };

    if (s_tex_cache_count < MAX_CACHED_TEXTURES) {
        snprintf(s_tex_cache[s_tex_cache_count].path, 1024, "%s", full_path);
        s_tex_cache[s_tex_cache_count].index = (int)new_idx;
        s_tex_cache_count++;
    }
    printf("[vrm_loader] external texture[%u]: %dx%d  %s\n", new_idx, w, h, full_path);
    return (int)new_idx;
}

/* ================================================================== */
/*  Skeleton building from Assimp scene                                */
/* ================================================================== */

