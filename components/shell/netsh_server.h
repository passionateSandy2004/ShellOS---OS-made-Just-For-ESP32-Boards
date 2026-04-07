#ifndef NETSH_SERVER_H
#define NETSH_SERVER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define NETSH_DEFAULT_PORT 2323

esp_err_t netsh_server_start(uint16_t port);
void      netsh_server_stop(void);
bool      netsh_server_is_running(void);

#endif
