#include "shell_io.h"
#include "uart_driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <unistd.h>

static SemaphoreHandle_t s_io_mutex;
static int               s_remote_sock = -1;

void shell_io_init(void)
{
    if (!s_io_mutex) {
        s_io_mutex = xSemaphoreCreateMutex();
    }
}

void shell_io_bind_socket(int sock_fd)
{
    if (s_io_mutex) {
        xSemaphoreTake(s_io_mutex, portMAX_DELAY);
    }
    s_remote_sock = sock_fd;
    if (s_io_mutex) {
        xSemaphoreGive(s_io_mutex);
    }
}

void shell_io_bind_uart(void)
{
    shell_io_bind_socket(-1);
}

bool shell_io_is_remote(void)
{
    return s_remote_sock >= 0;
}

static void write_sock(const char *buf, size_t len)
{
    int fd = s_remote_sock;
    if (fd < 0 || len == 0) {
        return;
    }
    size_t off = 0;
    while (off < len) {
        int n = send(fd, buf + off, len - off, 0);
        if (n <= 0) {
            break;
        }
        off += (size_t)n;
    }
}

static void write_out(const char *s)
{
    if (!s) {
        return;
    }
    if (s_remote_sock >= 0) {
        write_sock(s, strlen(s));
    } else {
        uart_print(s);
    }
}

void shell_io_print(const char *str)
{
    if (!s_io_mutex) {
        shell_io_init();
    }
    xSemaphoreTake(s_io_mutex, portMAX_DELAY);
    write_out(str);
    xSemaphoreGive(s_io_mutex);
}

void shell_io_putchar(char c)
{
    if (!s_io_mutex) {
        shell_io_init();
    }
    xSemaphoreTake(s_io_mutex, portMAX_DELAY);
    if (s_remote_sock >= 0) {
        (void)send(s_remote_sock, &c, 1, 0);
    } else {
        uart_putchar(c);
    }
    xSemaphoreGive(s_io_mutex);
}

void shell_io_printf(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (!s_io_mutex) {
        shell_io_init();
    }
    xSemaphoreTake(s_io_mutex, portMAX_DELAY);
    write_out(buf);
    xSemaphoreGive(s_io_mutex);
}

void shell_io_println(const char *str)
{
    shell_io_print(str);
    shell_io_print("\r\n");
}
