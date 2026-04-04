#ifndef UART_H
#define UART_H

#include <stdint.h>

void uart_init(uint32_t baudrate);
void uart_putchar(uint8_t c);
void uart_puts(const char* str);
void uart_printf(const char* format, ...);
void uart_readline(char* buffer, uint16_t max_len);

#endif
