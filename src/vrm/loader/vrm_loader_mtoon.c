/**
 * @file vrm_loader_mtoon.c
 * @brief Parse VRM 0.x MToon materialProperties from GLB JSON
 * @version 1.0
 * @date 2025-06-23
 * @copyright Copyright (c) Tuya Inc. All Rights Reserved.
 */
#include "vrm_loader.h"
#include "vrm_loader_internal.h"
#include "vrm_loader_glb_json.h"

#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* ---------------------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------------------- */

/**
 * @brief Case-insensitive substring search (portable strcasestr).
 * @param[in] haystack  String to search in.
 * @param[in] needle    Substring to find.
 * @return 1 if found, 0 otherwise.
 */
static int __str_contains_ci(const char *haystack, const char *needle)
{
    size_t nlen;
    size_t i;

    if (haystack == NULL || needle == NULL || needle[0] == '\0') {
        return 0;
    }
    nlen = strlen(needle);
    for (i = 0; haystack[i] != '\0'; i++) {
        if (strncasecmp(haystack + i, needle, nlen) == 0) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Return glTF image index referenced by a texture index.
 * @param[in] root          glTF root JSON.
 * @param[in] gltf_tex_idx  Index into textures[].
 * @return Image index, or -1.
 */
static int __gltf_image_index(const cJSON *root, int gltf_tex_idx)
{
    const cJSON *textures;
    const cJSON *tex;
    const cJSON *src;

    if (gltf_tex_idx < 0) {
        return -1;
    }
    textures = cJSON_GetObjectItem(root, "textures");
    if (textures == NULL || !cJSON_IsArray(textures)) {
        return -1;
    }
    tex = cJSON_GetArrayItem(textures, gltf_tex_idx);
    if (tex == NULL) {
        return -1;
    }
    src = cJSON_GetObjectItem(tex, "source");
    if (src != NULL && cJSON_IsNumber(src)) {
        return src->valueint;
    }
    return -1;
}

/**
 * @brief Test whether a glTF image is a Unity baked auxiliary map, not MToon shade.
 * @param[in] root     glTF root JSON.
 * @param[in] img_idx  Index into images[].
 * @return 1 if not a valid MToon _ShadeTexture, 0 if usable.
 */
static int __is_non_mtoon_shade_image(const cJSON *root, int img_idx)
{
    const cJSON *images;
    const cJSON *img;
    const cJSON *name_item;
    const char  *name;

    if (img_idx < 0) {
        return 1;
    }
    images = cJSON_GetObjectItem(root, "images");
    if (images == NULL || !cJSON_IsArray(images)) {
        return 0;
    }
    img = cJSON_GetArrayItem(images, img_idx);
    if (img == NULL) {
        return 1;
    }
    name_item = cJSON_GetObjectItem(img, "name");
    if (name_item == NULL || !cJSON_IsString(name_item)) {
        return 0;
    }
    name = name_item->valuestring;
    if (__str_contains_ci(name, "SSS")
        || __str_contains_ci(name, "ShadowMap")
        || __str_contains_ci(name, "Particle")
        || __str_contains_ci(name, "Normal")
        || __str_contains_ci(name, "Emission")
        || __str_contains_ci(name, "Outline")
        || __str_contains_ci(name, "Width")) {
        return 1;
    }
    return 0;
}

/**
 * @brief Drop Unity auxiliary maps from MToon shade slot (use main instead).
 * @param[in]     root        glTF root JSON.
 * @param[in]     model       Loaded model.
 * @param[in]     shade_gltf  Original glTF texture index for _ShadeTexture.
 * @param[in,out] mat         Material with tex_main / tex_shade set.
 * @return none
 */
static void __sanitize_mtoon_shade(const cJSON *root,
                                   const vrm_model_t *model,
                                   int shade_gltf,
                                   vrm_material_t *mat)
{
    int img_idx;

    if (mat == NULL || shade_gltf < 0 || mat->tex_shade < 0) {
        return;
    }

    img_idx = __gltf_image_index(root, shade_gltf);
    if (__is_non_mtoon_shade_image(root, img_idx)) {
        mat->tex_shade = mat->tex_main;
        return;
    }

    if (img_idx >= 0 && (uint32_t)img_idx < model->texture_count) {
        const vrm_texture_t *tex = &model->textures[img_idx];

        if (tex->pixels != NULL && tex->width > 0 && tex->height > 0
            && (tex->width <= 4 || tex->height <= 4)) {
            mat->tex_shade = mat->tex_main;
        }
    }
}

/**
 * @brief Set default MToon material fields.
 * @param[out] m  Material to initialize.
 * @return none
 */
static void __material_defaults(vrm_material_t *m)
{
    memset(m, 0, sizeof(*m));
    m->color[0] = 1.0f;
    m->color[1] = 1.0f;
    m->color[2] = 1.0f;
    m->color[3] = 1.0f;
    m->shade_color[0] = 0.9f;
    m->shade_color[1] = 0.8f;
    m->shade_color[2] = 0.7f;
    m->shade_color[3] = 1.0f;
    m->cutoff = 0.5f;
    m->shade_shift = 0.0f;
    m->shade_toony = 0.9f;
    m->render_queue = 2000;
    m->blend_mode = VRM_MTOON_OPAQUE;
    m->tex_main = -1;
    m->tex_shade = -1;
    snprintf(m->shader, sizeof(m->shader), "none");
}

/**
 * @brief Read a float from a cJSON object child.
 * @param[in] obj  Parent object.
 * @param[in] key  Property name.
 * @param[in] def  Default if missing.
 * @return Property value or default.
 */
static float __json_float(const cJSON *obj, const char *key, float def)
{
    const cJSON *item = cJSON_GetObjectItem(obj, key);

    if (item != NULL && cJSON_IsNumber(item)) {
        return (float)item->valuedouble;
    }
    return def;
}

/**
 * @brief Read a vec4 color from vectorProperties-style array.
 * @param[out] out   Four-component output.
 * @param[in]  arr   cJSON array (may be NULL).
 * @return none
 */
static void __json_vec4(float out[4], const cJSON *arr)
{
    int i;

    for (i = 0; i < 4; i++) {
        out[i] = 1.0f;
    }
    if (arr == NULL || !cJSON_IsArray(arr)) {
        return;
    }
    for (i = 0; i < 4 && i < cJSON_GetArraySize(arr); i++) {
        const cJSON *v = cJSON_GetArrayItem(arr, i);
        if (cJSON_IsNumber(v)) {
            out[i] = (float)v->valuedouble;
        }
    }
}

/**
 * @brief Clamp invalid renderQueue and apply UniVRM defaults from blend mode.
 * @param[out] mat  Material to fix.
 * @return none
 */
static void __fix_render_queue(vrm_material_t *mat)
{
    if (mat == NULL) {
        return;
    }
    if (mat->render_queue > 0) {
        return;
    }
    switch (mat->blend_mode) {
    case VRM_MTOON_CUTOUT:
        mat->render_queue = 2450;
        break;
    case VRM_MTOON_TRANSPARENT:
        mat->render_queue = 2501;
        break;
    case VRM_MTOON_TRANSPARENT_ZWRITE:
        mat->render_queue = 3000;
        break;
    case VRM_MTOON_OPAQUE:
    default:
        mat->render_queue = 2000;
        break;
    }
}

/**
 * @brief Map glTF texture index to model->textures[] (indexed by image source).
 * @param[in] model         Loaded model (textures decoded by image index).
 * @param[in] root          glTF root JSON.
 * @param[in] gltf_tex_idx  Index into glTF textures[].
 * @return Image index into model->textures, or -1.
 */
static int __resolve_gltf_texture(const vrm_model_t *model,
                                  const cJSON *root,
                                  int gltf_tex_idx)
{
    const cJSON *textures;
    const cJSON *tex;
    const cJSON *src;
    int          img_idx;

    if (gltf_tex_idx < 0) {
        return -1;
    }

    textures = cJSON_GetObjectItem(root, "textures");
    if (textures == NULL || !cJSON_IsArray(textures)) {
        return -1;
    }

    tex = cJSON_GetArrayItem(textures, gltf_tex_idx);
    if (tex == NULL) {
        return -1;
    }

    src = cJSON_GetObjectItem(tex, "source");
    img_idx = (src != NULL && cJSON_IsNumber(src)) ? src->valueint : -1;

    if (img_idx >= 0 && (uint32_t)img_idx < model->texture_count &&
        model->textures[img_idx].pixels != NULL) {
        return img_idx;
    }
    return -1;
}

/**
 * @brief Read texture index from textureProperties object.
 * @param[in] tex_props  textureProperties object.
 * @param[in] key        Property name (e.g. "_MainTex").
 * @return glTF texture index, or -1.
 */
static int __tex_prop_index(const cJSON *tex_props, const char *key)
{
    const cJSON *item = cJSON_GetObjectItem(tex_props, key);

    if (item != NULL && cJSON_IsNumber(item)) {
        return item->valueint;
    }
    return -1;
}

/**
 * @brief Apply glTF material alphaMode / unlit to a vrm_material_t.
 * @param[out] mat   Output material.
 * @param[in]  gm    glTF materials[i] object.
 * @return none
 */
static void __apply_gltf_material(vrm_material_t *mat, const cJSON *gm)
{
    const cJSON *ext;
    const cJSON *unlit;
    const cJSON *alpha;
    const char  *alpha_str;

    if (gm == NULL) {
        return;
    }

    ext = cJSON_GetObjectItem(gm, "extensions");
    if (ext != NULL) {
        unlit = cJSON_GetObjectItem(ext, "KHR_materials_unlit");
        if (unlit != NULL) {
            mat->is_unlit = 1;
        }
    }

    alpha = cJSON_GetObjectItem(gm, "alphaMode");
    if (alpha != NULL && cJSON_IsString(alpha)) {
        alpha_str = alpha->valuestring;
        if (strcmp(alpha_str, "MASK") == 0) {
            mat->blend_mode = VRM_MTOON_CUTOUT;
        } else if (strcmp(alpha_str, "BLEND") == 0) {
            if (mat->blend_mode == VRM_MTOON_OPAQUE) {
                mat->blend_mode = VRM_MTOON_TRANSPARENT;
            }
        }
    }
}

/**
 * @brief Parse one VRM materialProperties entry.
 * @param[out] mat    Output material.
 * @param[in]  prop   materialProperties[i] object.
 * @param[in]  root   glTF root.
 * @param[in]  model  Model with textures loaded.
 * @return none
 */
static void __parse_material_property(vrm_material_t *mat,
                                      const cJSON *prop,
                                      const cJSON *root,
                                      const vrm_model_t *model)
{
    const cJSON *fp;
    const cJSON *vp;
    const cJSON *tp;
    const cJSON *shader;
    const cJSON *rq;
    const cJSON *name;
    int          blend_f;
    int          main_gltf;
    int          shade_gltf;

    __material_defaults(mat);

    name = cJSON_GetObjectItem(prop, "name");
    if (name != NULL && cJSON_IsString(name)) {
        snprintf(mat->name, sizeof(mat->name), "%s", name->valuestring);
    }

    shader = cJSON_GetObjectItem(prop, "shader");
    if (shader != NULL && cJSON_IsString(shader)) {
        snprintf(mat->shader, sizeof(mat->shader), "%s", shader->valuestring);
        if (strcmp(shader->valuestring, "VRM/MToon") == 0) {
            mat->is_mtoon = 1;
        }
    }

    rq = cJSON_GetObjectItem(prop, "renderQueue");
    if (rq != NULL && cJSON_IsNumber(rq) && rq->valueint > 0) {
        mat->render_queue = rq->valueint;
    }

    fp = cJSON_GetObjectItem(prop, "floatProperties");
    if (fp != NULL) {
        blend_f = (int)__json_float(fp, "_BlendMode", 0.0f);
        if (blend_f >= 0 && blend_f <= 3) {
            mat->blend_mode = (vrm_mtoon_blend_mode_t)blend_f;
        }
        mat->cutoff = __json_float(fp, "_Cutoff", mat->cutoff);
        mat->shade_shift = __json_float(fp, "_ShadeShift", mat->shade_shift);
        mat->shade_toony = __json_float(fp, "_ShadeToony", mat->shade_toony);
    }

    vp = cJSON_GetObjectItem(prop, "vectorProperties");
    if (vp != NULL) {
        __json_vec4(mat->color, cJSON_GetObjectItem(vp, "_Color"));
        __json_vec4(mat->shade_color, cJSON_GetObjectItem(vp, "_ShadeColor"));
    }

    tp = cJSON_GetObjectItem(prop, "textureProperties");
    if (tp != NULL) {
        main_gltf = __tex_prop_index(tp, "_MainTex");
        shade_gltf = __tex_prop_index(tp, "_ShadeTexture");
        mat->tex_main = __resolve_gltf_texture(model, root, main_gltf);
        mat->tex_shade = __resolve_gltf_texture(model, root, shade_gltf);
        if (mat->tex_shade < 0) {
            mat->tex_shade = mat->tex_main;
        }
        __sanitize_mtoon_shade(root, model, shade_gltf, mat);
    }
}

/* ---------------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------------- */

/**
 * @brief Extract VRM 0.x MToon materials from a .vrm file.
 * @param[in,out] model  Model to populate (materials array).
 * @param[in]     path   Path to .vrm / .glb.
 * @return 0 on success, -1 on parse failure (non-fatal: defaults remain).
 */
int vrm_loader_extract_mtoon_materials(vrm_model_t *model, const char *path)
{
    vrm_glb_json_t glb;
    cJSON         *root = NULL;
    cJSON         *ext = NULL;
    cJSON         *vrm = NULL;
    cJSON         *mprops = NULL;
    cJSON         *gmats = NULL;
    int            count;
    int            i;

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

    ext = cJSON_GetObjectItem(root, "extensions");
    if (ext != NULL) {
        vrm = cJSON_GetObjectItem(ext, "VRM");
    }
    if (vrm == NULL) {
        cJSON_Delete(root);
        vrm_glb_json_free(&glb);
        return -1;
    }

    mprops = cJSON_GetObjectItem(vrm, "materialProperties");
    if (mprops == NULL || !cJSON_IsArray(mprops)) {
        cJSON_Delete(root);
        vrm_glb_json_free(&glb);
        return -1;
    }

    count = cJSON_GetArraySize(mprops);
    if (count <= 0) {
        cJSON_Delete(root);
        vrm_glb_json_free(&glb);
        return -1;
    }
    if (count > VRM_MAX_MATERIALS) {
        count = VRM_MAX_MATERIALS;
    }

    model->materials = (vrm_material_t *)calloc((size_t)count, sizeof(vrm_material_t));
    if (model->materials == NULL) {
        cJSON_Delete(root);
        vrm_glb_json_free(&glb);
        return -1;
    }
    model->material_count = (uint32_t)count;

    gmats = cJSON_GetObjectItem(root, "materials");

    for (i = 0; i < count; i++) {
        const cJSON *prop = cJSON_GetArrayItem(mprops, i);
        const cJSON *gm = NULL;

        __parse_material_property(&model->materials[i], prop, root, model);
        if (gmats != NULL && cJSON_IsArray(gmats)) {
            gm = cJSON_GetArrayItem(gmats, i);
            __apply_gltf_material(&model->materials[i], gm);
        }
        __fix_render_queue(&model->materials[i]);
        printf("[vrm_mtoon] mat[%d] '%s' shader=%s queue=%d mtoon=%d "
               "main_tex=%d shade_tex=%d blend=%d\n",
               i, model->materials[i].name, model->materials[i].shader,
               model->materials[i].render_queue, model->materials[i].is_mtoon,
               model->materials[i].tex_main, model->materials[i].tex_shade,
               (int)model->materials[i].blend_mode);
    }

    for (i = 0; i < (int)model->mesh_count; i++) {
        vrm_mesh_t *mesh = &model->meshes[i];
        int         mi = mesh->material_index;

        if (mi < 0 || (uint32_t)mi >= model->material_count) {
            continue;
        }
        if (mesh->texture_index < 0 && model->materials[mi].tex_main >= 0) {
            mesh->texture_index = model->materials[mi].tex_main;
        }
        memcpy(mesh->color, model->materials[mi].color, sizeof(float) * 4);
    }

    cJSON_Delete(root);
    vrm_glb_json_free(&glb);
    return 0;
}

/**
 * @brief Get render queue for a mesh (from its material, or 2000 default).
 * @param[in] model       Loaded model.
 * @param[in] mesh_index  Mesh index.
 * @return MToon renderQueue value.
 */
int vrm_material_render_queue(const vrm_model_t *model, uint32_t mesh_index)
{
    int mi;

    if (model == NULL || mesh_index >= model->mesh_count) {
        return 2000;
    }
    mi = model->meshes[mesh_index].material_index;
    if (mi < 0 || (uint32_t)mi >= model->material_count) {
        return 2000;
    }
    return model->materials[mi].render_queue;
}

/**
 * @brief Check if mesh should use MToon shader.
 * @param[in] model       Loaded model.
 * @param[in] mesh_index  Mesh index.
 * @return 1 if MToon, 0 otherwise.
 */
int vrm_material_is_mtoon(const vrm_model_t *model, uint32_t mesh_index)
{
    int mi;

    if (model == NULL || mesh_index >= model->mesh_count) {
        return 0;
    }
    mi = model->meshes[mesh_index].material_index;
    if (mi < 0 || (uint32_t)mi >= model->material_count) {
        return 0;
    }
    return model->materials[mi].is_mtoon;
}

/**
 * @brief Get material pointer for a mesh, or NULL.
 * @param[in] model       Loaded model.
 * @param[in] mesh_index  Mesh index.
 * @return Material pointer or NULL.
 */
const vrm_material_t *vrm_mesh_material(const vrm_model_t *model,
                                          uint32_t mesh_index)
{
    int mi;

    if (model == NULL || mesh_index >= model->mesh_count) {
        return NULL;
    }
    mi = model->meshes[mesh_index].material_index;
    if (mi < 0 || (uint32_t)mi >= model->material_count) {
        return NULL;
    }
    return &model->materials[mi];
}
