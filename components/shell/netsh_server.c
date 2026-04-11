#include "netsh_server.h"
#include "shell.h"
#include "shell_io.h"
#include "wifi_driver.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>

static const char *TAG = "netsh";

static TaskHandle_t s_task_handle = NULL;
static int          s_listen_fd   = -1;
static volatile bool s_stop       = false;
static uint16_t     s_bind_port   = NETSH_DEFAULT_PORT;

static bool read_line_socket(int fd, char *buf, size_t cap)
{
    size_t pos = 0;
    while (pos < cap - 1) {
        char c;
        int  n = recv(fd, &c, 1, 0);
        if (n <= 0) {
            return false;
        }
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            buf[pos] = '\0';
            return true;
        }
        if (c >= 0x20 && c < 0x7f) {
            buf[pos++] = (char)c;
        }
    }
    buf[cap - 1] = '\0';
    return true;
}

static void netsh_server_task(void *arg)
{
    (void)arg;

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port        = htons(s_bind_port),
    };

    int ls = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (ls < 0) {
        ESP_LOGE(TAG, "socket: %s", strerror(errno));
        s_listen_fd = -1;
        s_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    int yes = 1;
    (void)setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    if (bind(ls, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "bind: %s", strerror(errno));
        close(ls);
        s_listen_fd = -1;
        s_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    if (listen(ls, 1) != 0) {
        ESP_LOGE(TAG, "listen: %s", strerror(errno));
        close(ls);
        s_listen_fd = -1;
        s_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    s_listen_fd = ls;
    ESP_LOGI(TAG, "listening on :%u", (unsigned)s_bind_port);

    while (!s_stop) {
        struct sockaddr_in cli;
        socklen_t          clen = sizeof(cli);
        int                c    = accept(ls, (struct sockaddr *)&cli, &clen);
        if (c < 0) {
            if (s_stop) {
                break;
            }
            continue;
        }

        int one = 1;
        (void)setsockopt(c, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        (void)setsockopt(c, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));

        for (;;) {
            /* Own this socket: UART shell may have run while we blocked in recv */
            shell_io_bind_socket(c);
            shell_print_prompt_now();
            char line[SHELL_MAX_CMD_LEN];
            if (!read_line_socket(c, line, sizeof(line))) {
                break;
            }
            shell_io_bind_socket(c);
            shell_execute(line);
        }

        shell_io_bind_uart();
        close(c);
    }

    close(ls);
    s_listen_fd   = -1;
    s_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t netsh_server_start(uint16_t port)
{
    if (s_task_handle != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!wifi_driver_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }

    s_bind_port = (port == 0) ? NETSH_DEFAULT_PORT : port;
    s_stop      = false;

    /* 16 KB stack: task runs full command dispatch (same as shell_task).
     * Pin to Core 0 so it shares the core with the WiFi driver — socket
     * callbacks need no cross-core sync and interrupt latency is minimal. */
#if CONFIG_FREERTOS_UNICORE
    BaseType_t ok = xTaskCreate(netsh_server_task, "netsh_srv", 16384, NULL, 5, &s_task_handle);
#else
    BaseType_t ok = xTaskCreatePinnedToCore(netsh_server_task, "netsh_srv", 16384, NULL, 5, &s_task_handle, 0);
#endif
    if (ok != pdPASS) {
        s_task_handle = NULL;
        return ESP_FAIL;
    }
    return ESP_OK;
}

void netsh_server_stop(void)
{
    if (s_task_handle == NULL) {
        return;
    }
    s_stop = true;
    if (s_listen_fd >= 0) {
        (void)shutdown(s_listen_fd, SHUT_RDWR);
        close(s_listen_fd);
        s_listen_fd = -1;
    }
    for (int i = 0; i < 100 && s_task_handle != NULL; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

bool netsh_server_is_running(void)
{
    return s_task_handle != NULL;
}
