/**
 * @file vrm_loader_vrma.c
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

int vrm_load_vrma(vrm_model_t *model, const char *vrma_path)
{
    printf("[vrm_loader] loading VRMA: %s\n", vrma_path);

    /* ---- Read glTF JSON from VRMA file ---- */
    FILE *fp = fopen(vrma_path, "rb");
    if (!fp) { fprintf(stderr, "[vrm_loader] cannot open VRMA: %s\n", vrma_path); return -1; }

    unsigned char hdr[12];
    if (fread(hdr, 1, 12, fp) != 12) { fclose(fp); return -1; }
    if (hdr[0] != 'g' || hdr[1] != 'l' || hdr[2] != 'T' || hdr[3] != 'F') {
        fclose(fp); return -1;
    }

    /* Read total file */
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    unsigned char *file_buf = (unsigned char *)malloc(file_size);
    if (!file_buf) { fclose(fp); return -1; }
    if ((long)fread(file_buf, 1, file_size, fp) != file_size) {
        free(file_buf); fclose(fp); return -1;
    }
    fclose(fp);

    /* Parse glTF header */
    uint32_t json_chunk_len, json_chunk_type;
    memcpy(&json_chunk_len, file_buf + 12, 4);
    memcpy(&json_chunk_type, file_buf + 16, 4);
    if (json_chunk_type != 0x4E4F534A) { free(file_buf); return -1; }

    char *json_str = (char *)malloc(json_chunk_len + 1);
    memcpy(json_str, file_buf + 20, json_chunk_len);
    json_str[json_chunk_len] = '\0';

    /* Binary chunk */
    uint32_t bin_offset = 20 + json_chunk_len;
    /* Align to 4 bytes */
    while (bin_offset % 4 != 0) bin_offset++;

    unsigned char *bin_data = NULL;
    uint32_t bin_len = 0;
    if (bin_offset + 8 <= (uint32_t)file_size) {
        memcpy(&bin_len, file_buf + bin_offset, 4);
        uint32_t bin_type;
        memcpy(&bin_type, file_buf + bin_offset + 4, 4);
        if (bin_type == 0x004E4942) { /* 'BIN\0' */
            bin_data = file_buf + bin_offset + 8;
        }
    }

    if (!bin_data) {
        fprintf(stderr, "[vrm_loader] VRMA: no binary chunk found\n");
        free(json_str); free(file_buf);
        return -1;
    }

    /* ---- Parse VRMA node names and humanoid mapping ---- */
    /* Parse nodes array to get VRMA node names + rest rotation/translation + children */
    char vrma_node_names[512][128];
    float vrma_node_rot[512][4];   /* rest rotation (x,y,z,w) */
    float vrma_node_trans[512][3]; /* rest translation */
    int vrma_node_parent[512];     /* parent index (-1 if root) */
    float vrma_world_rot[512][4];  /* computed world rotation */
    int vrma_node_children[512][64]; /* children indices per node */
    int vrma_node_child_count[512];
    int vrma_node_count = 0;
    for (int _i = 0; _i < 512; _i++) {
        vrma_node_rot[_i][0] = vrma_node_rot[_i][1] = vrma_node_rot[_i][2] = 0.0f;
        vrma_node_rot[_i][3] = 1.0f;
        vrma_node_trans[_i][0] = vrma_node_trans[_i][1] = vrma_node_trans[_i][2] = 0.0f;
        vrma_node_parent[_i] = -1;
        vrma_world_rot[_i][0] = vrma_world_rot[_i][1] = vrma_world_rot[_i][2] = 0.0f;
        vrma_world_rot[_i][3] = 1.0f;
        vrma_node_child_count[_i] = 0;
    }

    const char *nodes_key = strstr(json_str, "\"nodes\"");
    if (nodes_key) {
        const char *arr = strchr(nodes_key + 7, '[');
        if (arr) {
            const char *p = arr + 1;
            while (*p && *p != ']' && vrma_node_count < 512) {
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

                char found_name[128] = "";
                const char *name_key = strstr(obj, "\"name\"");
                if (name_key && name_key < obj_end) {
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
                }
                snprintf(vrma_node_names[vrma_node_count], 128, "%s", found_name);

                /* Parse "rotation":[x,y,z,w] if present */
                const char *rot_k = strstr(obj, "\"rotation\"");
                if (rot_k && rot_k < obj_end) {
                    const char *ra = strchr(rot_k + 10, '[');
                    if (ra && ra < obj_end) {
                        ra++;
                        vrma_node_rot[vrma_node_count][0] = (float)atof(ra);
                        const char *c1 = strchr(ra, ',');
                        if (c1) { vrma_node_rot[vrma_node_count][1] = (float)atof(c1+1);
                        const char *c2 = strchr(c1+1, ',');
                        if (c2) { vrma_node_rot[vrma_node_count][2] = (float)atof(c2+1);
                        const char *c3 = strchr(c2+1, ',');
                        if (c3) { vrma_node_rot[vrma_node_count][3] = (float)atof(c3+1);
                        }}}
                    }
                }
                /* Parse "translation":[x,y,z] if present */
                const char *tr_k = strstr(obj, "\"translation\"");
                if (tr_k && tr_k < obj_end) {
                    const char *ta = strchr(tr_k + 13, '[');
                    if (ta && ta < obj_end) {
                        ta++;
                        vrma_node_trans[vrma_node_count][0] = (float)atof(ta);
                        const char *c1 = strchr(ta, ',');
                        if (c1) { vrma_node_trans[vrma_node_count][1] = (float)atof(c1+1);
                        const char *c2 = strchr(c1+1, ',');
                        if (c2) { vrma_node_trans[vrma_node_count][2] = (float)atof(c2+1);
                        }}
                    }
                }

                /* Parse "children":[i,j,...] if present */
                const char *ch_k = strstr(obj, "\"children\"");
                if (ch_k && ch_k < obj_end) {
                    const char *ca = strchr(ch_k + 10, '[');
                    if (ca && ca < obj_end) {
                        ca++;
                        while (*ca && *ca != ']' && vrma_node_child_count[vrma_node_count] < 64) {
                            while (*ca == ' ' || *ca == '\t' || *ca == '\n' || *ca == '\r' || *ca == ',') ca++;
                            if (*ca == ']') break;
                            int child_idx = atoi(ca);
                            vrma_node_children[vrma_node_count][vrma_node_child_count[vrma_node_count]++] = child_idx;
                            while (*ca && *ca != ',' && *ca != ']') ca++;
                        }
                    }
                }

                vrma_node_count++;
                p = obj_end + 1;
            }
        }
    }

    /* ---- Build parent map and compute world rotations ---- */
    /* From children lists, derive parent indices */
    for (int i = 0; i < vrma_node_count; i++) {
        for (int c = 0; c < vrma_node_child_count[i]; c++) {
            int ch = vrma_node_children[i][c];
            if (ch >= 0 && ch < vrma_node_count)
                vrma_node_parent[ch] = i;
        }
    }
    /* Compute world rotations via BFS (parent indices can be higher than children in glTF!) */
    {
        int queue[512];
        int q_head = 0, q_tail = 0;
        /* Enqueue root nodes */
        for (int i = 0; i < vrma_node_count; i++) {
            if (vrma_node_parent[i] < 0) {
                memcpy(vrma_world_rot[i], vrma_node_rot[i], 4 * sizeof(float));
                queue[q_tail++] = i;
            }
        }
        /* BFS: process children after parents */
        while (q_head < q_tail) {
            int node = queue[q_head++];
            for (int c = 0; c < vrma_node_child_count[node]; c++) {
                int ch = vrma_node_children[node][c];
                if (ch >= 0 && ch < vrma_node_count) {
                    vrm_loader_quat_multiply(vrma_world_rot[ch], vrma_world_rot[node], vrma_node_rot[ch]);
                    queue[q_tail++] = ch;
                }
            }
        }
    }

    /* ---- Parse VRMC_vrm_animation humanBones mapping ---- */
    /* Maps: humanoid_name -> vrma_node_index */
    typedef struct { char humanoid[64]; int vrma_node; } vrma_bone_map_t;
    vrma_bone_map_t vrma_map[128];
    int vrma_map_count = 0;

    const char *vrm_anim = strstr(json_str, "\"VRMC_vrm_animation\"");
    if (vrm_anim) {
        const char *hb = strstr(vrm_anim, "\"humanBones\"");
        if (hb) {
            const char *p = hb + 12;
            while (*p && *p != '{') p++;
            if (*p == '{') {
                p++;
                while (*p && vrma_map_count < 128) {
                    while (*p && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t' || *p == ',')) p++;
                    if (*p == '}') break;
                    if (*p != '"') { p++; continue; }
                    p++;
                    const char *key_end = strchr(p, '"');
                    if (!key_end) break;
                    int klen = (int)(key_end - p);
                    if (klen >= 64) klen = 63;
                    memcpy(vrma_map[vrma_map_count].humanoid, p, klen);
                    vrma_map[vrma_map_count].humanoid[klen] = '\0';
                    p = key_end + 1;

                    while (*p && *p != '{') p++;
                    if (!*p) break;
                    p++;
                    const char *nk = strstr(p, "\"node\"");
                    if (!nk) break;
                    const char *nv = nk + 6;
                    while (*nv && (*nv == ' ' || *nv == ':' || *nv == '\t')) nv++;
                    vrma_map[vrma_map_count].vrma_node = atoi(nv);

                    const char *cb = strchr(nv, '}');
                    if (!cb) break;
                    p = cb + 1;

                    vrma_map_count++;
                }
            }
        }
    }

    printf("[vrm_loader] VRMA humanoid map: %d entries, %d nodes\n", vrma_map_count, vrma_node_count);

    /* ---- Build retarget map: VRMA node index -> model bone index ---- */
    /* VRMA humanoid name -> model's humanoid map -> model node name -> model bone index */
    int retarget[512];
    memset(retarget, -1, sizeof(retarget));

    for (int i = 0; i < vrma_map_count; i++) {
        const char *hname = vrma_map[i].humanoid;
        int vrma_ni = vrma_map[i].vrma_node;
        if (vrma_ni < 0 || vrma_ni >= vrma_node_count) continue;

        /* Find model's node name for this humanoid bone */
        const char *model_node_name = NULL;
        for (uint32_t j = 0; j < model->humanoid_map_count; j++) {
            if (strcmp(model->humanoid_map[j].humanoid_name, hname) == 0) {
                model_node_name = model->humanoid_map[j].node_name;
                break;
            }
        }
        if (!model_node_name) continue;

        /* Find bone index in model by node name */
        int bone_idx = vrm_loader_find_bone_by_name(model_node_name);
        if (bone_idx >= 0) {
            retarget[vrma_ni] = bone_idx;
        }
    }

    int mapped = 0;
    for (int i = 0; i < vrma_node_count; i++)
        if (retarget[i] >= 0) mapped++;
    printf("[vrm_loader] VRMA retarget: %d/%d bones mapped\n", mapped, vrma_map_count);

    /* ---- Detect coordinate system mismatch (X/Z flip) ---- */
    /* VRM standard: left-side bones should have +X translation.
     * Detect purely from the MODEL's bone structure. */
    int coord_flip_xz = 0;
    {
        float score = 0.0f;
        int checks = 0;
        for (int i = 0; i < vrma_map_count; i++) {
            const char *hname = vrma_map[i].humanoid;
            int vrma_ni = vrma_map[i].vrma_node;
            if (vrma_ni < 0 || vrma_ni >= vrma_node_count) continue;
            int bone_idx = retarget[vrma_ni];
            if (bone_idx < 0) continue;
            float mt[3], mq[4], ms[3];
            vrm_loader_mat4_decompose(model->bones[bone_idx].local_transform, mt, mq, ms);
            if (strncmp(hname, "left", 4) == 0 && fabsf(mt[0]) > 0.01f) {
                score += (mt[0] < 0) ? -1.0f : 1.0f; checks++;
            }
            if (strncmp(hname, "right", 5) == 0 && fabsf(mt[0]) > 0.01f) {
                score += (mt[0] > 0) ? -1.0f : 1.0f; checks++;
            }
        }
        if (checks > 2 && score < 0) {
            coord_flip_xz = 1;
            printf("[vrm_loader] VRMA: model needs 180° Y correction\n");
        }
    }

    /* ---- Compute model bone rest world rotations for VRM 1.0 retarget ---- */
    /* We need the model's world-space rest rotations to properly transform
     * the world-space delta into each bone's parent local space. */
    float (*model_bone_world_rot)[4] = NULL;
    if (!coord_flip_xz) {
        model_bone_world_rot = (float (*)[4])calloc(model->bone_count, 4 * sizeof(float));
        for (uint32_t bi = 0; bi < model->bone_count; bi++) {
            float _t[3], lq[4], _s[3];
            vrm_loader_mat4_decompose(model->bones[bi].local_transform, _t, lq, _s);
            if (model->bones[bi].parent < 0) {
                memcpy(model_bone_world_rot[bi], lq, 4 * sizeof(float));
            } else {
                vrm_loader_quat_multiply(model_bone_world_rot[bi],
                                model_bone_world_rot[model->bones[bi].parent], lq);
            }
        }
    }

    /* ---- Parse accessors and bufferViews from JSON ---- */
    typedef struct {
        int buffer_view;
        int component_type; /* 5126=float, 5123=ushort, etc. */
        int count;
        int type_size;      /* 1=SCALAR, 3=VEC3, 4=VEC4 */
        float min_val, max_val;
    } accessor_t;

    accessor_t accessors[2048];
    int accessor_count = 0;

    const char *acc_key = strstr(json_str, "\"accessors\"");
    if (acc_key) {
        const char *arr = strchr(acc_key + 11, '[');
        if (arr) {
            const char *p = arr + 1;
            while (*p && *p != ']' && accessor_count < 2048) {
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

                accessor_t *a = &accessors[accessor_count];
                memset(a, 0, sizeof(*a));
                a->buffer_view = -1;

                /* Parse fields */
                const char *fld;
                fld = strstr(obj, "\"bufferView\"");
                if (fld && fld < obj_end) {
                    const char *v = fld + 12;
                    while (*v && (*v == ' ' || *v == ':' || *v == '\t')) v++;
                    a->buffer_view = atoi(v);
                }
                fld = strstr(obj, "\"componentType\"");
                if (fld && fld < obj_end) {
                    const char *v = fld + 15;
                    while (*v && (*v == ' ' || *v == ':' || *v == '\t')) v++;
                    a->component_type = atoi(v);
                }
                fld = strstr(obj, "\"count\"");
                if (fld && fld < obj_end) {
                    const char *v = fld + 7;
                    while (*v && (*v == ' ' || *v == ':' || *v == '\t')) v++;
                    a->count = atoi(v);
                }
                fld = strstr(obj, "\"type\"");
                if (fld && fld < obj_end) {
                    const char *v = fld + 6;
                    while (*v && (*v == ' ' || *v == ':' || *v == '\t' || *v == '"')) v++;
                    if (strncmp(v, "SCALAR", 6) == 0) a->type_size = 1;
                    else if (strncmp(v, "VEC4", 4) == 0) a->type_size = 4;
                    else if (strncmp(v, "VEC3", 4) == 0) a->type_size = 3;
                    else if (strncmp(v, "VEC2", 4) == 0) a->type_size = 2;
                    else a->type_size = 1;
                }
                /* Parse min/max for time range */
                fld = strstr(obj, "\"max\"");
                if (fld && fld < obj_end) {
                    const char *v = strchr(fld + 4, '[');
                    if (v && v < obj_end) {
                        a->max_val = (float)atof(v + 1);
                    } else {
                        v = fld + 4;
                        while (*v && (*v == ' ' || *v == ':' || *v == '\t')) v++;
                        a->max_val = (float)atof(v);
                    }
                }

                accessor_count++;
                p = obj_end + 1;
            }
        }
    }

    typedef struct {
        int buffer;
        int byte_offset;
        int byte_length;
        int byte_stride;
    } buffer_view_t;

    buffer_view_t buffer_views[2048];
    int bv_count = 0;

    const char *bv_key = strstr(json_str, "\"bufferViews\"");
    if (bv_key) {
        const char *arr = strchr(bv_key + 13, '[');
        if (arr) {
            const char *p = arr + 1;
            while (*p && *p != ']' && bv_count < 2048) {
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

                buffer_view_t *bv = &buffer_views[bv_count];
                memset(bv, 0, sizeof(*bv));

                const char *fld;
                fld = strstr(obj, "\"buffer\"");
                if (fld && fld < obj_end) {
                    const char *v = fld + 8;
                    while (*v && (*v == ' ' || *v == ':' || *v == '\t')) v++;
                    bv->buffer = atoi(v);
                }
                fld = strstr(obj, "\"byteOffset\"");
                if (fld && fld < obj_end) {
                    const char *v = fld + 12;
                    while (*v && (*v == ' ' || *v == ':' || *v == '\t')) v++;
                    bv->byte_offset = atoi(v);
                }
                fld = strstr(obj, "\"byteLength\"");
                if (fld && fld < obj_end) {
                    const char *v = fld + 12;
                    while (*v && (*v == ' ' || *v == ':' || *v == '\t')) v++;
                    bv->byte_length = atoi(v);
                }
                fld = strstr(obj, "\"byteStride\"");
                if (fld && fld < obj_end) {
                    const char *v = fld + 12;
                    while (*v && (*v == ' ' || *v == ':' || *v == '\t')) v++;
                    bv->byte_stride = atoi(v);
                }

                bv_count++;
                p = obj_end + 1;
            }
        }
    }

    /* Helper: read float data from an accessor */
    #define READ_ACCESSOR_FLOATS(acc_idx, out_ptr) do { \
        if ((acc_idx) >= 0 && (acc_idx) < accessor_count) { \
            accessor_t *_a = &accessors[acc_idx]; \
            if (_a->buffer_view >= 0 && _a->buffer_view < bv_count) { \
                buffer_view_t *_bv = &buffer_views[_a->buffer_view]; \
                int _offset = _bv->byte_offset; \
                if (_offset + _a->count * _a->type_size * (int)sizeof(float) <= (int)bin_len) { \
                    out_ptr = (float *)(bin_data + _offset); \
                } \
            } \
        } \
    } while(0)

    /* ---- Parse animations from JSON ---- */
    const char *anim_key = strstr(json_str, "\"animations\"");
    if (!anim_key) {
        fprintf(stderr, "[vrm_loader] VRMA: no animations found\n");
        free(json_str); free(file_buf);
        return -1;
    }

    /* For VRMA, there's typically one animation. Parse samplers and channels. */
    typedef struct { int input; int output; int interpolation; } sampler_t;
    typedef struct { int sampler; int target_node; int target_path; } channel_t;

    sampler_t samplers[1024];
    int sampler_count = 0;
    channel_t channels[1024];
    int channel_count = 0;

    /* Find the first animation object */
    const char *anim_arr = strchr(anim_key + 12, '[');
    if (anim_arr) {
        const char *anim_obj = strchr(anim_arr, '{');
        if (anim_obj) {
            /* Find the end of this animation object */
            int depth = 1;
            const char *q = anim_obj + 1;
            const char *anim_end = NULL;
            while (*q && depth > 0) {
                if (*q == '{') depth++;
                else if (*q == '}') { depth--; if (depth == 0) { anim_end = q; break; } }
                q++;
            }

            if (anim_end) {
                /* Parse samplers */
                const char *samp_key = strstr(anim_obj, "\"samplers\"");
                if (samp_key && samp_key < anim_end) {
                    const char *sarr = strchr(samp_key + 10, '[');
                    if (sarr) {
                        const char *p = sarr + 1;
                        while (*p && *p != ']' && sampler_count < 1024) {
                            const char *obj = strchr(p, '{');
                            if (!obj || obj > anim_end) break;
                            const char *oe = strchr(obj + 1, '}');
                            if (!oe) break;

                            sampler_t *s = &samplers[sampler_count];
                            s->input = -1; s->output = -1; s->interpolation = 0;

                            const char *fld;
                            fld = strstr(obj, "\"input\"");
                            if (fld && fld < oe) {
                                const char *v = fld + 7;
                                while (*v && (*v == ' ' || *v == ':' || *v == '\t')) v++;
                                s->input = atoi(v);
                            }
                            fld = strstr(obj, "\"output\"");
                            if (fld && fld < oe) {
                                const char *v = fld + 8;
                                while (*v && (*v == ' ' || *v == ':' || *v == '\t')) v++;
                                s->output = atoi(v);
                            }

                            sampler_count++;
                            p = oe + 1;
                        }
                    }
                }

                /* Parse channels */
                const char *chan_key = strstr(anim_obj, "\"channels\"");
                if (chan_key && chan_key < anim_end) {
                    const char *carr = strchr(chan_key + 10, '[');
                    if (carr) {
                        const char *p = carr + 1;
                        while (*p && *p != ']' && channel_count < 1024) {
                            const char *obj = strchr(p, '{');
                            if (!obj || obj > anim_end) break;

                            /* Find end of this channel object */
                            int d2 = 1;
                            const char *qq = obj + 1;
                            const char *oe = NULL;
                            while (*qq && d2 > 0) {
                                if (*qq == '{') d2++;
                                else if (*qq == '}') { d2--; if (d2 == 0) { oe = qq; break; } }
                                qq++;
                            }
                            if (!oe) break;

                            channel_t *c = &channels[channel_count];
                            c->sampler = -1; c->target_node = -1; c->target_path = -1;

                            const char *fld;
                            fld = strstr(obj, "\"sampler\"");
                            if (fld && fld < oe) {
                                const char *v = fld + 9;
                                while (*v && (*v == ' ' || *v == ':' || *v == '\t')) v++;
                                c->sampler = atoi(v);
                            }

                            /* target.node and target.path */
                            const char *tgt = strstr(obj, "\"target\"");
                            if (tgt && tgt < oe) {
                                const char *tn = strstr(tgt, "\"node\"");
                                if (tn && tn < oe) {
                                    const char *v = tn + 6;
                                    while (*v && (*v == ' ' || *v == ':' || *v == '\t')) v++;
                                    c->target_node = atoi(v);
                                }
                                const char *tp = strstr(tgt, "\"path\"");
                                if (tp && tp < oe) {
                                    const char *v = strchr(tp + 6, '"');
                                    if (v) {
                                        v++;
                                        if (strncmp(v, "translation", 11) == 0) c->target_path = 0;
                                        else if (strncmp(v, "rotation", 8) == 0) c->target_path = 1;
                                        else if (strncmp(v, "scale", 5) == 0) c->target_path = 2;
                                    }
                                }
                            }

                            channel_count++;
                            p = oe + 1;
                        }
                    }
                }
            }
        }
    }

    printf("[vrm_loader] VRMA: %d samplers, %d channels\n", sampler_count, channel_count);

    /* ---- Build animation ---- */
    /* Group channels by target node, then by bone */
    /* First, find duration */
    float duration = 0.0f;
    for (int i = 0; i < sampler_count; i++) {
        int input_acc = samplers[i].input;
        if (input_acc >= 0 && input_acc < accessor_count) {
            if (accessors[input_acc].max_val > duration)
                duration = accessors[input_acc].max_val;
        }
    }

    /* Create the animation */
    uint32_t new_anim_idx = model->animation_count;
    model->animations = (vrm_animation_t *)realloc(
        model->animations, (new_anim_idx + 1) * sizeof(vrm_animation_t));
    vrm_animation_t *va = &model->animations[new_anim_idx];
    memset(va, 0, sizeof(*va));
    model->animation_count = new_anim_idx + 1;

    snprintf(va->name, sizeof(va->name), "VRMA");
    va->duration = duration;

    /* Group channels by target_node -> bone_index */
    /* Temporary: count unique target bones */
    int unique_bones[512];
    int unique_bone_count = 0;

    for (int ci = 0; ci < channel_count; ci++) {
        int tn = channels[ci].target_node;
        if (tn < 0 || tn >= vrma_node_count) continue;
        int bone_idx = retarget[tn];
        if (bone_idx < 0) continue;

        /* Check if already in list */
        int found = 0;
        for (int j = 0; j < unique_bone_count; j++) {
            if (unique_bones[j] == bone_idx) { found = 1; break; }
        }
        if (!found && unique_bone_count < 512)
            unique_bones[unique_bone_count++] = bone_idx;
    }

    va->bone_anims = (vrm_bone_anim_t *)calloc(unique_bone_count, sizeof(vrm_bone_anim_t));
    va->bone_anim_count = unique_bone_count;

    for (int bi = 0; bi < unique_bone_count; bi++) {
        vrm_bone_anim_t *ba = &va->bone_anims[bi];
        ba->bone_index = unique_bones[bi];

        /* Count channels for this bone */
        int nch = 0;
        for (int ci = 0; ci < channel_count; ci++) {
            int tn = channels[ci].target_node;
            if (tn < 0 || tn >= vrma_node_count) continue;
            if (retarget[tn] != unique_bones[bi]) continue;
            nch++;
        }

        ba->channels = (vrm_anim_channel_t *)calloc(nch, sizeof(vrm_anim_channel_t));
        ba->channel_count = 0;

        for (int ci = 0; ci < channel_count; ci++) {
            int tn = channels[ci].target_node;
            if (tn < 0 || tn >= vrma_node_count) continue;
            if (retarget[tn] != unique_bones[bi]) continue;
            if (channels[ci].sampler < 0 || channels[ci].sampler >= sampler_count) continue;

            sampler_t *smp = &samplers[channels[ci].sampler];
            if (smp->input < 0 || smp->output < 0) continue;
            if (smp->input >= accessor_count || smp->output >= accessor_count) continue;

            accessor_t *in_acc = &accessors[smp->input];
            accessor_t *out_acc = &accessors[smp->output];

            /* Read time data */
            float *time_data = NULL;
            READ_ACCESSOR_FLOATS(smp->input, time_data);
            if (!time_data) continue;

            /* Read value data */
            float *val_data = NULL;
            READ_ACCESSOR_FLOATS(smp->output, val_data);
            if (!val_data) continue;

            vrm_anim_channel_t *ac = &ba->channels[ba->channel_count++];
            ac->path = channels[ci].target_path;
            ac->count = in_acc->count;

            /* Copy time data */
            ac->times = (float *)malloc(ac->count * sizeof(float));
            memcpy(ac->times, time_data, ac->count * sizeof(float));

            /* Copy value data */
            int val_size = (ac->path == 1) ? 4 : 3; /* rotation=quat(4), others=vec3(3) */
            ac->values = (float *)malloc(ac->count * val_size * sizeof(float));
            memcpy(ac->values, val_data, ac->count * val_size * sizeof(float));

            /* ---- Apply VRMA retarget correction ---- */
            /* three-vrm algorithm:
             * Q_normalized = Q_parent_world_vrma * Q_anim * inv(Q_bone_world_vrma)
             * This normalization is needed for ALL models (VRM 0.x and 1.0).
             * Then for VRM 0.x model ONLY: negate qx, qz (180° Y conjugation)
             *
             * For translation (hips only in VRMA):
             * t_normalized = hipsParentWorldMatrix * t_anim  (matrix transform)
             */
            if (ac->path == 1) {
                /* Rotation retarget using world rotations */
                float inv_bone_world[4];
                vrm_loader_quat_conjugate(inv_bone_world, vrma_world_rot[tn]);

                /* Parent world rotation */
                float parent_world[4] = {0, 0, 0, 1};
                if (vrma_node_parent[tn] >= 0) {
                    memcpy(parent_world, vrma_world_rot[vrma_node_parent[tn]], 4 * sizeof(float));
                }

                /* Get model bone's rest local rotation for VRM 1.0 composition */
                float model_rest_rot[4], _mrt[3], _mrs[3];
                vrm_loader_mat4_decompose(model->bones[unique_bones[bi]].local_transform,
                                 _mrt, model_rest_rot, _mrs);

                /* For VRM 1.0: precompute parent world rotation and its inverse */
                float model_pw[4] = {0, 0, 0, 1};
                float inv_model_pw[4] = {0, 0, 0, 1};
                if (!coord_flip_xz && model_bone_world_rot) {
                    int mbi = unique_bones[bi];
                    if (model->bones[mbi].parent >= 0) {
                        memcpy(model_pw, model_bone_world_rot[model->bones[mbi].parent],
                               4 * sizeof(float));
                    }
                    vrm_loader_quat_conjugate(inv_model_pw, model_pw);
                }

                for (uint32_t k = 0; k < ac->count; k++) {
                    float *qk = &ac->values[k * 4];
                    float t1[4], t2[4];
                    /* Q_world_delta = parent_world_vrma * Q_anim * inv(bone_world_vrma)
                     * This is a world-space delta (identity = no change from rest). */
                    vrm_loader_quat_multiply(t1, parent_world, qk);
                    vrm_loader_quat_multiply(t2, t1, inv_bone_world);
                    if (coord_flip_xz) {
                        /* 180° Y conjugation for VRM 0.x: negate x and z */
                        qk[0] = -t2[0];
                        qk[1] =  t2[1];
                        qk[2] = -t2[2];
                        qk[3] =  t2[3];
                    } else {
                        /* VRM 1.0: transform world delta into parent local space,
                         * then left-multiply with rest rotation.
                         * Q_local_delta = inv(parent_world) * Q_world_delta * parent_world
                         * Q_animated_local = Q_local_delta * Q_rest_local
                         * This ensures rotation axes stay in world alignment
                         * (e.g. spinning around world Y, not a tilted axis). */
                        float ld1[4], local_delta[4];
                        vrm_loader_quat_multiply(ld1, inv_model_pw, t2);
                        vrm_loader_quat_multiply(local_delta, ld1, model_pw);
                        float final_q[4];
                        vrm_loader_quat_multiply(final_q, local_delta, model_rest_rot);
                        qk[0] = final_q[0];
                        qk[1] = final_q[1];
                        qk[2] = final_q[2];
                        qk[3] = final_q[3];
                    }
                }
            }
            if (ac->path == 0) {
                /* Translation retarget (typically hips only):
                 * three-vrm: t_normalized = hipsParentWorldMatrix * t_anim
                 * Normalization is always needed. VRM 0.x additionally negates x,z. */
                float pw[4] = {0, 0, 0, 1};
                if (vrma_node_parent[tn] >= 0)
                    memcpy(pw, vrma_world_rot[vrma_node_parent[tn]], 4 * sizeof(float));

                /* Get model rest hips height for scale */
                float mt[3], mq[4], ms[3];
                vrm_loader_mat4_decompose(model->bones[unique_bones[bi]].local_transform,
                                 mt, mq, ms);
                /* VRMA rest hips position y-component (in world space) */
                float vrma_hy = vrma_node_trans[tn][1]; /* approximate */
                float scale = (vrma_hy > 0.01f) ? (mt[1] / vrma_hy) : 1.0f;

                for (uint32_t k = 0; k < ac->count; k++) {
                    float *tv = &ac->values[k * 3];
                    /* Rotate by parent world rotation: q * v * q^-1 */
                    float vx = tv[0], vy = tv[1], vz = tv[2];
                    /* Cross product part: t = 2 * (pw.xyz x v) */
                    float tx = 2.0f * (pw[1]*vz - pw[2]*vy);
                    float ty = 2.0f * (pw[2]*vx - pw[0]*vz);
                    float tz = 2.0f * (pw[0]*vy - pw[1]*vx);
                    /* result = v + w*t + (pw.xyz x t) */
                    float ox = vx + pw[3]*tx + (pw[1]*tz - pw[2]*ty);
                    float oy = vy + pw[3]*ty + (pw[2]*tx - pw[0]*tz);
                    float oz = vz + pw[3]*tz + (pw[0]*ty - pw[1]*tx);
                    if (coord_flip_xz) {
                        /* Negate x,z for VRM 0.x coord flip, scale */
                        tv[0] = -ox * scale;
                        tv[1] =  oy * scale;
                        tv[2] = -oz * scale;
                    } else {
                        /* VRM 1.0: apply rotation + scale, no coord flip */
                        tv[0] = ox * scale;
                        tv[1] = oy * scale;
                        tv[2] = oz * scale;
                    }
                }
            }
        }
    }

    printf("[vrm_loader] VRMA animation: duration=%.2fs, %d bone tracks, %d total channels\n",
           va->duration, unique_bone_count, channel_count);

    free(model_bone_world_rot);
    free(json_str);
    free(file_buf);
    return 0;

    #undef READ_ACCESSOR_FLOATS
}

/* ================================================================== */
/*  Animation evaluation                                               */
/* ================================================================== */

/** Find the two keyframes surrounding time t and return interpolation factor. */
