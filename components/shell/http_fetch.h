#ifndef HTTP_FETCH_H
#define HTTP_FETCH_H

#include "esp_err.h"

/* Stream HTTP(S) GET to a LittleFS file (absolute path under /root) */
esp_err_t shell_http_fetch_to_file(const char *url, const char *abs_path);

/*
 * Fetch a manifest from manifest_url, then each listed file.
 * Manifest lines (text):
 *   # comment
 *   <relative-path> <http-url>
 * Example:
 *   apps/hello.txt https://example.com/hello.txt
 */
esp_err_t shell_pkg_install_manifest(const char *manifest_url);

#endif
