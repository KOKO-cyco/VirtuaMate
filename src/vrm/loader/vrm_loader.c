/**
 * @file vrm_loader.c
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
#include <stb/stb_image.h>

int vrm_model_load(vrm_model_t *model, const char *path)
{
    memset(model, 0, sizeof(*model));
    s_bone_map_count = 0;

    char path_copy[1024];
    snprintf(path_copy, sizeof(path_copy), "%s", path);
    char *model_dir = dirname(path_copy);
    printf("[vrm_loader] model dir: %s\n", model_dir);

    const char *ext = strrchr(path, '.');
    int use_memory_load = 0;
    int is_pmx = 0;
    if (ext && (strcasecmp(ext, ".vrm") == 0)) use_memory_load = 1;
    if (ext && (strcasecmp(ext, ".pmx") == 0 || strcasecmp(ext, ".pmd") == 0)) is_pmx = 1;

    s_tex_cache_count = 0;

    /* Do NOT use aiProcess_PreTransformVertices — we need the bone hierarchy! */
    unsigned int flags = aiProcess_Triangulate
                       | aiProcess_GenSmoothNormals
                       | aiProcess_JoinIdenticalVertices
                       | aiProcess_SortByPType
                       | aiProcess_LimitBoneWeights;  /* limit to 4 weights per vertex */

    if (is_pmx || use_memory_load)
        flags |= aiProcess_FlipUVs;

    const struct aiScene *scene = NULL;

    if (use_memory_load) {
        FILE *fp = fopen(path, "rb");
        if (!fp) { fprintf(stderr, "[vrm_loader] cannot open: %s\n", path); return -1; }
        fseek(fp, 0, SEEK_END);
        long file_size = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (file_size <= 0) { fclose(fp); return -1; }

        unsigned char *buf = (unsigned char *)malloc((size_t)file_size);
        if (!buf) { fclose(fp); return -1; }
        if ((long)fread(buf, 1, (size_t)file_size, fp) != file_size) {
            free(buf); fclose(fp); return -1;
        }
        fclose(fp);

        const char *hint = "glb";
        if (file_size >= 4 && buf[0] == '{') hint = "gltf";

        scene = aiImportFileFromMemory(
            (const char *)buf, (unsigned int)file_size, flags, hint);
        free(buf);
    } else {
        scene = aiImportFile(path, flags);
    }

    if (!scene || !scene->mRootNode) {
        fprintf(stderr, "[vrm_loader] Assimp error: %s\n", aiGetErrorString());
        return -1;
    }

    printf("[vrm_loader] loaded: meshes=%u  materials=%u  textures=%u  animations=%u\n",
           scene->mNumMeshes, scene->mNumMaterials, scene->mNumTextures,
           scene->mNumAnimations);

    /* ---- Build skeleton from node hierarchy ---- */
    model->bones = (vrm_bone_t *)calloc(VRM_MAX_BONES, sizeof(vrm_bone_t));
    model->bone_count = 0;
    vrm_loader_register_node_bones(scene->mRootNode, -1, model);
    vrm_loader_fill_offset_matrices(scene, model);
    printf("[vrm_loader] skeleton: %u bones\n", model->bone_count);

    /* ---- Decode textures (glTF images for VRM/GLB; Assimp fallback otherwise) ---- */
    if (use_memory_load) {
        if (vrm_loader_load_gltf_images(model, path, model_dir) != 0) {
            printf("[vrm_loader] gltf image load failed, trying assimp textures\n");
            if (scene->mNumTextures > 0) {
                model->texture_count = scene->mNumTextures;
                model->textures = (vrm_texture_t *)calloc(model->texture_count,
                                                            sizeof(vrm_texture_t));
                for (unsigned i = 0; i < scene->mNumTextures; i++) {
                    if (__decode_embedded_texture(scene->mTextures[i],
                                                  &model->textures[i]) == 0) {
                        printf("[vrm_loader] texture[%u]: %dx%d OK\n", i,
                               model->textures[i].width, model->textures[i].height);
                    }
                }
            }
        }
    } else if (scene->mNumTextures > 0) {
        model->texture_count = scene->mNumTextures;
        model->textures = (vrm_texture_t *)calloc(model->texture_count, sizeof(vrm_texture_t));
        for (unsigned i = 0; i < scene->mNumTextures; i++) {
            if (__decode_embedded_texture(scene->mTextures[i], &model->textures[i]) == 0) {
                printf("[vrm_loader] texture[%u]: %dx%d OK\n", i,
                       model->textures[i].width, model->textures[i].height);
            }
        }
    }

    /* ---- Extract meshes ---- */
    model->mesh_count = vrm_loader_count_meshes(scene->mRootNode);
    if (model->mesh_count == 0) { aiReleaseImport(scene); return -1; }
    model->meshes = (vrm_mesh_t *)calloc(model->mesh_count, sizeof(vrm_mesh_t));

    uint32_t idx = 0;
    {
        float identity_mat[16];
        mat4_identity(identity_mat);
        vrm_loader_extract_meshes(scene->mRootNode, scene, model, &idx, model_dir, identity_mat);
    }
    model->mesh_count = idx;

    /* ---- VRM MToon materials (from GLB JSON) ---- */
    vrm_loader_extract_mtoon_materials(model, path);

    /* ---- Extract embedded animations ---- */
    vrm_loader_extract_animations(scene, model);

    /* ---- Bounding box ---- */
    vrm_loader_compute_bbox(model);
    printf("[vrm_loader] center=(%.2f, %.2f, %.2f)  extent=%.2f\n",
           model->center[0], model->center[1], model->center[2], model->extent);

    /* ---- VRM humanoid mapping ---- */
    vrm_loader_extract_vrm_humanoid_full(model, path);

    /* ---- VRM expressions (BlendShape) ---- */
    vrm_loader_extract_vrm_expressions(model, path);

    /* ---- VRM spring bones (secondary animation) ---- */
    vrm_loader_extract_vrm_spring_bones(model, path);

    /* ---- VRM 1.0 node constraints (Aim / Roll) ---- */
    vrm_loader_extract_vrm_node_constraints(model, path);

    aiReleaseImport(scene);
    return 0;
}

