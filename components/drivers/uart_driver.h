#ifndef UART_DRIVER_H
#define UART_DRIVER_H

#include <stdint.h>
#include <stddef.h>
#include "sdkconfig.h"
#include "driver/uart.h"

/* ─────────────────────────────────────────
   Shell serial (hardware-dependent)
   - ESP32-CAM: UART0 GPIO1 / GPIO3
   - ESP32-C6 + sdkconfig.defaults.esp32c6: USB Serial/JTAG (XIAO USB-C COM)
   ───────────────────────────────────────── */
#define SHELL_UART_PORT     UART_NUM_0
#define SHELL_UART_BAUD     115200
#define SHELL_UART_BUF_SIZE 256

#if CONFIG_IDF_TARGET_ESP32C6
#define SHELL_UART_TX_PIN   (-1)
#define SHELL_UART_RX_PIN   (-1)
#else
#define SHELL_UART_TX_PIN   1
#define SHELL_UART_RX_PIN   3
#endif

/* ─────────────────────────────────────────
   API
   ───────────────────────────────────────── */
void uart_driver_init(void);
void shell_serial_flush_tx(void);
void uart_driver_send_bytes(const void *data, size_t len);

#if CONFIG_IDF_TARGET_ESP32C6
/** Route ESP_LOG through USJ without blocking — required for WiFi when no COM port is open */
void shellos_c6_install_nonblocking_log(void);
#endif

/* Output */
void uart_putchar(char c);
void uart_print(const char *str);
void uart_println(const char *str);
void uart_printf(const char *fmt, ...);

/* Input */
int  uart_getchar(void);              /* blocking, returns char */
int  uart_readline(char *buf, int max_len);  /* reads until Enter */

#endif // UART_DRIVER_H
