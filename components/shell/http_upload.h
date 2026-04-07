#ifndef HTTP_UPLOAD_H
#define HTTP_UPLOAD_H

/*
 * ShellOS HTTP Package Upload Server
 *
 * Starts an HTTP server on port 8080 with the following REST endpoints:
 *
 *   POST /pkg/upload          — body is raw .shpkg bytes → installs package
 *   GET  /pkg/list            — JSON array of installed packages
 *   POST /pkg/run/<name>      — start a package
 *   POST /pkg/stop/<name>     — stop a package
 *
 * Must be called after WiFi is connected.
 */

#include "esp_err.h"

#define HTTP_UPLOAD_PORT 8080

esp_err_t http_upload_server_start(void);
void      http_upload_server_stop(void);

#endif /* HTTP_UPLOAD_H */
