/**
 * @file vrm_loader_spring_parse.c
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

void vrm_loader_extract_vrm_spring_bones(vrm_model_t *model, const char *path)
{
    model->spring_groups = NULL;
    model->spring_group_count = 0;
    model->collider_groups = NULL;
    model->collider_group_count = 0;

    /* Read the glTF JSON chunk */
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

    /* ---- Try VRM 0.x: secondaryAnimation ---- */
    const char *sec_anim = strstr(json_str, "\"secondaryAnimation\"");
    if (sec_anim) {
        /* ---- Parse colliderGroups ---- */
        const char *cg_key = strstr(sec_anim, "\"colliderGroups\"");
        if (cg_key) {
            const char *cg_arr = strchr(cg_key + 16, '[');
            if (cg_arr) {
                /* Count collider groups */
                int max_cg = 64;
                model->collider_groups = (vrm_collider_group_t *)calloc(max_cg, sizeof(vrm_collider_group_t));

                const char *cp = cg_arr + 1;
                while (*cp && *cp != ']' && (int)model->collider_group_count < max_cg) {
                    const char *cobj = strchr(cp, '{');
                    if (!cobj) break;

                    /* Find matching end brace at depth 1 */
                    int depth = 1;
                    const char *cq = cobj + 1;
                    const char *cobj_end = NULL;
                    while (*cq && depth > 0) {
                        if (*cq == '{') depth++;
                        else if (*cq == '}') { depth--; if (depth == 0) { cobj_end = cq; break; } }
                        cq++;
                    }
                    if (!cobj_end) break;

                    vrm_collider_group_t *grp = &model->collider_groups[model->collider_group_count];

                    /* "node": N */
                    const char *nk = strstr(cobj, "\"node\"");
                    int node_idx = -1;
                    if (nk && nk < cobj_end) {
                        const char *v = nk + 6;
                        while (*v && (*v == ' ' || *v == ':' || *v == '\t')) v++;
                        node_idx = atoi(v);
                    }
                    grp->bone_index = vrm_loader_find_bone_by_node_index(model, json_str, node_idx);

                    /* "colliders": [{offset:{x,y,z}, radius:R}, ...] */
                    const char *col_key = strstr(cobj, "\"colliders\"");
                    if (col_key && col_key < cobj_end) {
                        const char *col_arr = strchr(col_key + 11, '[');
                        if (col_arr && col_arr < cobj_end) {
                            int max_cols = 16;
                            grp->colliders = (vrm_spring_collider_t *)calloc(max_cols, sizeof(vrm_spring_collider_t));

                            const char *colp = col_arr + 1;
                            while (*colp && *colp != ']' && (int)grp->collider_count < max_cols) {
                                const char *co = strchr(colp, '{');
                                if (!co || co > cobj_end) break;
                                const char *co_end = strchr(co + 1, '}');
                                if (!co_end) break;

                                vrm_spring_collider_t *col = &grp->colliders[grp->collider_count];

                                /* Parse offset */
                                const char *off_key = strstr(co, "\"offset\"");
                                if (off_key && off_key < co_end) {
                                    const char *ox = strstr(off_key, "\"x\"");
                                    const char *oy = strstr(off_key, "\"y\"");
                                    const char *oz = strstr(off_key, "\"z\"");
                                    if (ox && ox < co_end) { const char *v = ox+3; while(*v&&(*v==' '||*v==':'||*v=='\t'))v++; col->offset[0]=(float)atof(v); }
                                    if (oy && oy < co_end) { const char *v = oy+3; while(*v&&(*v==' '||*v==':'||*v=='\t'))v++; col->offset[1]=(float)atof(v); }
                                    if (oz && oz < co_end) { const char *v = oz+3; while(*v&&(*v==' '||*v==':'||*v=='\t'))v++; col->offset[2]=(float)atof(v); }
                                }

                                const char *rk = strstr(co, "\"radius\"");
                                if (rk && rk < co_end) {
                                    const char *v = rk + 8;
                                    while (*v && (*v == ' ' || *v == ':' || *v == '\t')) v++;
                                    col->radius = (float)atof(v);
                                }

                                grp->collider_count++;
                                colp = co_end + 1;
                            }
                        }
                    }

                    model->collider_group_count++;
                    cp = cobj_end + 1;
                }
            }
        }

        /* ---- Parse boneGroups ---- */
        const char *bg_key = strstr(sec_anim, "\"boneGroups\"");
        if (bg_key) {
            const char *bg_arr = strchr(bg_key + 12, '[');
            if (bg_arr) {
                int max_groups = VRM_MAX_SPRING_GROUPS;
                model->spring_groups = (vrm_spring_group_t *)calloc(max_groups, sizeof(vrm_spring_group_t));

                const char *bp = bg_arr + 1;
                while (*bp && *bp != ']' && (int)model->spring_group_count < max_groups) {
                    const char *bobj = strchr(bp, '{');
                    if (!bobj) break;

                    /* Find matching end brace */
                    int depth = 1;
                    const char *bq = bobj + 1;
                    const char *bobj_end = NULL;
                    while (*bq && depth > 0) {
                        if (*bq == '{') depth++;
                        else if (*bq == '}') { depth--; if (depth == 0) { bobj_end = bq; break; } }
                        bq++;
                    }
                    if (!bobj_end) break;

                    vrm_spring_group_t *sgrp = &model->spring_groups[model->spring_group_count];
                    sgrp->center_bone = -1;

                    /* Parse shared parameters */
                    float stiffness = 1.0f, gravity_power = 0.0f, drag_force = 0.5f, hit_radius = 0.0f;
                    float gravity_dir[3] = {0.0f, -1.0f, 0.0f};

                    const char *fld;
                    /* VRM 0.x uses "stiffiness" (yes, misspelled in spec) */
                    fld = strstr(bobj, "\"stiffiness\"");
                    if (!fld || fld > bobj_end) fld = strstr(bobj, "\"stiffness\"");
                    if (fld && fld < bobj_end) {
                        const char *v = fld + 12; while (*v && (*v==' '||*v==':'||*v=='\t')) v++;
                        stiffness = (float)atof(v);
                    }
                    fld = strstr(bobj, "\"gravityPower\"");
                    if (fld && fld < bobj_end) {
                        const char *v = fld + 14; while (*v && (*v==' '||*v==':'||*v=='\t')) v++;
                        gravity_power = (float)atof(v);
                    }
                    fld = strstr(bobj, "\"dragForce\"");
                    if (fld && fld < bobj_end) {
                        const char *v = fld + 11; while (*v && (*v==' '||*v==':'||*v=='\t')) v++;
                        drag_force = (float)atof(v);
                    }
                    fld = strstr(bobj, "\"hitRadius\"");
                    if (fld && fld < bobj_end) {
                        const char *v = fld + 11; while (*v && (*v==' '||*v==':'||*v=='\t')) v++;
                        hit_radius = (float)atof(v);
                    }
                    fld = strstr(bobj, "\"gravityDir\"");
                    if (fld && fld < bobj_end) {
                        const char *gx = strstr(fld, "\"x\"");
                        const char *gy = strstr(fld, "\"y\"");
                        const char *gz = strstr(fld, "\"z\"");
                        if (gx && gx < bobj_end) { const char *v=gx+3; while(*v&&(*v==' '||*v==':'||*v=='\t'))v++; gravity_dir[0]=(float)atof(v); }
                        if (gy && gy < bobj_end) { const char *v=gy+3; while(*v&&(*v==' '||*v==':'||*v=='\t'))v++; gravity_dir[1]=(float)atof(v); }
                        if (gz && gz < bobj_end) { const char *v=gz+3; while(*v&&(*v==' '||*v==':'||*v=='\t'))v++; gravity_dir[2]=(float)atof(v); }
                    }

                    /* "center": node index or -1 */
                    fld = strstr(bobj, "\"center\"");
                    if (fld && fld < bobj_end) {
                        const char *v = fld + 8; while (*v && (*v==' '||*v==':'||*v=='\t')) v++;
                        int center_node = atoi(v);
                        if (center_node >= 0)
                            sgrp->center_bone = vrm_loader_find_bone_by_node_index(model, json_str, center_node);
                    }

                    /* "colliderGroups": [indices...] */
                    fld = strstr(bobj, "\"colliderGroups\"");
                    if (fld && fld < bobj_end) {
                        const char *cga = strchr(fld + 16, '[');
                        if (cga && cga < bobj_end) {
                            int max_cgi = 32;
                            sgrp->collider_group_indices = (int *)calloc(max_cgi, sizeof(int));
                            const char *cgp = cga + 1;
                            while (*cgp && *cgp != ']' && (int)sgrp->collider_group_count < max_cgi) {
                                while (*cgp && (*cgp == ' ' || *cgp == ',' || *cgp == '\t' || *cgp == '\n')) cgp++;
                                if (*cgp == ']') break;
                                sgrp->collider_group_indices[sgrp->collider_group_count++] = atoi(cgp);
                                while (*cgp && *cgp != ',' && *cgp != ']') cgp++;
                            }
                        }
                    }

                    /* "bones": [node_indices...] — each is a root of a chain */
                    const char *bones_key = strstr(bobj, "\"bones\"");
                    if (bones_key && bones_key < bobj_end) {
                        const char *barr = strchr(bones_key + 7, '[');
                        if (barr && barr < bobj_end) {
                            /* Collect root node indices */
                            int root_nodes[256];
                            int root_count = 0;
                            const char *bnp = barr + 1;
                            while (*bnp && *bnp != ']' && root_count < 256) {
                                while (*bnp && (*bnp==' '||*bnp==','||*bnp=='\t'||*bnp=='\n')) bnp++;
                                if (*bnp == ']') break;
                                root_nodes[root_count++] = atoi(bnp);
                                while (*bnp && *bnp != ',' && *bnp != ']') bnp++;
                            }

                            /* Build chains from each root and collect all joints */
                            int total_joints = 0;
                            int chain_buf[256 * 64]; /* temp storage (large for branching trees) */
                            int chain_lengths[256];

                            for (int r = 0; r < root_count; r++) {
                                int root_bone = vrm_loader_find_bone_by_node_index(model, json_str, root_nodes[r]);
                                int max_remaining = (256 * 64) - total_joints;
                                if (max_remaining < 1) max_remaining = 1;
                                chain_lengths[r] = vrm_loader_build_bone_chain(model, root_bone,
                                    &chain_buf[total_joints], max_remaining);
                                total_joints += chain_lengths[r];
                            }

                            if (total_joints > 0) {
                                sgrp->joints = (vrm_spring_joint_t *)calloc(total_joints, sizeof(vrm_spring_joint_t));
                                sgrp->joint_count = total_joints;

                                for (int j = 0; j < total_joints; j++) {
                                    vrm_spring_joint_t *jnt = &sgrp->joints[j];
                                    jnt->bone_index = chain_buf[j];
                                    jnt->stiffness = stiffness;
                                    jnt->gravity_power = gravity_power;
                                    jnt->gravity_dir[0] = gravity_dir[0];
                                    jnt->gravity_dir[1] = gravity_dir[1];
                                    jnt->gravity_dir[2] = gravity_dir[2];
                                    jnt->drag_force = drag_force;
                                    jnt->hit_radius = hit_radius;
                                }
                            }
                        }
                    }

                    if (sgrp->joint_count > 0) {
                        model->spring_group_count++;
                    } else {
                        free(sgrp->collider_group_indices);
                        sgrp->collider_group_indices = NULL;
                        sgrp->collider_group_count = 0;
                    }

                    bp = bobj_end + 1;
                }
            }
        }
    }

    /* ---- Try VRM 1.0: VRMC_springBone extension ---- */
    if (!model->spring_group_count) {
        const char *vrmc_sb = strstr(json_str, "\"VRMC_springBone\"");
        if (vrmc_sb) {
            /* ---- Parse colliders[] (flat array) ---- */
            /* Each collider: {"node": N, "shape": {"sphere": {offset,radius}} or {"capsule": {offset,radius,tail}}} */
            /* We map each collider to a single-collider collider_group so the existing
             * spring bone solver can reference them by index. */

            typedef struct {
                int node_idx;        /* glTF node index */
                float offset[3];
                float radius;
                int is_capsule;
                float tail[3];       /* capsule tail offset (unused by current solver, stored for future) */
            } vrmc_collider_t;

            vrmc_collider_t parsed_colliders[256];
            int parsed_collider_count = 0;

            const char *col_key = strstr(vrmc_sb, "\"colliders\"");
            if (col_key) {
                const char *col_arr = strchr(col_key + 11, '[');
                if (col_arr) {
                    const char *cp = col_arr + 1;
                    while (*cp && *cp != ']' && parsed_collider_count < 256) {
                        const char *cobj = strchr(cp, '{');
                        if (!cobj) break;
                        int depth = 1;
                        const char *cq = cobj + 1;
                        const char *cobj_end = NULL;
                        while (*cq && depth > 0) {
                            if (*cq == '{') depth++;
                            else if (*cq == '}') { depth--; if (depth == 0) { cobj_end = cq; break; } }
                            cq++;
                        }
                        if (!cobj_end) break;

                        vrmc_collider_t *pc = &parsed_colliders[parsed_collider_count];
                        memset(pc, 0, sizeof(*pc));
                        pc->node_idx = -1;

                        /* "node": N */
                        const char *nk = strstr(cobj, "\"node\"");
                        if (nk && nk < cobj_end) {
                            const char *v = nk + 6;
                            while (*v && (*v == ' ' || *v == ':' || *v == '\t')) v++;
                            pc->node_idx = atoi(v);
                        }

                        /* "shape": { "sphere": {...} } or { "capsule": {...} } */
                        const char *sphere_key = strstr(cobj, "\"sphere\"");
                        const char *capsule_key = strstr(cobj, "\"capsule\"");

                        const char *shape_obj = NULL;
                        const char *shape_end = NULL;

                        if (sphere_key && sphere_key < cobj_end) {
                            shape_obj = strchr(sphere_key + 8, '{');
                            if (shape_obj && shape_obj < cobj_end) {
                                shape_end = strchr(shape_obj + 1, '}');
                                pc->is_capsule = 0;
                            }
                        } else if (capsule_key && capsule_key < cobj_end) {
                            shape_obj = strchr(capsule_key + 9, '{');
                            if (shape_obj && shape_obj < cobj_end) {
                                /* Find matching brace */
                                int d2 = 1;
                                const char *se = shape_obj + 1;
                                while (*se && d2 > 0) {
                                    if (*se == '{') d2++;
                                    else if (*se == '}') { d2--; if (d2 == 0) { shape_end = se; break; } }
                                    se++;
                                }
                                pc->is_capsule = 1;
                            }
                        }

                        if (shape_obj && shape_end) {
                            /* Parse "offset": [x,y,z] */
                            const char *off = strstr(shape_obj, "\"offset\"");
                            if (off && off < shape_end) {
                                const char *oa = strchr(off + 8, '[');
                                if (oa && oa < shape_end) {
                                    oa++;
                                    pc->offset[0] = (float)atof(oa);
                                    const char *c1 = strchr(oa, ',');
                                    if (c1) { pc->offset[1] = (float)atof(c1+1);
                                    const char *c2 = strchr(c1+1, ',');
                                    if (c2) { pc->offset[2] = (float)atof(c2+1); }}
                                }
                            }
                            /* Parse "radius": R */
                            const char *rk = strstr(shape_obj, "\"radius\"");
                            if (rk && rk < shape_end) {
                                const char *v = rk + 8;
                                while (*v && (*v == ' ' || *v == ':' || *v == '\t')) v++;
                                pc->radius = (float)atof(v);
                            }
                            /* Parse "tail": [x,y,z] for capsule */
                            if (pc->is_capsule) {
                                const char *tk = strstr(shape_obj, "\"tail\"");
                                if (tk && tk < shape_end) {
                                    const char *ta = strchr(tk + 6, '[');
                                    if (ta && ta < shape_end) {
                                        ta++;
                                        pc->tail[0] = (float)atof(ta);
                                        const char *c1 = strchr(ta, ',');
                                        if (c1) { pc->tail[1] = (float)atof(c1+1);
                                        const char *c2 = strchr(c1+1, ',');
                                        if (c2) { pc->tail[2] = (float)atof(c2+1); }}
                                    }
                                }
                            }
                        }

                        parsed_collider_count++;
                        cp = cobj_end + 1;
                    }
                }
            }

            /* ---- Parse colliderGroups[] ---- */
            /* VRM 1.0: {"name":"...", "colliders":[0,1,...]} — indices into the flat colliders array */
            typedef struct {
                char name[64];
                int collider_indices[32];
                int collider_count;
            } vrmc_collider_group_t;

            vrmc_collider_group_t parsed_cgroups[64];
            int parsed_cgroup_count = 0;

            const char *cg_key = strstr(vrmc_sb, "\"colliderGroups\"");
            if (cg_key) {
                const char *cg_arr = strchr(cg_key + 16, '[');
                if (cg_arr) {
                    const char *cgp = cg_arr + 1;
                    while (*cgp && *cgp != ']' && parsed_cgroup_count < 64) {
                        const char *cgobj = strchr(cgp, '{');
                        if (!cgobj) break;
                        int depth = 1;
                        const char *cq = cgobj + 1;
                        const char *cgobj_end = NULL;
                        while (*cq && depth > 0) {
                            if (*cq == '{') depth++;
                            else if (*cq == '}') { depth--; if (depth == 0) { cgobj_end = cq; break; } }
                            cq++;
                        }
                        if (!cgobj_end) break;

                        vrmc_collider_group_t *pcg = &parsed_cgroups[parsed_cgroup_count];
                        memset(pcg, 0, sizeof(*pcg));

                        /* "colliders": [0, 1, ...] */
                        const char *ci_key = strstr(cgobj, "\"colliders\"");
                        if (ci_key && ci_key < cgobj_end) {
                            const char *cia = strchr(ci_key + 11, '[');
                            if (cia && cia < cgobj_end) {
                                const char *cip = cia + 1;
                                while (*cip && *cip != ']' && pcg->collider_count < 32) {
                                    while (*cip && (*cip == ' ' || *cip == ',' || *cip == '\t' || *cip == '\n' || *cip == '\r')) cip++;
                                    if (*cip == ']') break;
                                    pcg->collider_indices[pcg->collider_count++] = atoi(cip);
                                    while (*cip && *cip != ',' && *cip != ']') cip++;
                                }
                            }
                        }

                        parsed_cgroup_count++;
                        cgp = cgobj_end + 1;
                    }
                }
            }

            /* ---- Build runtime collider_groups from parsed data ---- */
            /* Map each VRM 1.0 colliderGroup to a vrm_collider_group_t.
             * A group may reference multiple colliders on different nodes,
             * but our runtime struct has one bone_index per group.
             * We group by the first collider's node. */
            if (parsed_cgroup_count > 0) {
                model->collider_groups = (vrm_collider_group_t *)calloc(
                    parsed_cgroup_count, sizeof(vrm_collider_group_t));

                for (int gi = 0; gi < parsed_cgroup_count; gi++) {
                    vrmc_collider_group_t *pcg = &parsed_cgroups[gi];
                    vrm_collider_group_t *grp = &model->collider_groups[model->collider_group_count];

                    if (pcg->collider_count <= 0) continue;

                    /* Use the first collider's node as the group's bone */
                    int first_ci = pcg->collider_indices[0];
                    if (first_ci >= 0 && first_ci < parsed_collider_count) {
                        grp->bone_index = vrm_loader_find_bone_by_node_index(
                            model, json_str, parsed_colliders[first_ci].node_idx);
                    } else {
                        grp->bone_index = -1;
                    }

                    /* Allocate colliders for this group */
                    grp->colliders = (vrm_spring_collider_t *)calloc(
                        pcg->collider_count, sizeof(vrm_spring_collider_t));

                    for (int ci = 0; ci < pcg->collider_count; ci++) {
                        int idx = pcg->collider_indices[ci];
                        if (idx < 0 || idx >= parsed_collider_count) continue;
                        vrmc_collider_t *pc = &parsed_colliders[idx];
                        vrm_spring_collider_t *col = &grp->colliders[grp->collider_count];
                        col->offset[0] = pc->offset[0];
                        col->offset[1] = pc->offset[1];
                        col->offset[2] = pc->offset[2];
                        col->radius = pc->radius;
                        grp->collider_count++;
                    }

                    model->collider_group_count++;
                }
            }

            /* ---- Parse springs[] ---- */
            /* VRM 1.0: each spring has its own joints[] with per-joint parameters
             * and optional colliderGroups reference */
            const char *springs_key = strstr(vrmc_sb, "\"springs\"");
            if (springs_key) {
                const char *sp_arr = strchr(springs_key + 9, '[');
                if (sp_arr) {
                    int max_groups = VRM_MAX_SPRING_GROUPS;
                    model->spring_groups = (vrm_spring_group_t *)calloc(
                        max_groups, sizeof(vrm_spring_group_t));

                    const char *sp = sp_arr + 1;
                    while (*sp && *sp != ']' && (int)model->spring_group_count < max_groups) {
                        const char *sobj = strchr(sp, '{');
                        if (!sobj) break;
                        int depth = 1;
                        const char *sq = sobj + 1;
                        const char *sobj_end = NULL;
                        while (*sq && depth > 0) {
                            if (*sq == '{') depth++;
                            else if (*sq == '}') { depth--; if (depth == 0) { sobj_end = sq; break; } }
                            sq++;
                        }
                        if (!sobj_end) break;

                        vrm_spring_group_t *sgrp = &model->spring_groups[model->spring_group_count];
                        memset(sgrp, 0, sizeof(*sgrp));
                        sgrp->center_bone = -1;

                        /* "center": node_index (optional) */
                        const char *center_key = strstr(sobj, "\"center\"");
                        if (center_key && center_key < sobj_end) {
                            const char *v = center_key + 8;
                            while (*v && (*v == ' ' || *v == ':' || *v == '\t')) v++;
                            if (*v >= '0' && *v <= '9') {
                                int center_node = atoi(v);
                                sgrp->center_bone = vrm_loader_find_bone_by_node_index(
                                    model, json_str, center_node);
                            }
                        }

                        /* "colliderGroups": [indices into parsed_cgroups] */
                        const char *scg_key = strstr(sobj, "\"colliderGroups\"");
                        if (scg_key && scg_key < sobj_end) {
                            const char *scg_arr = strchr(scg_key + 16, '[');
                            if (scg_arr && scg_arr < sobj_end) {
                                int max_cgi = 32;
                                sgrp->collider_group_indices = (int *)calloc(max_cgi, sizeof(int));
                                const char *scgp = scg_arr + 1;
                                while (*scgp && *scgp != ']' && (int)sgrp->collider_group_count < max_cgi) {
                                    while (*scgp && (*scgp == ' ' || *scgp == ',' || *scgp == '\t' || *scgp == '\n' || *scgp == '\r')) scgp++;
                                    if (*scgp == ']') break;
                                    sgrp->collider_group_indices[sgrp->collider_group_count++] = atoi(scgp);
                                    while (*scgp && *scgp != ',' && *scgp != ']') scgp++;
                                }
                            }
                        }

                        /* "joints": [{node, stiffness, dragForce, hitRadius, gravityPower, gravityDir}, ...] */
                        const char *joints_key = strstr(sobj, "\"joints\"");
                        if (joints_key && joints_key < sobj_end) {
                            const char *jt_arr = strchr(joints_key + 8, '[');
                            if (jt_arr && jt_arr < sobj_end) {
                                /* Count joints first */
                                int joint_cap = 128;
                                vrm_spring_joint_t *joints = (vrm_spring_joint_t *)calloc(
                                    joint_cap, sizeof(vrm_spring_joint_t));
                                int jcount = 0;

                                const char *jp = jt_arr + 1;
                                while (*jp && *jp != ']' && jcount < joint_cap) {
                                    const char *jobj = strchr(jp, '{');
                                    if (!jobj || jobj > sobj_end) break;
                                    const char *jobj_end = strchr(jobj + 1, '}');
                                    if (!jobj_end || jobj_end > sobj_end) break;

                                    vrm_spring_joint_t *jnt = &joints[jcount];
                                    /* defaults */
                                    jnt->stiffness = 1.0f;
                                    jnt->drag_force = 0.5f;
                                    jnt->gravity_power = 0.0f;
                                    jnt->gravity_dir[0] = 0.0f;
                                    jnt->gravity_dir[1] = -1.0f;
                                    jnt->gravity_dir[2] = 0.0f;
                                    jnt->hit_radius = 0.0f;
                                    jnt->bone_index = -1;

                                    const char *fld;
                                    fld = strstr(jobj, "\"node\"");
                                    if (fld && fld < jobj_end) {
                                        const char *v = fld + 6;
                                        while (*v && (*v == ' ' || *v == ':' || *v == '\t')) v++;
                                        int ni = atoi(v);
                                        jnt->bone_index = vrm_loader_find_bone_by_node_index(
                                            model, json_str, ni);
                                    }
                                    fld = strstr(jobj, "\"stiffness\"");
                                    if (fld && fld < jobj_end) {
                                        const char *v = fld + 11;
                                        while (*v && (*v == ' ' || *v == ':' || *v == '\t')) v++;
                                        jnt->stiffness = (float)atof(v);
                                    }
                                    fld = strstr(jobj, "\"dragForce\"");
                                    if (fld && fld < jobj_end) {
                                        const char *v = fld + 10;
                                        while (*v && (*v == ' ' || *v == ':' || *v == '\t')) v++;
                                        jnt->drag_force = (float)atof(v);
                                    }
                                    fld = strstr(jobj, "\"hitRadius\"");
                                    if (fld && fld < jobj_end) {
                                        const char *v = fld + 11;
                                        while (*v && (*v == ' ' || *v == ':' || *v == '\t')) v++;
                                        jnt->hit_radius = (float)atof(v);
                                    }
                                    fld = strstr(jobj, "\"gravityPower\"");
                                    if (fld && fld < jobj_end) {
                                        const char *v = fld + 14;
                                        while (*v && (*v == ' ' || *v == ':' || *v == '\t')) v++;
                                        jnt->gravity_power = (float)atof(v);
                                    }
                                    fld = strstr(jobj, "\"gravityDir\"");
                                    if (fld && fld < jobj_end) {
                                        /* Parse [x,y,z] */
                                        const char *ga = strchr(fld + 12, '[');
                                        if (ga && ga < jobj_end) {
                                            ga++;
                                            jnt->gravity_dir[0] = (float)atof(ga);
                                            const char *c1 = strchr(ga, ',');
                                            if (c1) { jnt->gravity_dir[1] = (float)atof(c1+1);
                                            const char *c2 = strchr(c1+1, ',');
                                            if (c2) { jnt->gravity_dir[2] = (float)atof(c2+1); }}
                                        }
                                    }

                                    jcount++;
                                    jp = jobj_end + 1;
                                }

                                if (jcount > 0) {
                                    sgrp->joints = (vrm_spring_joint_t *)realloc(
                                        joints, jcount * sizeof(vrm_spring_joint_t));
                                    sgrp->joint_count = jcount;
                                } else {
                                    free(joints);
                                }
                            }
                        }

                        if (sgrp->joint_count > 0) {
                            model->spring_group_count++;
                        } else {
                            free(sgrp->collider_group_indices);
                            sgrp->collider_group_indices = NULL;
                            sgrp->collider_group_count = 0;
                        }

                        sp = sobj_end + 1;
                    }
                }
            }

            printf("[vrm_loader] VRM 1.0 spring bones parsed: %d colliders, %d collider groups\n",
                   parsed_collider_count, parsed_cgroup_count);
        }
    }

    free(json_str);

    /* Log summary */
    uint32_t total_joints = 0;
    for (uint32_t i = 0; i < model->spring_group_count; i++)
        total_joints += model->spring_groups[i].joint_count;

    if (model->spring_group_count > 0) {
        printf("[vrm_loader] spring bones: %u groups, %u joints, %u collider groups\n",
               model->spring_group_count, total_joints, model->collider_group_count);
        for (uint32_t i = 0; i < model->spring_group_count; i++) {
            vrm_spring_group_t *sg = &model->spring_groups[i];
            const char *first_name = (sg->joint_count > 0 && sg->joints[0].bone_index >= 0)
                ? model->bones[sg->joints[0].bone_index].name : "?";
            printf("[vrm_loader]   group[%u]: %u joints (first: \"%s\") stiff=%.2f drag=%.2f grav=%.2f\n",
                   i, sg->joint_count, first_name,
                   sg->joints ? sg->joints[0].stiffness : 0,
                   sg->joints ? sg->joints[0].drag_force : 0,
                   sg->joints ? sg->joints[0].gravity_power : 0);
        }
    }
}

void vrm_auto_blink(vrm_model_t *model, float time_sec, float interval)
{
    if (!model) return;

    /* Try common blink expression names */
    static const char *blink_names[] = { "blink", "Blink", "BLINK", "blinkLeft", NULL };
    int blink_idx = -1;
    for (int i = 0; blink_names[i]; i++) {
        blink_idx = vrm_find_expression(model, blink_names[i]);
        if (blink_idx >= 0) break;
    }
    if (blink_idx < 0) return;

    /* Blink pattern: quick close/open over ~0.15s */
    float phase = fmodf(time_sec, interval);
    float blink_dur = 0.15f;
    float w = 0.0f;
    if (phase < blink_dur) {
        /* Triangle wave: 0 -> 1 -> 0 over blink_dur */
        float t = phase / blink_dur;
        w = (t < 0.5f) ? (t * 2.0f) : (2.0f - t * 2.0f);
    }
    vrm_set_expression_weight(model, blink_idx, w);
}
