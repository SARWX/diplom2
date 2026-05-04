#ifndef UART_H
#define UART_H

#include <stdint.h>

void uart_init(uint32_t baudrate);
void uart_putchar(uint8_t c);
void uart_puts(const char* str);
void uart_printf(const char* format, ...);
/**
 * @brief Read a line from UART, calling @p idle_fn on every loop iteration.
 *
 * @p idle_fn may be NULL. Useful for processing network frames while waiting
 * for user input.
 */
void uart_readline(char* buffer, uint16_t max_len);
void uart_readline_idle(char* buffer, uint16_t max_len, void (*idle_fn)(void));

/** @brief Enable or disable debug output globally (1 = on, 0 = off). Default: 1. */
void uart_dbg_set(uint8_t enable);
/** @brief Print a debug message — silently dropped if debug is disabled. */
void uart_dbg(const char* fmt, ...);

#endif
