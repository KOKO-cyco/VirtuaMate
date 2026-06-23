/**
 * @file vrm_loader_mesh.c
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

typedef struct {
    char name[128];
    int  bone_index;
} bone_name_map_t;

static bone_name_map_t s_bone_map[VRM_MAX_BONES];
int s_bone_map_count = 0;

int vrm_loader_find_bone_by_name(const char *name)
{
    for (int i = 0; i < s_bone_map_count; i++)
        if (strcmp(s_bone_map[i].name, name) == 0)
            return s_bone_map[i].bone_index;
    return -1;
}

/** Recursively register all nodes in the scene as bones.
 *  We need the full node hierarchy for correct animation. */
void vrm_loader_register_node_bones(const struct aiNode *node, int parent_idx,
                                  vrm_model_t *model)
{
    if (model->bone_count >= VRM_MAX_BONES) return;

    int my_idx = (int)model->bone_count;
    vrm_bone_t *bone = &model->bones[my_idx];

    snprintf(bone->name, sizeof(bone->name), "%s", node->mName.data);
    bone->parent = parent_idx;

    /* Store node's local transform */
    vrm_loader_ai_mat4_to_float16(&node->mTransformation, bone->local_transform);

    /* Inverse bind matrix will be filled from aiBone data later; default = identity */
    mat4_identity(bone->offset_matrix);

    /* Register in name map */
    if (s_bone_map_count < VRM_MAX_BONES) {
        snprintf(s_bone_map[s_bone_map_count].name, 128, "%s", node->mName.data);
        s_bone_map[s_bone_map_count].bone_index = my_idx;
        s_bone_map_count++;
    }

    model->bone_count++;

    for (unsigned i = 0; i < node->mNumChildren; i++)
        vrm_loader_register_node_bones(node->mChildren[i], my_idx, model);
}

/** After registering all nodes, fill in offset matrices from aiBone data. */
void vrm_loader_fill_offset_matrices(const struct aiScene *scene, vrm_model_t *model)
{
    for (unsigned mi = 0; mi < scene->mNumMeshes; mi++) {
        const struct aiMesh *mesh = scene->mMeshes[mi];
        for (unsigned bi = 0; bi < mesh->mNumBones; bi++) {
            const struct aiBone *ab = mesh->mBones[bi];
            int idx = vrm_loader_find_bone_by_name(ab->mName.data);
            if (idx >= 0) {
                vrm_loader_ai_mat4_to_float16(&ab->mOffsetMatrix, model->bones[idx].offset_matrix);
            } else {
                printf("[vrm_loader] vrm_loader_fill_offset_matrices: aiBone '%s' in mesh '%s' NOT FOUND in bone map!\n",
                       ab->mName.data, mesh->mName.data);
            }
        }
    }
}

/* ================================================================== */
/*  Mesh extraction with bone weights                                  */
/* ================================================================== */

uint32_t vrm_loader_count_meshes(const struct aiNode *node)
{
    uint32_t count = node->mNumMeshes;
    for (unsigned i = 0; i < node->mNumChildren; i++)
        count += vrm_loader_count_meshes(node->mChildren[i]);
    return count;
}

