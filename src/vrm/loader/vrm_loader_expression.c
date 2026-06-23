/**
 * @file vrm_loader_expression.c
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

void vrm_loader_extract_vrm_expressions(vrm_model_t *model, const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return;

    unsigned char hdr[12];
    if (fread(hdr, 1, 12, fp) != 12) { fclose(fp); return; }
    if (hdr[0] != 'g' || hdr[1] != 'l' || hdr[2] != 'T' || hdr[3] != 'F') {
        fclose(fp); return;
    }

    uint32_t chunk_len, chunk_type;
    if (fread(&chunk_len, 4, 1, fp) != 1) { fclose(fp); return; }
    if (fread(&chunk_type, 4, 1, fp) != 1) { fclose(fp); return; }
    if (chunk_type != 0x4E4F534A) { fclose(fp); return; }

    char *json_str = (char *)malloc(chunk_len + 1);
    if (!json_str) { fclose(fp); return; }
    if (fread(json_str, 1, chunk_len, fp) != chunk_len) { free(json_str); fclose(fp); return; }
    json_str[chunk_len] = '\0';
    fclose(fp);

    model->expressions = (vrm_expression_t *)calloc(VRM_MAX_EXPRESSIONS, sizeof(vrm_expression_t));
    model->expression_count = 0;
    memset(model->expression_weights, 0, sizeof(model->expression_weights));

    /* ---- Parse glTF mesh primitives to build mesh-index mapping ---- */
    /* In glTF, meshes[i].primitives[j] maps to Assimp scene meshes sequentially.
     * We need: for each expression bind (mesh_index, morph_index in glTF),
     * find the corresponding vrm_mesh_t and morph target. */
    int gltf_mesh_to_assimp[256]; /* gltf mesh index -> first assimp mesh index */
    int gltf_mesh_prim_count[256];  /* gltf mesh index -> number of primitives */
    int gltf_mesh_count = 0;
    memset(gltf_mesh_to_assimp, -1, sizeof(gltf_mesh_to_assimp));
    memset(gltf_mesh_prim_count, 0, sizeof(gltf_mesh_prim_count));

    /* Parse "meshes" array to count primitives per glTF mesh */
    const char *meshes_key = strstr(json_str, "\"meshes\"");
    if (meshes_key) {
        const char *arr = strchr(meshes_key + 8, '[');
        if (arr) {
            int assimp_idx = 0;
            const char *p = arr + 1;
            while (*p && *p != ']' && gltf_mesh_count < 256) {
                const char *obj = strchr(p, '{');
                if (!obj) break;
                int depth = 1;
                const char *q = obj + 1;
                const char *obj_end = NULL;
                while (*q && depth > 0) {
                    if (*q == '{') depth++;
                    else if (*q == '}') { depth--; if (depth == 0) { obj_end = q; break; } }
                    q++;
                }
                if (!obj_end) break;

                gltf_mesh_to_assimp[gltf_mesh_count] = assimp_idx;

                /* Count primitives in this mesh */
                int prim_count = 1;
                const char *prim_key = strstr(obj, "\"primitives\"");
                if (prim_key && prim_key < obj_end) {
                    const char *parr = strchr(prim_key + 12, '[');
                    if (parr && parr < obj_end) {
                        prim_count = 0;
                        const char *pp = parr + 1;
                        int pd = 1;
                        while (*pp && pd > 0) {
                            if (*pp == '{') { pd++; if (pd == 2) prim_count++; }
                            else if (*pp == '}') pd--;
                            else if (*pp == ']' && pd == 1) break;
                            pp++;
                        }
                        if (prim_count < 1) prim_count = 1;
                    }
                }
                gltf_mesh_prim_count[gltf_mesh_count] = prim_count;
                assimp_idx += prim_count;

                gltf_mesh_count++;
                p = obj_end + 1;
            }
        }
    }

    /* Helper: find vrm_mesh_t index by assimp mesh index */
    #define FIND_VRM_MESH(assimp_mi) vrm_loader_find_vrm_mesh_by_assimp(model, (assimp_mi))

    /* ---- Try VRM 1.0: VRMC_vrm.expressions ---- */
    const char *vrmc_vrm = strstr(json_str, "\"VRMC_vrm\"");
    if (vrmc_vrm) {
        const char *expr_key = strstr(vrmc_vrm, "\"expressions\"");
        if (expr_key) {
            /* Find "preset" and "custom" sections */
            const char *sections[] = { "\"preset\"", "\"custom\"" };
            int is_preset[] = { 1, 0 };

            for (int sec = 0; sec < 2; sec++) {
                const char *sec_key = strstr(expr_key, sections[sec]);
                if (!sec_key) continue;

                const char *sp = sec_key + strlen(sections[sec]);
                while (*sp && *sp != '{') sp++;
                if (*sp != '{') continue;
                sp++;

                /* Iterate expression entries: "name":{...} */
                while (*sp && model->expression_count < VRM_MAX_EXPRESSIONS) {
                    while (*sp && (*sp == ' ' || *sp == '\n' || *sp == '\r' || *sp == '\t' || *sp == ',')) sp++;
                    if (*sp == '}') break;
                    if (*sp != '"') { sp++; continue; }

                    /* Expression name */
                    sp++;
                    const char *name_end = strchr(sp, '"');
                    if (!name_end) break;
                    int nlen = (int)(name_end - sp);
                    if (nlen >= 64) nlen = 63;

                    vrm_expression_t *expr = &model->expressions[model->expression_count];
                    memcpy(expr->name, sp, nlen);
                    expr->name[nlen] = '\0';
                    expr->is_preset = is_preset[sec];
                    expr->binds = NULL;
                    expr->bind_count = 0;
                    sp = name_end + 1;

                    /* Find the expression object */
                    while (*sp && *sp != '{') sp++;
                    if (!*sp) break;
                    /* Find end of this expression object */
                    int edepth = 1;
                    const char *eq = sp + 1;
                    const char *expr_end = NULL;
                    while (*eq && edepth > 0) {
                        if (*eq == '{') edepth++;
                        else if (*eq == '}') { edepth--; if (edepth == 0) { expr_end = eq; break; } }
                        eq++;
                    }
                    if (!expr_end) break;

                    /* Parse "morphTargetBinds":[{"node":N,"index":I,"weight":W},...] */
                    const char *mtb_key = strstr(sp, "\"morphTargetBinds\"");
                    if (mtb_key && mtb_key < expr_end) {
                        const char *mtb_arr = strchr(mtb_key + 18, '[');
                        if (mtb_arr && mtb_arr < expr_end) {
                            /* Count binds */
                            int max_binds = 32;
                            expr->binds = (vrm_expression_bind_t *)calloc(max_binds, sizeof(vrm_expression_bind_t));

                            const char *bp = mtb_arr + 1;
                            while (*bp && *bp != ']' && (int)expr->bind_count < max_binds) {
                                const char *bobj = strchr(bp, '{');
                                if (!bobj || bobj > expr_end) break;
                                const char *bobj_end = strchr(bobj + 1, '}');
                                if (!bobj_end) break;

                                int gltf_mesh_idx = -1, morph_idx = -1;
                                float bweight = 1.0f;

                                const char *fld;
                                fld = strstr(bobj, "\"node\"");
                                if (fld && fld < bobj_end) {
                                    const char *v = fld + 6;
                                    while (*v && (*v == ' ' || *v == ':' || *v == '\t')) v++;
                                    gltf_mesh_idx = atoi(v);
                                }
                                fld = strstr(bobj, "\"index\"");
                                if (fld && fld < bobj_end) {
                                    const char *v = fld + 7;
                                    while (*v && (*v == ' ' || *v == ':' || *v == '\t')) v++;
                                    morph_idx = atoi(v);
                                }
                                fld = strstr(bobj, "\"weight\"");
                                if (fld && fld < bobj_end) {
                                    const char *v = fld + 8;
                                    while (*v && (*v == ' ' || *v == ':' || *v == '\t')) v++;
                                    bweight = (float)atof(v);
                                }

                                /* Resolve glTF mesh index -> vrm mesh index.
                                 * A single glTF mesh may have N primitives that Assimp
                                 * splits into N separate meshes. Create a bind for each. */
                                if (gltf_mesh_idx >= 0 && gltf_mesh_idx < gltf_mesh_count && morph_idx >= 0) {
                                    int base_ai = gltf_mesh_to_assimp[gltf_mesh_idx];
                                    int nprims  = gltf_mesh_prim_count[gltf_mesh_idx];
                                    for (int pi = 0; pi < nprims && (int)expr->bind_count < max_binds; pi++) {
                                        int vrm_mi = FIND_VRM_MESH(base_ai + pi);
                                        if (vrm_mi >= 0 && (uint32_t)morph_idx < model->meshes[vrm_mi].morph_target_count) {
                                            vrm_expression_bind_t *bind = &expr->binds[expr->bind_count++];
                                            bind->mesh_index = (uint32_t)vrm_mi;
                                            bind->morph_index = (uint32_t)morph_idx;
                                            bind->weight = bweight;
                                        }
                                    }
                                }

                                bp = bobj_end + 1;
                            }

                            if (expr->bind_count == 0) {
                                free(expr->binds);
                                expr->binds = NULL;
                            }
                        }
                    }

                    if (expr->bind_count > 0 || expr->name[0] != '\0') {
                        model->expression_count++;
                    }

                    sp = expr_end + 1;
                }
            }
        }
    }

    /* ---- Try VRM 0.x: blendShapeMaster.blendShapeGroups ---- */
    if (model->expression_count == 0) {
        const char *bsm = strstr(json_str, "\"blendShapeMaster\"");
        if (!bsm) bsm = strstr(json_str, "\"blendShapeGroups\"");
        if (bsm) {
            const char *bsg = strstr(bsm, "\"blendShapeGroups\"");
            if (!bsg) bsg = bsm; /* might already point to it */
            const char *arr = strchr(bsg + 18, '[');
            if (arr) {
                const char *p = arr + 1;
                while (*p && *p != ']' && model->expression_count < VRM_MAX_EXPRESSIONS) {
                    const char *obj = strchr(p, '{');
                    if (!obj) break;
                    int depth = 1;
                    const char *q = obj + 1;
                    const char *obj_end = NULL;
                    while (*q && depth > 0) {
                        if (*q == '{') depth++;
                        else if (*q == '}') { depth--; if (depth == 0) { obj_end = q; break; } }
                        q++;
                    }
                    if (!obj_end) break;

                    vrm_expression_t *expr = &model->expressions[model->expression_count];
                    memset(expr, 0, sizeof(*expr));

                    /* Parse "name" (VRM 0.x uses "name" field) */
                    const char *nk = strstr(obj, "\"name\"");
                    if (nk && nk < obj_end) {
                        const char *nv = strchr(nk + 6, '"');
                        if (nv && nv < obj_end) {
                            nv++;
                            const char *ne = strchr(nv, '"');
                            if (ne) {
                                int nlen = (int)(ne - nv);
                                if (nlen >= 64) nlen = 63;
                                memcpy(expr->name, nv, nlen);
                                expr->name[nlen] = '\0';
                            }
                        }
                    }

                    /* Parse "presetName" — use it as the canonical expression name
                     * when available and not "unknown", so the emotion system can
                     * match by standard VRM preset names regardless of display language. */
                    const char *pk = strstr(obj, "\"presetName\"");
                    if (pk && pk < obj_end) {
                        const char *pv = strchr(pk + 12, '"');
                        if (pv && pv < obj_end) {
                            pv++;
                            const char *pve = strchr(pv, '"');
                            if (pve) {
                                int plen = (int)(pve - pv);
                                if (plen < 64 && !(plen == 7 && strncmp(pv, "unknown", 7) == 0)) {
                                    /* Overwrite name with standard presetName */
                                    memcpy(expr->name, pv, plen);
                                    expr->name[plen] = '\0';
                                    expr->is_preset = 1;
                                }
                            }
                        }
                    }

                    /* Parse "binds":[{"mesh":M,"index":I,"weight":W},...] */
                    const char *binds_key = strstr(obj, "\"binds\"");
                    if (binds_key && binds_key < obj_end) {
                        const char *barr = strchr(binds_key + 7, '[');
                        if (barr && barr < obj_end) {
                            int max_binds = 128; /* larger: each glTF bind expands to N primitives */
                            expr->binds = (vrm_expression_bind_t *)calloc(max_binds, sizeof(vrm_expression_bind_t));

                            const char *bp = barr + 1;
                            while (*bp && *bp != ']' && (int)expr->bind_count < max_binds) {
                                const char *bobj = strchr(bp, '{');
                                if (!bobj || bobj > obj_end) break;
                                const char *bobj_end = strchr(bobj + 1, '}');
                                if (!bobj_end) break;

                                int gltf_mesh_idx = -1, morph_idx = -1;
                                float bweight = 100.0f; /* VRM 0.x uses 0-100 */

                                const char *fld;
                                fld = strstr(bobj, "\"mesh\"");
                                if (fld && fld < bobj_end) {
                                    const char *v = fld + 6;
                                    while (*v && (*v == ' ' || *v == ':' || *v == '\t')) v++;
                                    gltf_mesh_idx = atoi(v);
                                }
                                fld = strstr(bobj, "\"index\"");
                                if (fld && fld < bobj_end) {
                                    const char *v = fld + 7;
                                    while (*v && (*v == ' ' || *v == ':' || *v == '\t')) v++;
                                    morph_idx = atoi(v);
                                }
                                fld = strstr(bobj, "\"weight\"");
                                if (fld && fld < bobj_end) {
                                    const char *v = fld + 8;
                                    while (*v && (*v == ' ' || *v == ':' || *v == '\t')) v++;
                                    bweight = (float)atof(v);
                                }

                                /* VRM 0.x weight is 0-100, normalize to 0-1 */
                                bweight /= 100.0f;

                                /* A single glTF mesh may have N primitives that Assimp
                                 * splits into N separate meshes. Create a bind for each. */
                                if (gltf_mesh_idx >= 0 && gltf_mesh_idx < gltf_mesh_count && morph_idx >= 0) {
                                    int base_ai = gltf_mesh_to_assimp[gltf_mesh_idx];
                                    int nprims  = gltf_mesh_prim_count[gltf_mesh_idx];
                                    for (int pi = 0; pi < nprims && (int)expr->bind_count < max_binds; pi++) {
                                        int vrm_mi = FIND_VRM_MESH(base_ai + pi);
                                        if (vrm_mi >= 0 && (uint32_t)morph_idx < model->meshes[vrm_mi].morph_target_count) {
                                            vrm_expression_bind_t *bind = &expr->binds[expr->bind_count++];
                                            bind->mesh_index = (uint32_t)vrm_mi;
                                            bind->morph_index = (uint32_t)morph_idx;
                                            bind->weight = bweight;
                                        }
                                    }
                                }

                                bp = bobj_end + 1;
                            }

                            if (expr->bind_count == 0) {
                                free(expr->binds);
                                expr->binds = NULL;
                            }
                        }
                    }

                    if (expr->name[0] != '\0') {
                        model->expression_count++;
                    }

                    p = obj_end + 1;
                }
            }
        }
    }

    #undef FIND_VRM_MESH

    free(json_str);

    if (model->expression_count > 0) {
        model->expressions = (vrm_expression_t *)realloc(
            model->expressions, model->expression_count * sizeof(vrm_expression_t));
        printf("[vrm_loader] expressions: %u\n", model->expression_count);
        for (uint32_t i = 0; i < model->expression_count && i < 10; i++)
            printf("[vrm_loader]   [%u] \"%s\" (%u binds, %s)\n",
                   i, model->expressions[i].name, model->expressions[i].bind_count,
                   model->expressions[i].is_preset ? "preset" : "custom");
        if (model->expression_count > 10)
            printf("[vrm_loader]   ... +%u more\n", model->expression_count - 10);
    } else {
        free(model->expressions);
        model->expressions = NULL;
        printf("[vrm_loader] no VRM expressions found\n");
    }
}

