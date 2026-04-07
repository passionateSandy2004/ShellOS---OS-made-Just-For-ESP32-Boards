#include "shpkg.h"
#include "esp_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <errno.h>

static const char *TAG = "shpkg";

/* ── little-endian read helpers ── */
static uint16_t read_u16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t read_u32(const uint8_t *p) { return (uint32_t)(p[0] | (p[1]<<8) | (p[2]<<16) | (p[3]<<24)); }

esp_err_t shpkg_extract(const char *shpkg_abs_path, const char *dest_dir)
{
    if (!shpkg_abs_path || !dest_dir) return ESP_ERR_INVALID_ARG;

    FILE *f = fopen(shpkg_abs_path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "cannot open %s: %s", shpkg_abs_path, strerror(errno));
        return ESP_FAIL;
    }

    /* ── Read and validate header ── */
    uint8_t hdr[7];  /* magic(4) + version(1) + nfiles(2) */
    if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
        fclose(f); return ESP_FAIL;
    }
    if (memcmp(hdr, SHPKG_MAGIC, 4) != 0) {
        ESP_LOGE(TAG, "%s: bad magic", shpkg_abs_path);
        fclose(f); return ESP_FAIL;
    }
    if (hdr[4] != SHPKG_VERSION) {
        ESP_LOGE(TAG, "%s: unsupported version 0x%02x", shpkg_abs_path, hdr[4]);
        fclose(f); return ESP_FAIL;
    }
    uint16_t nfiles = read_u16(&hdr[5]);
    ESP_LOGI(TAG, "extracting %u file(s) to %s", nfiles, dest_dir);

    /* ── Extract each file ── */
    char name[256];
    char out_path[512];
    uint8_t *data = NULL;
    size_t   data_cap = 0;

    for (uint16_t i = 0; i < nfiles; i++) {
        /* name length */
        uint8_t name_len_byte;
        if (fread(&name_len_byte, 1, 1, f) != 1) goto err;
        uint8_t name_len = name_len_byte;

        /* name */
        if (name_len == 0 || name_len >= sizeof(name) - 1) goto err;
        if (fread(name, 1, name_len, f) != name_len) goto err;
        name[name_len] = '\0';

        /* Reject path traversal */
        if (strstr(name, "..") || name[0] == '/') {
            ESP_LOGE(TAG, "rejected unsafe path '%s'", name);
            goto err;
        }

        /* data length */
        uint8_t dlen_buf[4];
        if (fread(dlen_buf, 1, 4, f) != 4) goto err;
        uint32_t data_len = read_u32(dlen_buf);

        if (data_len > 256 * 1024) {  /* 256 KB per file max */
            ESP_LOGE(TAG, "file '%s' too large (%lu bytes)", name, (unsigned long)data_len);
            goto err;
        }

        /* Grow buffer if needed */
        if (data_len + 1 > data_cap) {
            free(data);
            data_cap = data_len + 1;
            data = malloc(data_cap);
            if (!data) goto err;
        }

        if (fread(data, 1, data_len, f) != data_len) goto err;
        data[data_len] = '\0';

        /* Build output path */
        snprintf(out_path, sizeof(out_path), "%s/%s", dest_dir, name);

        /* Create parent directories */
        char tmp[sizeof(out_path)];
        strncpy(tmp, out_path, sizeof(tmp) - 1);
        char *slash = strrchr(tmp, '/');
        if (slash && slash != tmp) {
            *slash = '\0';
            /* Recursive mkdir (single level here; deep nesting unlikely) */
            char *p = tmp + strlen(dest_dir) + 1;
            while ((p = strchr(p, '/'))) {
                *p = '\0';
                mkdir(tmp, 0777);
                *p = '/';
                p++;
            }
            mkdir(tmp, 0777);
        }

        /* Write file */
        FILE *out = fopen(out_path, "wb");
        if (!out) {
            ESP_LOGE(TAG, "cannot create %s: %s", out_path, strerror(errno));
            goto err;
        }
        if (fwrite(data, 1, data_len, out) != data_len) {
            fclose(out); goto err;
        }
        fclose(out);
        ESP_LOGI(TAG, "  extracted %s (%lu bytes)", name, (unsigned long)data_len);
    }

    free(data);
    fclose(f);
    return ESP_OK;

err:
    free(data);
    fclose(f);
    ESP_LOGE(TAG, "extraction failed");
    return ESP_FAIL;
}
