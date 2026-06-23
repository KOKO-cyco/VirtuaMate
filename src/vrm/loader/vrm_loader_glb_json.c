/**
 * @file vrm_loader_glb_json.c
 * @brief GLB JSON / BIN chunk loader
 * @version 1.0
 * @date 2025-06-23
 * @copyright Copyright (c) Tuya Inc. All Rights Reserved.
 */
#include "vrm_loader_glb_json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int vrm_glb_json_load(vrm_glb_json_t *out, const char *path)
{
    FILE *fp;
    unsigned char hdr[12];
    uint32_t      chunk_len;
    uint32_t      chunk_type;

    if (out == NULL || path == NULL) {
        return -1;
    }
    memset(out, 0, sizeof(*out));

    fp = fopen(path, "rb");
    if (fp == NULL) {
        return -1;
    }

    if (fread(hdr, 1, 12, fp) != 12) {
        fclose(fp);
        return -1;
    }
    if (hdr[0] != 'g' || hdr[1] != 'l' || hdr[2] != 'T' || hdr[3] != 'F') {
        fclose(fp);
        return -1;
    }

    if (fread(&chunk_len, 4, 1, fp) != 1) {
        fclose(fp);
        return -1;
    }
    if (fread(&chunk_type, 4, 1, fp) != 1) {
        fclose(fp);
        return -1;
    }
    if (chunk_type != 0x4E4F534A) {
        fclose(fp);
        return -1;
    }

    out->json = (char *)malloc((size_t)chunk_len + 1);
    if (out->json == NULL) {
        fclose(fp);
        return -1;
    }
    if (fread(out->json, 1, chunk_len, fp) != chunk_len) {
        vrm_glb_json_free(out);
        fclose(fp);
        return -1;
    }
    out->json[chunk_len] = '\0';
    out->json_len = chunk_len;

    if (fread(&chunk_len, 4, 1, fp) == 1) {
        if (fread(&chunk_type, 4, 1, fp) == 1 && chunk_type == 0x004E4942) {
            out->bin = (unsigned char *)malloc(chunk_len);
            if (out->bin != NULL) {
                if (fread(out->bin, 1, chunk_len, fp) == chunk_len) {
                    out->bin_len = chunk_len;
                } else {
                    free(out->bin);
                    out->bin = NULL;
                    out->bin_len = 0;
                }
            }
        }
    }

    fclose(fp);
    return 0;
}

void vrm_glb_json_free(vrm_glb_json_t *j)
{
    if (j == NULL) {
        return;
    }
    free(j->json);
    free(j->bin);
    memset(j, 0, sizeof(*j));
}
