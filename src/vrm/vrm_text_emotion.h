/**
 * @file vrm_text_emotion.h
 * @brief Real-time text emotion analyzer — scans streaming text for sentiment
 *        keywords and drives the avatar's facial expression automatically.
 * @version 1.0
 * @date 2025-06-23
 * @copyright Copyright (c) Tuya Inc. All Rights Reserved.
 */
#ifndef VRM_TEXT_EMOTION_H
#define VRM_TEXT_EMOTION_H

#ifdef __cplusplus
extern "C" {
#endif

void text_emotion_reset(void);
void text_emotion_set_base(const char *emotion);
void text_emotion_feed(const char *text, int len);
void text_emotion_flush(void);

#ifdef __cplusplus
}
#endif

#endif /* VRM_TEXT_EMOTION_H */
