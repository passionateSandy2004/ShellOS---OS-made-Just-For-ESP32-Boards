#ifndef SHELL_IO_H
#define SHELL_IO_H

#include <stdbool.h>

void shell_io_init(void);

/* -1 = UART only; >=0 = TCP session sends here (UART silent for shell output) */
void shell_io_bind_socket(int sock_fd);
void shell_io_bind_uart(void);
bool shell_io_is_remote(void);

void shell_io_print(const char *str);
void shell_io_println(const char *str);
void shell_io_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void shell_io_putchar(char c);

#endif
