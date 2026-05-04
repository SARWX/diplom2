#ifndef UART_H
#define UART_H

#include <stdint.h>

void uart_init(uint32_t baudrate);
void uart_putchar(uint8_t c);
void uart_puts(const char* str);
void uart_printf(const char* format, ...);
void uart_readline(char* buffer, uint16_t max_len);

/** @brief Enable or disable debug output globally (1 = on, 0 = off). Default: 1. */
void uart_dbg_set(uint8_t enable);
/** @brief Print a debug message — silently dropped if debug is disabled. */
void uart_dbg(const char* fmt, ...);

#endif
