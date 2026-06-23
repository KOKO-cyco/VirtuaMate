/**
 * @file vrm_lip_sync.h
 * @brief Audio-driven lip sync: feeds decoded PCM samples and produces per-frame
 *        vowel weights (aa / ih / ou / ee / oh) for VRM expression blending.
 *
 * Thread model:
 *   - Producer (audio output thread): calls lip_sync_feed_pcm()
 *   - Consumer (render/main thread):  calls lip_sync_get_weights()
 *
 * Shared weights are stored as volatile float so the compiler never caches them
 * in registers across threads.  On ARM/x86 a naturally-aligned 32-bit store is
 * atomic at the hardware level, so no mutex is required for this use-case.
 *
 * @version 1.0
 * @date 2025-06-23
 * @copyright Copyright (c) Tuya Inc. All Rights Reserved.
 */

#ifndef VRM_LIP_SYNC_H
#define VRM_LIP_SYNC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float lp_state;
    float smooth_energy;
    float target_aa;
    float target_ih;
    float target_ou;
    float target_ee;
    float target_oh;
    volatile float w_aa;
    volatile float w_ih;
    volatile float w_ou;
    volatile float w_ee;
    volatile float w_oh;
    int sample_rate;
} lip_sync_ctx_t;

/**
 * @brief Initialise the context. Call once before feeding audio.
 * @param[in] ctx         Context to initialise.
 * @param[in] sample_rate PCM sample rate in Hz (typically 16000).
 * @return none
 */
void lip_sync_init(lip_sync_ctx_t *ctx, int sample_rate);

/**
 * @brief Feed a block of decoded PCM to update vowel weights.
 * @param[in] ctx      Lip sync context.
 * @param[in] buf      Pointer to raw PCM bytes.
 * @param[in] byte_len Number of bytes in buf.
 * @param[in] channels 1 = mono, 2 = stereo (stereo is downmixed by averaging).
 * @param[in] bits     Bits per sample (only 16 is supported; others are ignored).
 * @return none
 */
void lip_sync_feed_pcm(lip_sync_ctx_t *ctx,
                       const void *buf, uint32_t byte_len,
                       int channels, int bits);

/**
 * @brief Read current vowel weights. Call from the render thread.
 * @param[in]  ctx  Lip sync context.
 * @param[out] aa   Mouth open wide weight (optional).
 * @param[out] ih   Mouth narrow weight (optional).
 * @param[out] ou   Mouth round weight (optional).
 * @param[out] ee   Mouth half-open weight (optional).
 * @param[out] oh   Mouth round-open weight (optional).
 * @return none
 */
void lip_sync_get_weights(const lip_sync_ctx_t *ctx,
                          float *aa, float *ih, float *ou,
                          float *ee, float *oh);

/**
 * @brief Reset all weights to zero (e.g., when speaking stops).
 * @param[in] ctx  Lip sync context.
 * @return none
 */
void lip_sync_reset(lip_sync_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* VRM_LIP_SYNC_H */
