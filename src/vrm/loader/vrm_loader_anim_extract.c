/**
 * @file vrm_loader_anim_extract.c
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

void vrm_loader_extract_animations(const struct aiScene *scene, vrm_model_t *model)
{
    if (scene->mNumAnimations == 0) return;

    model->animations = (vrm_animation_t *)calloc(scene->mNumAnimations, sizeof(vrm_animation_t));
    model->animation_count = scene->mNumAnimations;

    for (unsigned ai = 0; ai < scene->mNumAnimations; ai++) {
        const struct aiAnimation *anim = scene->mAnimations[ai];
        vrm_animation_t *va = &model->animations[ai];

        snprintf(va->name, sizeof(va->name), "%s",
                 anim->mName.length > 0 ? anim->mName.data : "Animation");

        double tps = anim->mTicksPerSecond > 0 ? anim->mTicksPerSecond : 30.0;
        va->duration = (float)(anim->mDuration / tps);

        va->bone_anims = (vrm_bone_anim_t *)calloc(anim->mNumChannels, sizeof(vrm_bone_anim_t));
        va->bone_anim_count = 0;

        for (unsigned ci = 0; ci < anim->mNumChannels; ci++) {
            const struct aiNodeAnim *ch = anim->mChannels[ci];
            int bone_idx = vrm_loader_find_bone_by_name(ch->mNodeName.data);
            if (bone_idx < 0) continue;

            vrm_bone_anim_t *ba = &va->bone_anims[va->bone_anim_count];
            ba->bone_index = bone_idx;

            /* Count channels: up to 3 (T, R, S) */
            int nch = 0;
            if (ch->mNumPositionKeys > 0) nch++;
            if (ch->mNumRotationKeys > 0) nch++;
            if (ch->mNumScalingKeys > 0) nch++;

            ba->channels = (vrm_anim_channel_t *)calloc(nch, sizeof(vrm_anim_channel_t));
            ba->channel_count = 0;

            /* Translation */
            if (ch->mNumPositionKeys > 0) {
                vrm_anim_channel_t *ac = &ba->channels[ba->channel_count++];
                ac->path = 0;
                ac->count = ch->mNumPositionKeys;
                ac->times = (float *)malloc(ac->count * sizeof(float));
                ac->values = (float *)malloc(ac->count * 3 * sizeof(float));
                for (unsigned k = 0; k < ac->count; k++) {
                    ac->times[k] = (float)(ch->mPositionKeys[k].mTime / tps);
                    ac->values[k*3+0] = ch->mPositionKeys[k].mValue.x;
                    ac->values[k*3+1] = ch->mPositionKeys[k].mValue.y;
                    ac->values[k*3+2] = ch->mPositionKeys[k].mValue.z;
                }
            }

            /* Rotation (quaternion) */
            if (ch->mNumRotationKeys > 0) {
                vrm_anim_channel_t *ac = &ba->channels[ba->channel_count++];
                ac->path = 1;
                ac->count = ch->mNumRotationKeys;
                ac->times = (float *)malloc(ac->count * sizeof(float));
                ac->values = (float *)malloc(ac->count * 4 * sizeof(float));
                for (unsigned k = 0; k < ac->count; k++) {
                    ac->times[k] = (float)(ch->mRotationKeys[k].mTime / tps);
                    /* Assimp: w,x,y,z → our storage: x,y,z,w */
                    ac->values[k*4+0] = ch->mRotationKeys[k].mValue.x;
                    ac->values[k*4+1] = ch->mRotationKeys[k].mValue.y;
                    ac->values[k*4+2] = ch->mRotationKeys[k].mValue.z;
                    ac->values[k*4+3] = ch->mRotationKeys[k].mValue.w;
                }
            }

            /* Scale */
            if (ch->mNumScalingKeys > 0) {
                vrm_anim_channel_t *ac = &ba->channels[ba->channel_count++];
                ac->path = 2;
                ac->count = ch->mNumScalingKeys;
                ac->times = (float *)malloc(ac->count * sizeof(float));
                ac->values = (float *)malloc(ac->count * 3 * sizeof(float));
                for (unsigned k = 0; k < ac->count; k++) {
                    ac->times[k] = (float)(ch->mScalingKeys[k].mTime / tps);
                    ac->values[k*3+0] = ch->mScalingKeys[k].mValue.x;
                    ac->values[k*3+1] = ch->mScalingKeys[k].mValue.y;
                    ac->values[k*3+2] = ch->mScalingKeys[k].mValue.z;
                }
            }

            va->bone_anim_count++;
        }
    }

    printf("[vrm_loader] extracted %u animation(s)\n", model->animation_count);
}