void vrm_model_free(vrm_model_t *model)
{
    if (!model) return;

    if (model->meshes) {
        for (uint32_t i = 0; i < model->mesh_count; i++) {
            free(model->meshes[i].vertices);
            free(model->meshes[i].base_vertices);
            free(model->meshes[i].indices);
            if (model->meshes[i].morph_targets) {
                for (uint32_t mt = 0; mt < model->meshes[i].morph_target_count; mt++) {
                    free(model->meshes[i].morph_targets[mt].delta_positions);
                    free(model->meshes[i].morph_targets[mt].delta_normals);
                }
                free(model->meshes[i].morph_targets);
            }
        }
        free(model->meshes);
    }
    if (model->textures) {
        for (uint32_t i = 0; i < model->texture_count; i++)
            if (model->textures[i].pixels)
                stbi_image_free(model->textures[i].pixels);
        free(model->textures);
    }
    if (model->bones) free(model->bones);
    if (model->animations) {
        for (uint32_t ai = 0; ai < model->animation_count; ai++) {
            vrm_animation_t *va = &model->animations[ai];
            for (uint32_t bi = 0; bi < va->bone_anim_count; bi++) {
                vrm_bone_anim_t *ba = &va->bone_anims[bi];
                for (uint32_t ci = 0; ci < ba->channel_count; ci++) {
                    free(ba->channels[ci].times);
                    free(ba->channels[ci].values);
                }
                free(ba->channels);
            }
            free(va->bone_anims);
            if (va->expr_anims) {
                for (uint32_t ei = 0; ei < va->expr_anim_count; ei++) {
                    free(va->expr_anims[ei].channel.times);
                    free(va->expr_anims[ei].channel.values);
                }
                free(va->expr_anims);
            }
        }
        free(model->animations);
    }
    if (model->humanoid_map) free(model->humanoid_map);
    if (model->expressions) {
        for (uint32_t i = 0; i < model->expression_count; i++)
            free(model->expressions[i].binds);
        free(model->expressions);
    }

    /* Free spring bone data */
    if (model->spring_groups) {
        for (uint32_t i = 0; i < model->spring_group_count; i++) {
            free(model->spring_groups[i].joints);
            free(model->spring_groups[i].collider_group_indices);
        }
        free(model->spring_groups);
    }
    if (model->collider_groups) {
        for (uint32_t i = 0; i < model->collider_group_count; i++)
            free(model->collider_groups[i].colliders);
        free(model->collider_groups);
    }

    free(model->constraints);
    free(model->materials);

    memset(model, 0, sizeof(*model));
}
