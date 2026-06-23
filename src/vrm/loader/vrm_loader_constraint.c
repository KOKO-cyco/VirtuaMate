/**
 * @file vrm_loader_constraint.c
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

int vrm_loader_find_bone_by_node_index(const vrm_model_t *model,
                                     const char *json_str, int node_idx)
{
    /* Walk to "nodes" array, find entry at node_idx, get "name" */
    const char *narr = strstr(json_str, "\"nodes\"");
    if (!narr) return -1;
    const char *arr = strchr(narr + 7, '[');
    if (!arr) return -1;

    const char *p = arr + 1;
    int cur = 0;
    while (*p && cur < node_idx) {
        const char *obj = strchr(p, '{');
        if (!obj) return -1;
        int depth = 1;
        const char *q = obj + 1;
        while (*q && depth > 0) {
            if (*q == '{') depth++;
            else if (*q == '}') depth--;
            q++;
        }
        p = q;
        cur++;
    }

    /* Now p points near the node_idx-th object */
    const char *obj = strchr(p, '{');
    if (!obj) return -1;

    const char *nk = strstr(obj, "\"name\"");
    if (!nk) return -1;
    const char *nv = strchr(nk + 6, '"');
    if (!nv) return -1;
    nv++;
    const char *nve = strchr(nv, '"');
    if (!nve) return -1;

    int nlen = (int)(nve - nv);
    char node_name[128];
    if (nlen >= 128) nlen = 127;
    memcpy(node_name, nv, nlen);
    node_name[nlen] = '\0';

    /* Find bone with matching name */
    for (uint32_t i = 0; i < model->bone_count; i++) {
        if (strcmp(model->bones[i].name, node_name) == 0)
            return (int)i;
    }
    return -1;
}

/**
 * Build a chain from a root bone by walking all descendants recursively.
 * For single-child paths, follows the chain linearly.
 * For branches, recursively includes all sub-chains (depth-first).
 * Returns the number of bones added.
 */
int vrm_loader_build_bone_chain(const vrm_model_t *model, int root_bone,
                              int *out_chain, int max_chain)
{
    if (root_bone < 0 || root_bone >= (int)model->bone_count) return 0;
    if (max_chain <= 0) return 0;

    int count = 0;
     out_chain[count++] = root_bone;

    /* Find all children of this bone */
    int children[32];
    int child_count = 0;
    for (uint32_t i = 0; i < model->bone_count && child_count < 32; i++) {
        if (model->bones[i].parent == root_bone) {
            children[child_count++] = (int)i;
        }
    }

    /* Skip _end bones (leaf terminators with no children of their own) */
    if (child_count == 0) {
        return count; /* leaf */
    }

    /* Recursively add all children */
    for (int c = 0; c < child_count; c++) {
        int remaining = max_chain - count;
        if (remaining <= 0) break;
        int added = vrm_loader_build_bone_chain(model, children[c],
                                       &out_chain[count], remaining);
        count += added;
    }

    return count;
}

/* ================================================================== */
/*  VRMC_node_constraint extraction                                    */
/* ================================================================== */

