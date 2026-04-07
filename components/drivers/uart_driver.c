#include "uart_driver.h"
#include "sdkconfig.h"
#include "driver/uart.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#if CONFIG_IDF_TARGET_ESP32C6
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <fcntl.h>
#include <unistd.h>
#endif

#if CONFIG_IDF_TARGET_ESP32C6

/*
 * USB-Serial-JTAG TX must never block on a full ring buffer when no host has opened
 * the CDC port; otherwise esp_wifi / event loop tasks stall and WiFi never comes up.
 * Always use tick=0 and chunk; drop overflow. When a terminal is open, the host
 * drains the buffer and logs look normal.
 */
static void usj_write_best_effort(const void *data, size_t len)
{
    if (!data || len == 0) {
        return;
    }
    const uint8_t *p = (const uint8_t *)data;
    const size_t chunk_max = 64;

    while (len > 0) {
        size_t chunk = len > chunk_max ? chunk_max : len;
        int n = usb_serial_jtag_write_bytes(p, chunk, 0);
        if (n == 0) {
            return;
        }
        p += chunk;
        len -= chunk;
    }
}

static int shellos_usj_vprintf(const char *fmt, va_list ap)
{
    char buf[512];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (n <= 0) {
        return n;
    }
    size_t len = (size_t)n;
    if (len >= sizeof(buf)) {
        len = sizeof(buf) - 1;
    }
    usj_write_best_effort(buf, len);
    return n;
}

void shellos_c6_install_nonblocking_log(void)
{
    /*
     * Laptop USB: device enumerates and usb_serial_jtag_is_connected()==true even when
     * no terminal has opened the COM port. Default VFS / driver TX then blocks on the
     * ringbuffer (wpa_supplicant printf, etc.) and Wi-Fi never finishes.
     * Non-blocking VFS writes to the HW FIFO with a short busy-wait then drops bytes.
     */
    usb_serial_jtag_vfs_use_nonblocking();

    int fl = fcntl(STDOUT_FILENO, F_GETFL, 0);
    if (fl >= 0) {
        fcntl(STDOUT_FILENO, F_SETFL, fl | O_NONBLOCK);
    }
    fl = fcntl(STDERR_FILENO, F_GETFL, 0);
    if (fl >= 0) {
        fcntl(STDERR_FILENO, F_SETFL, fl | O_NONBLOCK);
    }

    esp_log_set_vprintf(shellos_usj_vprintf);
}

void uart_driver_init(void)
{
    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
        cfg.tx_buffer_size = SHELL_UART_BUF_SIZE * 8;
        cfg.rx_buffer_size = SHELL_UART_BUF_SIZE * 2;
        ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&cfg));
    }
}

void shell_serial_flush_tx(void)
{
    (void)usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(50));
}

void uart_driver_send_bytes(const void *data, size_t len)
{
    usj_write_best_effort(data, len);
}

void uart_putchar(char c)
{
    uart_driver_send_bytes(&c, 1);
}

void uart_print(const char *str)
{
    if (!str) {
        return;
    }
    uart_driver_send_bytes(str, strlen(str));
}

void uart_println(const char *str)
{
    uart_print(str);
    uart_print("\r\n");
}

void uart_printf(const char *fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    uart_print(buf);
}

int uart_getchar(void)
{
    uint8_t c;
    while (usb_serial_jtag_read_bytes(&c, 1, portMAX_DELAY) != 1) {
    }
    return (int)c;
}

#else

void uart_driver_init(void)
{
    uart_config_t cfg = {
        .baud_rate  = SHELL_UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };

    uart_driver_install(SHELL_UART_PORT,
                        SHELL_UART_BUF_SIZE * 2,
                        SHELL_UART_BUF_SIZE * 2,
                        0, NULL, 0);

    uart_param_config(SHELL_UART_PORT, &cfg);

    uart_set_pin(SHELL_UART_PORT,
                 SHELL_UART_TX_PIN,
                 SHELL_UART_RX_PIN,
                 UART_PIN_NO_CHANGE,
                 UART_PIN_NO_CHANGE);
}

void shell_serial_flush_tx(void)
{
    (void)uart_wait_tx_done(SHELL_UART_PORT, pdMS_TO_TICKS(500));
}

void uart_driver_send_bytes(const void *data, size_t len)
{
    if (!data || len == 0) {
        return;
    }
    uart_write_bytes(SHELL_UART_PORT, data, len);
}

void uart_putchar(char c)
{
    uart_write_bytes(SHELL_UART_PORT, &c, 1);
}

void uart_print(const char *str)
{
    if (!str) {
        return;
    }
    uart_write_bytes(SHELL_UART_PORT, str, strlen(str));
}

void uart_println(const char *str)
{
    uart_print(str);
    uart_print("\r\n");
}

void uart_printf(const char *fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    uart_print(buf);
}

int uart_getchar(void)
{
    uint8_t c;
    while (uart_read_bytes(SHELL_UART_PORT, &c, 1, portMAX_DELAY) != 1) {
    }
    return (int)c;
}

#endif

int uart_readline(char *buf, int max_len)
{
    int pos = 0;
    while (1) {
        int c = uart_getchar();

        if (c == '\r' || c == '\n') {
            uart_print("\r\n");
            break;
        } else if (c == 0x7F || c == 0x08) {
            if (pos > 0) {
                pos--;
                uart_print("\b \b");
            }
        } else if (c >= 0x20 && c < 0x7F) {
            if (pos < max_len - 1) {
                buf[pos++] = (char)c;
                uart_putchar((char)c);
            }
        }
    }
    buf[pos] = '\0';
    return pos;
}