/* ================================================================== */
/*  Public: Expression / BlendShape API                                */
/* ================================================================== */

int vrm_find_expression(const vrm_model_t *model, const char *name)
{
    if (!model || !model->expressions || !name) return -1;
    for (uint32_t i = 0; i < model->expression_count; i++) {
        if (strcasecmp(model->expressions[i].name, name) == 0)
            return (int)i;
    }
    return -1;
}

void vrm_set_expression_weight(vrm_model_t *model, int expr_index, float weight)
{
    if (!model || expr_index < 0 || (uint32_t)expr_index >= model->expression_count) return;
    if (weight < 0.0f) weight = 0.0f;
    if (weight > 1.0f) weight = 1.0f;
    model->expression_weights[expr_index] = weight;
}

void vrm_apply_morph_targets(vrm_model_t *model)
{
    if (!model) return;

    /* Reset all meshes to base vertices */
    for (uint32_t mi = 0; mi < model->mesh_count; mi++) {
        vrm_mesh_t *mesh = &model->meshes[mi];
        if (mesh->base_vertices && mesh->morph_target_count > 0) {
            memcpy(mesh->vertices, mesh->base_vertices,
                   (size_t)mesh->vertex_count * 16 * sizeof(float));
        }
    }

    /* Accumulate morph target deltas from active expressions */
    for (uint32_t ei = 0; ei < model->expression_count; ei++) {
        float ew = model->expression_weights[ei];
        if (ew < 1e-6f) continue;

        const vrm_expression_t *expr = &model->expressions[ei];
        for (uint32_t bi = 0; bi < expr->bind_count; bi++) {
            const vrm_expression_bind_t *bind = &expr->binds[bi];
            if (bind->mesh_index >= model->mesh_count) continue;

            vrm_mesh_t *mesh = &model->meshes[bind->mesh_index];
            if (bind->morph_index >= mesh->morph_target_count) continue;

            const vrm_morph_target_t *mt = &mesh->morph_targets[bind->morph_index];
            float w = ew * bind->weight;

            /* Apply position deltas */
            if (mt->delta_positions) {
                for (uint32_t v = 0; v < mesh->vertex_count; v++) {
                    float *dst = &mesh->vertices[v * 16];
                    dst[0] += mt->delta_positions[v*3+0] * w;
                    dst[1] += mt->delta_positions[v*3+1] * w;
                    dst[2] += mt->delta_positions[v*3+2] * w;
                }
            }

            /* Apply normal deltas */
            if (mt->delta_normals) {
                for (uint32_t v = 0; v < mesh->vertex_count; v++) {
                    float *dst = &mesh->vertices[v * 16];
                    dst[3] += mt->delta_normals[v*3+0] * w;
                    dst[4] += mt->delta_normals[v*3+1] * w;
                    dst[5] += mt->delta_normals[v*3+2] * w;
                }
            }
        }
    }
}

/* ================================================================== */
/*  VRM Spring Bone extraction from glTF JSON                          */
/* ================================================================== */

/**
 * Find bone index by glTF node index.
 * glTF node indices map to Assimp nodes by name; we search our bone array.
 */