void vrm_loader_extract_vrm_node_constraints(vrm_model_t *model, const char *path)
{
    model->constraints = NULL;
    model->constraint_count = 0;

    /* Read glTF JSON chunk */
    FILE *fp = fopen(path, "rb");
    if (!fp) return;

    uint8_t hdr[12];
    if (fread(hdr, 1, 12, fp) != 12) { fclose(fp); return; }

    uint32_t json_len;
    if (fread(&json_len, 4, 1, fp) != 1) { fclose(fp); return; }
    uint32_t chunk_type;
    if (fread(&chunk_type, 4, 1, fp) != 1) { fclose(fp); return; }

    char *json_str = (char *)malloc(json_len + 1);
    if (!json_str) { fclose(fp); return; }
    if (fread(json_str, 1, json_len, fp) != json_len) {
        free(json_str); fclose(fp); return;
    }
    json_str[json_len] = '\0';
    fclose(fp);

    /* Scan through "nodes" array for VRMC_node_constraint extensions */
    vrm_node_constraint_t constraints[VRM_MAX_CONSTRAINTS];
    int count = 0;

    const char *nc_ptr = strstr(json_str, "\"VRMC_node_constraint\"");
    while (nc_ptr && count < VRM_MAX_CONSTRAINTS) {
        /* Find the node index: walk backwards to find which node object this is in.
         * Instead, we re-scan from the node array to correlate. */
        nc_ptr = strstr(nc_ptr, "\"constraint\"");
        if (!nc_ptr) break;

        /* Find the enclosing constraint block end */
        const char *cblock = strchr(nc_ptr, '{');
        if (!cblock) break;
        int depth = 1;
        const char *cblock_end = cblock + 1;
        while (*cblock_end && depth > 0) {
            if (*cblock_end == '{') depth++;
            else if (*cblock_end == '}') depth--;
            cblock_end++;
        }

        /* Determine if this is "aim" or "roll" */
        const char *aim_key = strstr(cblock, "\"aim\"");
        const char *roll_key = strstr(cblock, "\"roll\"");

        int is_aim = (aim_key && aim_key < cblock_end) ? 1 : 0;
        int is_roll = (roll_key && roll_key < cblock_end) ? 1 : 0;

        if (!is_aim && !is_roll) {
            nc_ptr = cblock_end;
            continue;
        }

        vrm_node_constraint_t *nc = &constraints[count];
        memset(nc, 0, sizeof(*nc));
        nc->bone_index = -1;
        nc->source_index = -1;
        nc->weight = 1.0f;

        const char *inner = is_aim ? aim_key : roll_key;
        const char *inner_obj = strchr(inner, '{');
        if (!inner_obj || inner_obj >= cblock_end) { nc_ptr = cblock_end; continue; }
        const char *inner_end = strchr(inner_obj + 1, '}');
        if (!inner_end || inner_end >= cblock_end) inner_end = cblock_end - 1;

        /* "source": N */
        const char *src_key = strstr(inner_obj, "\"source\"");
        int source_node = -1;
        if (src_key && src_key < inner_end) {
            const char *v = src_key + 8;
            while (*v && (*v == ' ' || *v == ':' || *v == '\t')) v++;
            source_node = atoi(v);
            nc->source_index = vrm_loader_find_bone_by_node_index(model, json_str, source_node);
        }

        /* "weight": W */
        const char *wk = strstr(inner_obj, "\"weight\"");
        if (wk && wk < inner_end) {
            const char *v = wk + 8;
            while (*v && (*v == ' ' || *v == ':' || *v == '\t')) v++;
            nc->weight = (float)atof(v);
        }

        if (is_aim) {
            nc->type = 0;
            /* "aimAxis": "PositiveX" etc */
            const char *ax_key = strstr(inner_obj, "\"aimAxis\"");
            if (ax_key && ax_key < inner_end) {
                const char *v = strchr(ax_key + 9, '"');
                if (v) {
                    v++;
                    if (strncmp(v, "PositiveX", 9) == 0) nc->axis = VRM_AIM_POSITIVE_X;
                    else if (strncmp(v, "NegativeX", 9) == 0) nc->axis = VRM_AIM_NEGATIVE_X;
                    else if (strncmp(v, "PositiveY", 9) == 0) nc->axis = VRM_AIM_POSITIVE_Y;
                    else if (strncmp(v, "NegativeY", 9) == 0) nc->axis = VRM_AIM_NEGATIVE_Y;
                    else if (strncmp(v, "PositiveZ", 9) == 0) nc->axis = VRM_AIM_POSITIVE_Z;
                    else if (strncmp(v, "NegativeZ", 9) == 0) nc->axis = VRM_AIM_NEGATIVE_Z;
                }
            }
        } else {
            nc->type = 1;
            /* "rollAxis": "X" / "Y" / "Z" */
            const char *ax_key = strstr(inner_obj, "\"rollAxis\"");
            if (ax_key && ax_key < inner_end) {
                const char *v = strchr(ax_key + 10, '"');
                if (v) {
                    v++;
                    if (*v == 'X') nc->axis = VRM_ROLL_X;
                    else if (*v == 'Y') nc->axis = VRM_ROLL_Y;
                    else if (*v == 'Z') nc->axis = VRM_ROLL_Z;
                }
            }
        }

        count++;
        nc_ptr = cblock_end;
    }

    /* Now we need to find which node each constraint belongs to.
     * Walk through nodes array, and for each node with VRMC_node_constraint,
     * match it to our parsed constraints in order. */
    if (count > 0) {
        /* Find node indices by re-scanning VRMC_node_constraint occurrences
         * correlated with nodes array position */
        const char *nodes_arr = strstr(json_str, "\"nodes\"");
        if (nodes_arr) {
            const char *arr = strchr(nodes_arr + 7, '[');
            if (arr) {
                const char *p = arr + 1;
                int node_idx = 0;
                int ci = 0; /* constraint index */
                while (*p && *p != ']' && ci < count) {
                    const char *obj = strchr(p, '{');
                    if (!obj) break;
                    /* Find matching end brace at depth 1 */
                    int d = 1;
                    const char *q = obj + 1;
                    const char *obj_end = NULL;
                    while (*q && d > 0) {
                        if (*q == '{') d++;
                        else if (*q == '}') { d--; if (d == 0) { obj_end = q; break; } }
                        q++;
                    }
                    if (!obj_end) break;

                    /* Check if this node has VRMC_node_constraint */
                    const char *nc_check = strstr(obj, "\"VRMC_node_constraint\"");
                    if (nc_check && nc_check < obj_end) {
                        if (ci < count) {
                            constraints[ci].bone_index = vrm_loader_find_bone_by_node_index(
                                model, json_str, node_idx);
                            ci++;
                        }
                    }

                    p = obj_end + 1;
                    node_idx++;
                }
            }
        }

        /* Allocate and copy */
        model->constraints = (vrm_node_constraint_t *)malloc(
            count * sizeof(vrm_node_constraint_t));
        memcpy(model->constraints, constraints, count * sizeof(vrm_node_constraint_t));
        model->constraint_count = count;

        printf("[vrm_loader] node constraints: %d\n", count);
        for (int i = 0; i < count; i++) {
            const char *type_str = constraints[i].type == 0 ? "aim" : "roll";
            const char *bone_name = (constraints[i].bone_index >= 0) ?
                model->bones[constraints[i].bone_index].name : "?";
            const char *src_name = (constraints[i].source_index >= 0) ?
                model->bones[constraints[i].source_index].name : "?";
            printf("[vrm_loader]   [%d] %s: %s -> %s (weight=%.2f)\n",
                   i, type_str, bone_name, src_name, constraints[i].weight);
        }
    }

    free(json_str);
}