void vrm_loader_extract_meshes(const struct aiNode *node,
                             const struct aiScene *scene,
                             vrm_model_t *model,
                             uint32_t *out_idx,
                             const char *model_dir,
                             const float *parent_world)
{
    /* Compute this node's accumulated world transform */
    float node_local[16], node_world[16];
    vrm_loader_ai_mat4_to_float16(&node->mTransformation, node_local);
    mat4_multiply(node_world, parent_world, node_local);

    /* Check if world transform is non-identity (needs vertex pre-transform) */
    int world_is_identity = 1;
    for (int i = 0; i < 16; i++) {
        float expected = (i % 5 == 0) ? 1.0f : 0.0f;
        if (fabsf(node_world[i] - expected) > 1e-5f) {
            world_is_identity = 0;
            break;
        }
    }

    for (unsigned m = 0; m < node->mNumMeshes; m++) {
        const struct aiMesh *ai_mesh = scene->mMeshes[node->mMeshes[m]];
        vrm_mesh_t *mesh = &model->meshes[*out_idx];

        /* ---- Vertex layout: pos(3)+normal(3)+uv(2)+boneId(4)+boneWt(4) = 16 floats ---- */
        uint32_t nv = ai_mesh->mNumVertices;
        mesh->vertex_count = nv;
        mesh->vertices = (float *)calloc((size_t)nv * 16, sizeof(float));
        if (!mesh->vertices) return;

        for (unsigned v = 0; v < nv; v++) {
            float *dst = &mesh->vertices[v * 16];
            dst[0] = ai_mesh->mVertices[v].x;
            dst[1] = ai_mesh->mVertices[v].y;
            dst[2] = ai_mesh->mVertices[v].z;
            if (ai_mesh->mNormals) {
                dst[3] = ai_mesh->mNormals[v].x;
                dst[4] = ai_mesh->mNormals[v].y;
                dst[5] = ai_mesh->mNormals[v].z;
            } else {
                dst[4] = 1.0f;
            }
            if (ai_mesh->mTextureCoords[0]) {
                dst[6] = ai_mesh->mTextureCoords[0][v].x;
                dst[7] = ai_mesh->mTextureCoords[0][v].y;
            }
            /* bone IDs [8..11] and weights [12..15] initialized to 0 */
        }

        /* ---- Indices ---- */
        mesh->index_count = 0;
        for (unsigned f = 0; f < ai_mesh->mNumFaces; f++)
            mesh->index_count += ai_mesh->mFaces[f].mNumIndices;

        mesh->indices = (uint32_t *)calloc(mesh->index_count, sizeof(uint32_t));
        if (!mesh->indices) return;

        uint32_t ii = 0;
        for (unsigned f = 0; f < ai_mesh->mNumFaces; f++) {
            const struct aiFace *face = &ai_mesh->mFaces[f];
            for (unsigned j = 0; j < face->mNumIndices; j++)
                mesh->indices[ii++] = face->mIndices[j];
        }

        /* ---- Bone weights ---- */
        mesh->has_bones = (ai_mesh->mNumBones > 0) ? 1 : 0;
        {
            int resolved = 0, unresolved = 0;
            for (unsigned bi = 0; bi < ai_mesh->mNumBones; bi++) {
                const struct aiBone *ab = ai_mesh->mBones[bi];
                int tmp = vrm_loader_find_bone_by_name(ab->mName.data);
                if (tmp >= 0) resolved++; else { unresolved++; printf("[vrm_loader] mesh[%u] '%s': UNRESOLVED bone '%s'\n", *out_idx, ai_mesh->mName.data, ab->mName.data); }
            }
            if (unresolved > 0)
                printf("[vrm_loader] mesh[%u] '%s': %d/%u bones resolved, %d UNRESOLVED\n",
                       *out_idx, ai_mesh->mName.data, resolved, ai_mesh->mNumBones, unresolved);
        }
        for (unsigned bi = 0; bi < ai_mesh->mNumBones; bi++) {
            const struct aiBone *ab = ai_mesh->mBones[bi];
            int bone_idx = vrm_loader_find_bone_by_name(ab->mName.data);
            if (bone_idx < 0) continue;

            for (unsigned wi = 0; wi < ab->mNumWeights; wi++) {
                unsigned vert_id = ab->mWeights[wi].mVertexId;
                float weight = ab->mWeights[wi].mWeight;
                if (vert_id >= nv) continue;

                float *dst = &mesh->vertices[vert_id * 16];
                /* Find an empty slot in the 4 bone slots */
                for (int s = 0; s < 4; s++) {
                    if (dst[12 + s] == 0.0f) {
                        dst[8 + s] = (float)bone_idx;
                        dst[12 + s] = weight;
                        break;
                    }
                }
            }
        }

        /* Normalize bone weights */
        if (mesh->has_bones) {
            for (unsigned v = 0; v < nv; v++) {
                float *dst = &mesh->vertices[v * 16];
                float sum = dst[12] + dst[13] + dst[14] + dst[15];
                if (sum > 1e-6f) {
                    dst[12] /= sum; dst[13] /= sum;
                    dst[14] /= sum; dst[15] /= sum;
                }
            }
        }

        /* ---- Store Assimp mesh index for morph target / expression binding ---- */
        mesh->assimp_mesh_index = (int)node->mMeshes[m];

        /* ---- Morph targets (blend shapes) ---- */
        mesh->morph_targets = NULL;
        mesh->morph_target_count = 0;
        if (ai_mesh->mNumAnimMeshes > 0) {
            uint32_t mt_count = ai_mesh->mNumAnimMeshes;
            if (mt_count > VRM_MAX_MORPH_TARGETS) mt_count = VRM_MAX_MORPH_TARGETS;
            mesh->morph_targets = (vrm_morph_target_t *)calloc(mt_count, sizeof(vrm_morph_target_t));
            mesh->morph_target_count = mt_count;

            for (unsigned mi = 0; mi < mt_count; mi++) {
                const struct aiAnimMesh *am = ai_mesh->mAnimMeshes[mi];
                vrm_morph_target_t *mt = &mesh->morph_targets[mi];

                /* Name from Assimp */
                if (am->mName.length > 0)
                    snprintf(mt->name, sizeof(mt->name), "%s", am->mName.data);
                else
                    snprintf(mt->name, sizeof(mt->name), "morph_%u", mi);

                mt->vertex_count = nv;

                /* Compute position deltas */
                if (am->mVertices) {
                    mt->delta_positions = (float *)calloc((size_t)nv * 3, sizeof(float));
                    for (unsigned v = 0; v < nv; v++) {
                        mt->delta_positions[v*3+0] = am->mVertices[v].x - ai_mesh->mVertices[v].x;
                        mt->delta_positions[v*3+1] = am->mVertices[v].y - ai_mesh->mVertices[v].y;
                        mt->delta_positions[v*3+2] = am->mVertices[v].z - ai_mesh->mVertices[v].z;
                    }
                }

                /* Compute normal deltas */
                if (am->mNormals && ai_mesh->mNormals) {
                    mt->delta_normals = (float *)calloc((size_t)nv * 3, sizeof(float));
                    for (unsigned v = 0; v < nv; v++) {
                        mt->delta_normals[v*3+0] = am->mNormals[v].x - ai_mesh->mNormals[v].x;
                        mt->delta_normals[v*3+1] = am->mNormals[v].y - ai_mesh->mNormals[v].y;
                        mt->delta_normals[v*3+2] = am->mNormals[v].z - ai_mesh->mNormals[v].z;
                    }
                } else {
                    mt->delta_normals = NULL;
                }
            }
            printf("[vrm_loader] mesh[%u]: %u morph targets\n", *out_idx, mt_count);
        }

        /* ---- Pre-transform vertices to world space ---- */
        /* glTF skinning formula: jointMatrix = inv(meshNodeGlobal) * J * IBM
         * By pre-transforming vertices by meshNodeGlobal, our bone_matrix = J * IBM
         * works correctly for all meshes regardless of mesh node position. */
        if (!world_is_identity) {
            for (unsigned v = 0; v < nv; v++) {
                float *dst = &mesh->vertices[v * 16];
                /* Transform position (full 4x4 with translation) */
                float x = dst[0], y = dst[1], z = dst[2];
                dst[0] = node_world[0]*x + node_world[4]*y + node_world[8]*z  + node_world[12];
                dst[1] = node_world[1]*x + node_world[5]*y + node_world[9]*z  + node_world[13];
                dst[2] = node_world[2]*x + node_world[6]*y + node_world[10]*z + node_world[14];
                /* Transform normal (rotation/scale only, no translation) */
                float nx = dst[3], ny = dst[4], nz = dst[5];
                dst[3] = node_world[0]*nx + node_world[4]*ny + node_world[8]*nz;
                dst[4] = node_world[1]*nx + node_world[5]*ny + node_world[9]*nz;
                dst[5] = node_world[2]*nx + node_world[6]*ny + node_world[10]*nz;
            }
            /* Transform morph target deltas (rotation/scale only) */
            for (uint32_t mi2 = 0; mi2 < mesh->morph_target_count; mi2++) {
                vrm_morph_target_t *mt = &mesh->morph_targets[mi2];
                if (mt->delta_positions) {
                    for (unsigned v = 0; v < nv; v++) {
                        float dx = mt->delta_positions[v*3+0];
                        float dy = mt->delta_positions[v*3+1];
                        float dz = mt->delta_positions[v*3+2];
                        mt->delta_positions[v*3+0] = node_world[0]*dx + node_world[4]*dy + node_world[8]*dz;
                        mt->delta_positions[v*3+1] = node_world[1]*dx + node_world[5]*dy + node_world[9]*dz;
                        mt->delta_positions[v*3+2] = node_world[2]*dx + node_world[6]*dy + node_world[10]*dz;
                    }
                }
                if (mt->delta_normals) {
                    for (unsigned v = 0; v < nv; v++) {
                        float dx = mt->delta_normals[v*3+0];
                        float dy = mt->delta_normals[v*3+1];
                        float dz = mt->delta_normals[v*3+2];
                        mt->delta_normals[v*3+0] = node_world[0]*dx + node_world[4]*dy + node_world[8]*dz;
                        mt->delta_normals[v*3+1] = node_world[1]*dx + node_world[5]*dy + node_world[9]*dz;
                        mt->delta_normals[v*3+2] = node_world[2]*dx + node_world[6]*dy + node_world[10]*dz;
                    }
                }
            }
            printf("[vrm_loader] mesh[%u] '%s': pre-transformed to world space\n",
                   *out_idx, ai_mesh->mName.data);
        }

        /* ---- Create base vertices copy for morph blending ---- */
        mesh->base_vertices = (float *)malloc((size_t)nv * 16 * sizeof(float));
        if (mesh->base_vertices)
            memcpy(mesh->base_vertices, mesh->vertices, (size_t)nv * 16 * sizeof(float));

        /* ---- Material ---- */
        mesh->color[0] = mesh->color[1] = mesh->color[2] = mesh->color[3] = 1.0f;
        mesh->texture_index = -1;
        mesh->material_index = (ai_mesh->mMaterialIndex < scene->mNumMaterials)
            ? (int)ai_mesh->mMaterialIndex : -1;

        if (ai_mesh->mMaterialIndex < scene->mNumMaterials) {
            const struct aiMaterial *mat = scene->mMaterials[ai_mesh->mMaterialIndex];
            struct aiColor4D base_color = {1,1,1,1};
            if (aiGetMaterialColor(mat, AI_MATKEY_BASE_COLOR, &base_color) != aiReturn_SUCCESS)
                if (aiGetMaterialColor(mat, AI_MATKEY_COLOR_DIFFUSE, &base_color) != aiReturn_SUCCESS)
                    base_color = (struct aiColor4D){1,1,1,1};
            mesh->color[0] = base_color.r; mesh->color[1] = base_color.g;
            mesh->color[2] = base_color.b; mesh->color[3] = base_color.a;

            char ext_path[512];
            int tex_idx = __resolve_texture_index(mat, scene, ext_path, sizeof(ext_path));
            if (tex_idx >= 0)
                mesh->texture_index = tex_idx;
            else if (tex_idx == -2 && ext_path[0] != '\0' && model_dir)
                mesh->texture_index = __load_external_texture(model, model_dir, ext_path);
        }

        (*out_idx)++;
    }

    for (unsigned i = 0; i < node->mNumChildren; i++)
        vrm_loader_extract_meshes(node->mChildren[i], scene, model, out_idx, model_dir, node_world);
}

