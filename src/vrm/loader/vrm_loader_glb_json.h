/**
 * @file vrm_loader_glb_json.h
 * @brief Load GLB JSON (+ binary) chunk from a .vrm / .glb file
 * @version 1.0
 * @date 2025-06-23
 * @copyright Copyright (c) Tuya Inc. All Rights Reserved.
 */
#ifndef VRM_LOADER_GLB_JSON_H
#define VRM_LOADER_GLB_JSON_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char           *json;
    size_t          json_len;
    unsigned char  *bin;
    size_t          bin_len;
} vrm_glb_json_t;

/**
 * @brief Read the JSON chunk (and BIN chunk if present) from a GLB file.
 * @param[out] out   Output structure (zeroed on failure).
 * @param[in]  path  Path to .vrm / .glb file.
 * @return 0 on success, -1 on error.
 */
int vrm_glb_json_load(vrm_glb_json_t *out, const char *path);

/**
 * @brief Free buffers allocated by vrm_glb_json_load().
 * @param[in] j  GLB JSON context.
 * @return none
 */
void vrm_glb_json_free(vrm_glb_json_t *j);

#ifdef __cplusplus
}
#endif

#endif /* VRM_LOADER_GLB_JSON_H */
