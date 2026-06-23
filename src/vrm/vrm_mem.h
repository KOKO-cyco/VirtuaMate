/**
 * @file vrm_mem.h
 * @brief VRM module memory allocation wrappers (TuyaOpen heap / PSRAM).
 * @version 1.0
 * @date 2025-06-23
 * @copyright Copyright (c) Tuya Inc. All Rights Reserved.
 */
#ifndef VRM_MEM_H
#define VRM_MEM_H

#include "app_base_config.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define vrm_malloc(s)     claw_malloc(s)
#define vrm_free(p)       claw_free(p)
#define vrm_realloc(p, s) realloc((p), (s))

static inline void *vrm_calloc(size_t nmemb, size_t size)
{
    size_t nbytes = nmemb * size;
    void  *p = claw_malloc(nbytes);

    if (p != NULL) {
        memset(p, 0, nbytes);
    }
    return p;
}

static inline char *vrm_strdup(const char *s)
{
    size_t len;
    char  *dup;

    if (s == NULL) {
        return NULL;
    }
    len = strlen(s) + 1;
    dup = (char *)vrm_malloc(len);
    if (dup != NULL) {
        memcpy(dup, s, len);
    }
    return dup;
}

#endif /* VRM_MEM_H */