/* ================================================================== */
/*  Bounding box                                                       */
/* ================================================================== */

void vrm_loader_compute_bbox(vrm_model_t *model)
{
    model->bbox_min[0] = model->bbox_min[1] = model->bbox_min[2] =  FLT_MAX;
    model->bbox_max[0] = model->bbox_max[1] = model->bbox_max[2] = -FLT_MAX;

    for (uint32_t m = 0; m < model->mesh_count; m++) {
        const vrm_mesh_t *mesh = &model->meshes[m];
        for (uint32_t i = 0; i < mesh->vertex_count; i++) {
            const float *v = &mesh->vertices[i * 16]; /* pos at offset 0 */
            for (int j = 0; j < 3; j++) {
                if (v[j] < model->bbox_min[j]) model->bbox_min[j] = v[j];
                if (v[j] > model->bbox_max[j]) model->bbox_max[j] = v[j];
            }
        }
    }
    for (int j = 0; j < 3; j++)
        model->center[j] = (model->bbox_min[j] + model->bbox_max[j]) * 0.5f;

    float dx = model->bbox_max[0] - model->bbox_min[0];
    float dy = model->bbox_max[1] - model->bbox_min[1];
    float dz = model->bbox_max[2] - model->bbox_min[2];
    model->extent = dx;
    if (dy > model->extent) model->extent = dy;
    if (dz > model->extent) model->extent = dz;
    if (model->extent < 1e-6f) model->extent = 1.0f;
}
