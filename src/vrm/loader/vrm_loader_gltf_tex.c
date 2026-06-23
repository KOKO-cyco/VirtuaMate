/**
 * @file vrm_loader_gltf_tex.c
 * @brief Decode glTF images from GLB BIN chunk (indexed by image[])
 * @version 1.0
 * @date 2025-06-23
 * @copyright Copyright (c) Tuya Inc. All Rights Reserved.
 */
#include "vrm_loader_internal.h"
#include "vrm_loader_glb_json.h"

#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stb/stb_image.h>

/**
 * @brief Decode one image from bufferView bytes in the GLB BIN chunk.
 * @param[in]  data  Image bytes.
 * @param[in]  len   Byte length.
 * @param[out] out   Output texture.
 * @return 0 on success, -1 on failure.
 */
static int __decode_image_bytes(const unsigned char *data, size_t len, vrm_texture_t *out)
{
    int      w = 0;
    int      h = 0;
    int      ch = 0;
    uint8_t *pixels = NULL;

    if (data == NULL || len == 0 || out == NULL) {
        return -1;
    }

    stbi_set_flip_vertically_on_load(0);
    pixels = stbi_load_from_memory(data, (int)len, &w, &h, &ch, 4);
    if (pixels == NULL || w <= 0 || h <= 0) {
        return -1;
    }

    out->pixels = pixels;
    out->width = w;
    out->height = h;
    out->channels = 4;
    return 0;
}

/**
 * @brief Load an external image URI relative to model_dir.
 * @param[in]  model_dir  Directory containing the model file.
 * @param[in]  uri        glTF image URI string.
 * @param[out] out        Output texture.
 * @return 0 on success, -1 on failure.
 */
static int __decode_image_uri(const char *model_dir, const char *uri, vrm_texture_t *out)
{
    char full_path[1024];
    int  w = 0;
    int  h = 0;
    int  ch = 0;
    uint8_t *pixels = NULL;

    if (uri == NULL || out == NULL) {
        return -1;
    }
    if (strncmp(uri, "data:", 5) == 0) {
        return -1;
    }

    if (uri[0] == '/') {
        snprintf(full_path, sizeof(full_path), "%s", uri);
    } else {
        snprintf(full_path, sizeof(full_path), "%s/%s", model_dir, uri);
    }

    stbi_set_flip_vertically_on_load(0);
    pixels = stbi_load(full_path, &w, &h, &ch, 4);
    if (pixels == NULL || w <= 0 || h <= 0) {
        return -1;
    }

    out->pixels = pixels;
    out->width = w;
    out->height = h;
    out->channels = 4;
    return 0;
}

/**
 * @brief Decode all glTF images[] from a GLB/VRM file into model->textures.
 *
 * Textures are stored by glTF image index so material textureProperties
 * resolve correctly even when Assimp skips embedded images.
 *
 * @param[in,out] model      Model to populate (replaces any existing textures).
 * @param[in]     path       Path to .vrm / .glb file.
 * @param[in]     model_dir  Directory for external image URIs.
 * @return 0 on success, -1 on failure.
 */
int vrm_loader_load_gltf_images(vrm_model_t *model, const char *path, const char *model_dir)
{
    vrm_glb_json_t glb;
    cJSON         *root = NULL;
    cJSON         *images = NULL;
    cJSON         *buffer_views = NULL;
    int            count = 0;
    int            i = 0;
    int            ok_count = 0;

    if (model == NULL || path == NULL) {
        return -1;
    }

    if (vrm_glb_json_load(&glb, path) != 0) {
        return -1;
    }

    root = cJSON_Parse(glb.json);
    if (root == NULL) {
        vrm_glb_json_free(&glb);
        return -1;
    }

    images = cJSON_GetObjectItem(root, "images");
    if (images == NULL || !cJSON_IsArray(images)) {
        cJSON_Delete(root);
        vrm_glb_json_free(&glb);
        return -1;
    }

    count = cJSON_GetArraySize(images);
    if (count <= 0) {
        cJSON_Delete(root);
        vrm_glb_json_free(&glb);
        return -1;
    }

    if (model->textures != NULL) {
        for (uint32_t ti = 0; ti < model->texture_count; ti++) {
            if (model->textures[ti].pixels != NULL) {
                stbi_image_free(model->textures[ti].pixels);
            }
        }
        free(model->textures);
        model->textures = NULL;
        model->texture_count = 0;
    }

    model->textures = (vrm_texture_t *)calloc((size_t)count, sizeof(vrm_texture_t));
    if (model->textures == NULL) {
        cJSON_Delete(root);
        vrm_glb_json_free(&glb);
        return -1;
    }
    model->texture_count = (uint32_t)count;

    buffer_views = cJSON_GetObjectItem(root, "bufferViews");

    for (i = 0; i < count; i++) {
        const cJSON *img = cJSON_GetArrayItem(images, i);
        const cJSON *uri = NULL;
        const cJSON *bv = NULL;
        const cJSON *bvi = NULL;
        const cJSON *off = NULL;
        const cJSON *blen = NULL;
        int          bv_idx = -1;
        size_t       byte_offset = 0;
        size_t       byte_length = 0;
        const char  *name = NULL;

        if (img == NULL) {
            continue;
        }

        name = cJSON_GetStringValue(cJSON_GetObjectItem(img, "name"));
        uri = cJSON_GetObjectItem(img, "uri");
        if (uri != NULL && cJSON_IsString(uri)) {
            if (__decode_image_uri(model_dir, uri->valuestring,
                                   &model->textures[i]) == 0) {
                ok_count++;
                printf("[vrm_loader] gltf image[%d] '%s': %dx%d OK (uri)\n",
                       i, name ? name : "", model->textures[i].width,
                       model->textures[i].height);
            }
            continue;
        }

        bv = cJSON_GetObjectItem(img, "bufferView");
        if (bv == NULL || !cJSON_IsNumber(bv) || buffer_views == NULL) {
            continue;
        }

        bv_idx = bv->valueint;
        bvi = cJSON_GetArrayItem(buffer_views, bv_idx);
        if (bvi == NULL) {
            continue;
        }

        off = cJSON_GetObjectItem(bvi, "byteOffset");
        blen = cJSON_GetObjectItem(bvi, "byteLength");
        if (blen == NULL || !cJSON_IsNumber(blen)) {
            continue;
        }

        byte_offset = (off != NULL && cJSON_IsNumber(off)) ? (size_t)off->valueint : 0;
        byte_length = (size_t)blen->valueint;

        if (glb.bin == NULL || byte_offset + byte_length > glb.bin_len) {
            printf("[vrm_loader] gltf image[%d]: bufferView out of range\n", i);
            continue;
        }

        if (__decode_image_bytes(glb.bin + byte_offset, byte_length,
                                 &model->textures[i]) == 0) {
            ok_count++;
            printf("[vrm_loader] gltf image[%d] '%s': %dx%d OK\n",
                   i, name ? name : "", model->textures[i].width,
                   model->textures[i].height);
        } else {
            printf("[vrm_loader] gltf image[%d] '%s': decode failed (%zu bytes)\n",
                   i, name ? name : "", byte_length);
        }
    }

    cJSON_Delete(root);
    vrm_glb_json_free(&glb);

    if (ok_count == 0) {
        return -1;
    }
    return 0;
}
