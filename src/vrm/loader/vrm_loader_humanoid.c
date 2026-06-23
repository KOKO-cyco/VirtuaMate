/**
 * @file vrm_loader_humanoid.c
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

static void __extract_vrm_humanoid(vrm_model_t *model, const char *path)
{
    /* We need to parse the glTF JSON chunk ourselves since Assimp doesn't
       expose VRM extension data. Read the file and extract the JSON. */
    FILE *fp = fopen(path, "rb");
    if (!fp) return;

    unsigned char hdr[12];
    if (fread(hdr, 1, 12, fp) != 12) { fclose(fp); return; }

    /* Validate glTF binary header */
    if (hdr[0] != 'g' || hdr[1] != 'l' || hdr[2] != 'T' || hdr[3] != 'F') {
        fclose(fp); return;
    }

    uint32_t chunk_len, chunk_type;
    if (fread(&chunk_len, 4, 1, fp) != 1) { fclose(fp); return; }
    if (fread(&chunk_type, 4, 1, fp) != 1) { fclose(fp); return; }
    if (chunk_type != 0x4E4F534A) { fclose(fp); return; } /* 'JSON' */

    char *json = (char *)malloc(chunk_len + 1);
    if (!json) { fclose(fp); return; }
    if (fread(json, 1, chunk_len, fp) != chunk_len) { free(json); fclose(fp); return; }
    json[chunk_len] = '\0';
    fclose(fp);

    /* Simple JSON parsing for VRM humanoid bones.
       We look for both VRM 0.x ("VRM"."humanoid"."humanBones":[...])
       and VRM 1.0 ("VRMC_vrm"."humanoid"."humanBones":{...}) */

    /* Helper: find a key in JSON string. Very basic — works for our use case. */
    model->humanoid_map = NULL;
    model->humanoid_map_count = 0;

    /* Allocate generously */
    vrm_humanoid_map_t *map = (vrm_humanoid_map_t *)calloc(128, sizeof(vrm_humanoid_map_t));
    if (!map) { free(json); return; }
    int map_count = 0;

    /* Try VRM 0.x format: "humanBones" is an array of {"bone":"xxx","node":NNN} */
    const char *hb_ptr = strstr(json, "\"humanBones\"");
    while (hb_ptr) {
        /* Find the opening '[' or '{' after "humanBones" */
        const char *p = hb_ptr + 12;
        while (*p && *p != '[' && *p != '{') p++;

        if (*p == '[') {
            /* VRM 0.x array format */
            p++;
            while (*p && *p != ']' && map_count < 128) {
                /* Find "bone" key */
                const char *bone_key = strstr(p, "\"bone\"");
                if (!bone_key || bone_key > strchr(p, ']')) break;
                /* Extract bone name value */
                const char *bv = strchr(bone_key + 6, '"');
                if (!bv) break;
                bv++;
                const char *bv_end = strchr(bv, '"');
                if (!bv_end) break;
                int blen = (int)(bv_end - bv);
                if (blen >= 64) blen = 63;
                memcpy(map[map_count].humanoid_name, bv, blen);
                map[map_count].humanoid_name[blen] = '\0';

                /* Find "node" key and get node index */
                const char *node_key = strstr(bone_key, "\"node\"");
                if (!node_key) break;
                const char *nv = node_key + 6;
                while (*nv && (*nv == ' ' || *nv == ':' || *nv == '\t')) nv++;
                int node_idx = atoi(nv);

                /* Look up node name from Assimp scene — we stored it in our bone map */
                /* We need to match node index to bone name. Since we registered all nodes
                   in order during vrm_loader_register_node_bones, the assimp node index won't match
                   our bone index directly. Instead, we need to look up by the original
                   scene node. We'll store the node index and resolve after. */
                /* Actually, let's just store the node_idx and resolve later from the scene */
                snprintf(map[map_count].node_name, 128, "__node_idx_%d", node_idx);

                map_count++;
                p = bv_end + 1;
                /* Skip to next object or end */
                const char *next_obj = strchr(p, '{');
                const char *arr_end = strchr(p, ']');
                if (!next_obj || (arr_end && next_obj > arr_end)) break;
                p = next_obj;
            }
            break;
        } else if (*p == '{') {
            /* VRM 1.0 / VRMC_vrm dict format: {"hips":{"node":0}, ...} */
            p++;
            int brace_depth = 1;
            while (*p && brace_depth > 0 && map_count < 128) {
                /* Find next key */
                const char *key_start = strchr(p, '"');
                if (!key_start) break;
                key_start++;
                const char *key_end = strchr(key_start, '"');
                if (!key_end) break;

                int klen = (int)(key_end - key_start);
                if (klen >= 64) klen = 63;

                /* Check if this looks like a bone name (not "node") */
                if (strncmp(key_start, "node", 4) == 0 && klen == 4) {
                    p = key_end + 1;
                    continue;
                }

                memcpy(map[map_count].humanoid_name, key_start, klen);
                map[map_count].humanoid_name[klen] = '\0';

                /* Find "node" value */
                const char *node_key = strstr(key_end, "\"node\"");
                if (!node_key) { p = key_end + 1; continue; }
                const char *nv = node_key + 6;
                while (*nv && (*nv == ' ' || *nv == ':' || *nv == '\t')) nv++;
                int node_idx = atoi(nv);
                snprintf(map[map_count].node_name, 128, "__node_idx_%d", node_idx);

                map_count++;

                /* Move past this entry's closing brace */
                const char *cb = strchr(nv, '}');
                if (!cb) break;
                p = cb + 1;

                /* Check for end of outer brace */
                while (*p && (*p == ',' || *p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
                if (*p == '}') break;
            }
            break;
        }

        /* Try next occurrence */
        hb_ptr = strstr(hb_ptr + 12, "\"humanBones\"");
    }

    free(json);

    if (map_count > 0) {
        model->humanoid_map = (vrm_humanoid_map_t *)realloc(map, map_count * sizeof(vrm_humanoid_map_t));
        model->humanoid_map_count = map_count;
    } else {
        free(map);
    }
}

/** After scene is loaded and bone map built, resolve "__node_idx_N" entries
 *  to actual node names. */
static void __resolve_humanoid_node_names(vrm_model_t *model,
                                          const struct aiScene *scene)
{
    if (!model->humanoid_map) return;

    /* Build a flat list of all nodes by DFS order — matching glTF node indices */
    /* glTF node indices correspond to the order nodes appear in the JSON nodes array.
       Assimp preserves node names but flattens the hierarchy. We need to match
       by traversing the scene in the correct order. */

    /* Approach: glTF nodes array order = DFS pre-order of the Assimp scene.
       But actually Assimp doesn't guarantee this. Instead, let's just collect
       all nodes and try to match by index via a BFS/DFS that matches glTF order. */

    /* Simpler approach: we already parsed the node index from JSON. The glTF
       nodes array is ordered, and Assimp names nodes by their glTF names.
       So let's just re-read the JSON to get node name by index. */

    /* Actually the simplest: re-open the file and get node names from JSON. */
    /* But we don't have the path here... let's use a different approach:
       iterate Assimp scene nodes and build index. */

    /* Best approach: the node names are already in our bone_map, keyed by name.
       The "__node_idx_N" approach is fragile. Let's parse the JSON node names
       directly when we have the file path. We'll do this in __extract_vrm_humanoid. */

    /* For now, we'll use a brute-force DFS to assign indices. */
    /* Actually, let's just use a static array filled during scene traversal. */
}

/* Collect node names in DFS pre-order (matching glTF node array order). */
static const char **s_node_names_by_gltf_idx = NULL;
static int s_node_names_count = 0;

static void __collect_node_names_dfs(const struct aiNode *node)
{
    /* This doesn't match glTF node order because Assimp reorders nodes.
       Instead we'll re-parse from the JSON in __extract_vrm_humanoid. */
    (void)node;
}

/** Extract VRM humanoid mapping, resolving node indices to names from raw JSON. */
void vrm_loader_extract_vrm_humanoid_full(vrm_model_t *model, const char *path)
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

    /* ---- Parse all node names from "nodes" array ---- */
    char node_names[1024][128];
    int total_nodes = 0;

    const char *nodes_key = strstr(json_str, "\"nodes\"");
    if (nodes_key) {
        const char *arr = strchr(nodes_key + 7, '[');
        if (arr) {
            const char *p = arr + 1;
            while (*p && *p != ']' && total_nodes < 1024) {
                /* Find the next { */
                const char *obj = strchr(p, '{');
                if (!obj) break;
                /* Find "name" within this object */
                const char *obj_end = NULL;
                int depth = 1;
                const char *q = obj + 1;
                while (*q && depth > 0) {
                    if (*q == '{') depth++;
                    else if (*q == '}') { depth--; if (depth == 0) { obj_end = q; break; } }
                    q++;
                }
                if (!obj_end) break;

                /* Find "name" key between obj and obj_end */
                const char *name_key = obj;
                char found_name[128] = "";
                while ((name_key = strstr(name_key, "\"name\"")) != NULL && name_key < obj_end) {
                    const char *colon = name_key + 6;
                    while (*colon && (*colon == ' ' || *colon == ':' || *colon == '\t')) colon++;
                    if (*colon == '"') {
                        colon++;
                        const char *ne = strchr(colon, '"');
                        if (ne) {
                            int nlen = (int)(ne - colon);
                            if (nlen >= 128) nlen = 127;
                            memcpy(found_name, colon, nlen);
                            found_name[nlen] = '\0';
                        }
                    }
                    break;
                }
                snprintf(node_names[total_nodes], 128, "%s", found_name);
                total_nodes++;
                p = obj_end + 1;
            }
        }
    }

    printf("[vrm_loader] parsed %d node names from glTF JSON\n", total_nodes);

    /* ---- Parse humanoid bones ---- */
    model->humanoid_map = NULL;
    model->humanoid_map_count = 0;

    vrm_humanoid_map_t *map = (vrm_humanoid_map_t *)calloc(128, sizeof(vrm_humanoid_map_t));
    if (!map) { free(json_str); return; }
    int map_count = 0;

    /* Find humanBones — try all occurrences and pick the one inside VRM/VRMC_vrm */
    const char *hb_ptr = strstr(json_str, "\"humanBones\"");
    while (hb_ptr && map_count == 0) {
        const char *p = hb_ptr + 12;
        while (*p && *p != '[' && *p != '{') p++;

        if (*p == '[') {
            /* VRM 0.x array: [{"bone":"hips","node":27}, ...] */
            p++;
            while (*p && *p != ']' && map_count < 128) {
                const char *bone_key = strstr(p, "\"bone\"");
                const char *arr_end = strchr(p, ']');
                if (!bone_key || (arr_end && bone_key > arr_end)) break;

                const char *bv = strchr(bone_key + 6, '"');
                if (!bv) break;
                bv++;
                const char *bv_end = strchr(bv, '"');
                if (!bv_end) break;
                int blen = (int)(bv_end - bv);
                if (blen >= 64) blen = 63;
                memcpy(map[map_count].humanoid_name, bv, blen);
                map[map_count].humanoid_name[blen] = '\0';

                const char *node_key = strstr(bv_end, "\"node\"");
                if (!node_key) break;
                const char *nv = node_key + 6;
                while (*nv && (*nv == ' ' || *nv == ':' || *nv == '\t')) nv++;
                int node_idx = atoi(nv);

                if (node_idx >= 0 && node_idx < total_nodes)
                    snprintf(map[map_count].node_name, 128, "%s", node_names[node_idx]);
                else
                    snprintf(map[map_count].node_name, 128, "node_%d", node_idx);

                map_count++;

                const char *next_obj = strchr(bv_end, '{');
                arr_end = strchr(bv_end, ']');
                if (!next_obj || (arr_end && next_obj > arr_end)) break;
                p = next_obj;
            }
        } else if (*p == '{') {
            /* VRM 1.0 / VRMC_vrm dict: {"hips":{"node":0}, "spine":{"node":1}, ...} */
            p++;
            while (*p && map_count < 128) {
                while (*p && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t' || *p == ',')) p++;
                if (*p == '}') break;
                if (*p != '"') { p++; continue; }

                /* Key */
                p++;
                const char *key_end = strchr(p, '"');
                if (!key_end) break;
                int klen = (int)(key_end - p);
                if (klen >= 64) klen = 63;

                char key_buf[64];
                memcpy(key_buf, p, klen);
                key_buf[klen] = '\0';
                p = key_end + 1;

                /* Find colon and opening brace */
                while (*p && *p != '{') p++;
                if (!*p) break;
                p++;

                /* Find "node" : N */
                const char *node_key = strstr(p, "\"node\"");
                if (!node_key) break;
                const char *nv = node_key + 6;
                while (*nv && (*nv == ' ' || *nv == ':' || *nv == '\t')) nv++;
                int node_idx = atoi(nv);

                /* Skip to closing brace */
                const char *cb = strchr(nv, '}');
                if (!cb) break;
                p = cb + 1;

                memcpy(map[map_count].humanoid_name, key_buf, klen + 1);
                if (node_idx >= 0 && node_idx < total_nodes)
                    snprintf(map[map_count].node_name, 128, "%s", node_names[node_idx]);
                else
                    snprintf(map[map_count].node_name, 128, "node_%d", node_idx);

                map_count++;
            }
        }

        if (map_count == 0)
            hb_ptr = strstr(hb_ptr + 12, "\"humanBones\"");
        else
            break;
    }

    free(json_str);

    if (map_count > 0) {
        model->humanoid_map = (vrm_humanoid_map_t *)realloc(map, map_count * sizeof(vrm_humanoid_map_t));
        model->humanoid_map_count = map_count;
        printf("[vrm_loader] humanoid bone map: %d entries\n", map_count);
        for (int i = 0; i < map_count && i < 5; i++)
            printf("[vrm_loader]   %s -> %s\n", model->humanoid_map[i].humanoid_name,
                   model->humanoid_map[i].node_name);
        if (map_count > 5) printf("[vrm_loader]   ... +%d more\n", map_count - 5);
    } else {
        free(map);
    }
}

/* ================================================================== */
/*  Embedded animation extraction from Assimp                         */
/* ================================================================== */
