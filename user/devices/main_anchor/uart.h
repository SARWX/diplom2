#ifndef UART_H
#define UART_H

#include <stdint.h>
#include "stm32f10x.h"

/*==============================================================================
 * Configuration
 *============================================================================*/

#define UART_RX_BUFFER_SIZE  256
#define UART_TX_BUFFER_SIZE  256

/*==============================================================================
 * Initialization
 *============================================================================*/

/**
 * Initialize UART1 (PA9 - TX, PA10 - RX) for console communication
 * @param baudrate - baud rate (e.g., 115200, 9600)
 */
void uart_init(uint32_t baudrate);

/**
 * Initialize UART with custom GPIO pins (optional)
 * @param baudrate - baud rate
 * @param tx_pin - TX GPIO pin (e.g., GPIO_Pin_9)
 * @param rx_pin - RX GPIO pin
 * @param gpio_port - GPIO port (e.g., GPIOA)
 */
void uart_init_pins(uint32_t baudrate, uint16_t tx_pin, uint16_t rx_pin, GPIO_TypeDef* gpio_port);

/*==============================================================================
 * Transmit Functions
 *============================================================================*/

/**
 * Send single character
 * @param c - character to send
 */
void uart_putchar(uint8_t c);

/**
 * Send string
 * @param str - null-terminated string
 */
void uart_puts(const char* str);

/**
 * Send buffer
 * @param data - data buffer
 * @param len - length of data
 */
void uart_send(const uint8_t* data, uint16_t len);

/**
 * Send formatted string (printf-like)
 * @param format - format string
 */
void uart_printf(const char* format, ...);

/*==============================================================================
 * Receive Functions
 *============================================================================*/

/**
 * Get received character (blocking)
 * @return received character
 */
uint8_t uart_getchar(void);

/**
 * Check if character is available
 * @return 1 if character available, 0 otherwise
 */
uint8_t uart_available(void);

/**
 * Read character from buffer (non-blocking)
 * @param c - pointer to store character
 * @return 1 if character read, 0 if no data
 */
uint8_t uart_read(uint8_t* c);

/**
 * Read line (blocking until newline)
 * @param buffer - output buffer
 * @param max_len - maximum buffer size
 * @return number of bytes read
 */
uint16_t uart_readline(char* buffer, uint16_t max_len);

/**
 * Get pointer to receive buffer (for command parsing)
 * @return pointer to internal receive buffer
 */
char* uart_get_rx_buffer(void);

/**
 * Clear receive buffer
 */
void uart_clear_rx_buffer(void);

/*==============================================================================
 * Callback Support
 *============================================================================*/

/**
 * Callback type for received data
 */
typedef void (*uart_rx_callback_t)(uint8_t data);

/**
 * Set callback for each received character
 * @param callback - function to call on each received byte
 */
void uart_set_rx_callback(uart_rx_callback_t callback);

/**
 * Set callback for complete line (when newline received)
 * @param callback - function to call when line is received
 */
void uart_set_line_callback(void (*callback)(const char* line, uint16_t len));

/*==============================================================================
 * Interrupt Control
 *============================================================================*/

/**
 * Enable UART receive interrupt
 */
void uart_enable_rx_interrupt(void);

/**
 * Disable UART receive interrupt
 */
void uart_disable_rx_interrupt(void);

#endif /* UART_H */
