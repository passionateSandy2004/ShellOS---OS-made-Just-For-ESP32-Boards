#ifndef SHPKG_H
#define SHPKG_H

/*
 * ShellOS .shpkg package archive format (custom binary, no zip dependency)
 *
 * Format:
 *   [4]  magic   "SHPK"
 *   [1]  version  0x01
 *   [2]  nfiles   (uint16_t, little-endian)
 *   For each file:
 *     [1]  name_len (uint8_t)
 *     [name_len]  name (UTF-8, no null terminator)
 *     [4]  data_len (uint32_t, little-endian)
 *     [data_len]  file bytes
 */

#include "esp_err.h"
#include <stddef.h>

#define SHPKG_MAGIC    "SHPK"
#define SHPKG_VERSION   0x01

/*
 * Extract all files from a .shpkg archive at shpkg_abs_path
 * into dest_dir (absolute path, must already exist).
 * Returns ESP_OK on success.
 */
esp_err_t shpkg_extract(const char *shpkg_abs_path, const char *dest_dir);

#endif /* SHPKG_H */
