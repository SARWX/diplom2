#include "uart.h"
#include "stm32f10x.h"
#include "core_cm3.h"
#include "stm32f10x_usart.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_gpio.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/*==============================================================================
 * Internal Buffers
 *============================================================================*/

static volatile uint8_t rx_buffer[UART_RX_BUFFER_SIZE];
static volatile uint16_t rx_head = 0;
static volatile uint16_t rx_tail = 0;

static uint8_t tx_buffer[UART_TX_BUFFER_SIZE];
static volatile uint16_t tx_head = 0;
static volatile uint16_t tx_tail = 0;
static volatile uint8_t tx_busy = 0;

static char line_buffer[UART_RX_BUFFER_SIZE];
static volatile uint16_t line_pos = 0;

static uart_rx_callback_t rx_callback = NULL;
static void (*line_callback)(const char* line, uint16_t len) = NULL;

/*==============================================================================
 * Internal Functions
 *============================================================================*/

static int is_rx_buffer_full(void)
{
    uint16_t next = (rx_head + 1) % UART_RX_BUFFER_SIZE;
    return (next == rx_tail);
}

static int is_rx_buffer_empty(void)
{
    return (rx_head == rx_tail);
}

static int is_tx_buffer_empty(void)
{
    return (tx_head == tx_tail);
}

static void put_char_to_tx_buffer(uint8_t c)
{
    uint16_t next = (tx_head + 1) % UART_TX_BUFFER_SIZE;
    
    /* Wait for space in buffer */
    while (next == tx_tail) {
        /* Buffer full - wait */
    }
    
    tx_buffer[tx_head] = c;
    tx_head = next;
    
    /* Start transmission if not already busy */
    if (!tx_busy) {
        tx_busy = 1;
        USART_SendData(USART1, tx_buffer[tx_tail]);
    }
}

/*==============================================================================
 * Interrupt Handlers
 *============================================================================*/

void USART1_IRQHandler(void)
{
    /* Receive interrupt */
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET) {
        uint8_t data = USART_ReceiveData(USART1);
        
        /* Store in ring buffer */
        if (!is_rx_buffer_full()) {
            rx_buffer[rx_head] = data;
            rx_head = (rx_head + 1) % UART_RX_BUFFER_SIZE;
            
            /* Call character callback */
            if (rx_callback) {
                rx_callback(data);
            }
            
            /* Line callback handling */
            if (line_callback) {
                if (data == '\r' || data == '\n') {
                    if (line_pos > 0) {
                        line_buffer[line_pos] = '\0';
                        line_callback(line_buffer, line_pos);
                        line_pos = 0;
                    }
                } else if (data == '\b' || data == 0x7F) {
                    /* Backspace */
                    if (line_pos > 0) {
                        line_pos--;
                    }
                } else if (line_pos < UART_RX_BUFFER_SIZE - 1) {
                    line_buffer[line_pos++] = data;
                }
            }
        }
    }
    
    /* Transmit interrupt */
    if (USART_GetITStatus(USART1, USART_IT_TXE) != RESET) {
        if (!is_tx_buffer_empty()) {
            tx_tail = (tx_tail + 1) % UART_TX_BUFFER_SIZE;
            if (!is_tx_buffer_empty()) {
                USART_SendData(USART1, tx_buffer[tx_tail]);
            } else {
                tx_busy = 0;
                USART_ITConfig(USART1, USART_IT_TXE, DISABLE);
            }
        } else {
            tx_busy = 0;
            USART_ITConfig(USART1, USART_IT_TXE, DISABLE);
        }
    }
}

/*==============================================================================
 * Initialization
 *============================================================================*/

void uart_init(uint32_t baudrate)
{
    /* Enable clocks */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);
    
    /* Configure GPIO: PA9 = TX, PA10 = RX */
    GPIO_InitTypeDef gpio;
    
    /* TX pin - push-pull alternate function */
    gpio.GPIO_Pin = GPIO_Pin_9;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);
    
    /* RX pin - floating input */
    gpio.GPIO_Pin = GPIO_Pin_10;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &gpio);
    
    /* Configure USART */
    USART_InitTypeDef usart;
    usart.USART_BaudRate = baudrate;
    usart.USART_WordLength = USART_WordLength_8b;
    usart.USART_StopBits = USART_StopBits_1;
    usart.USART_Parity = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &usart);
    
    /* Enable USART */
    USART_Cmd(USART1, ENABLE);
    
    /* Clear buffers */
    rx_head = 0;
    rx_tail = 0;
    tx_head = 0;
    tx_tail = 0;
    tx_busy = 0;
    line_pos = 0;
}

void uart_init_pins(uint32_t baudrate, uint16_t tx_pin, uint16_t rx_pin, GPIO_TypeDef* gpio_port)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);
    
    GPIO_InitTypeDef gpio;
    
    /* TX pin */
    gpio.GPIO_Pin = tx_pin;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(gpio_port, &gpio);
    
    /* RX pin */
    gpio.GPIO_Pin = rx_pin;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(gpio_port, &gpio);
    
    USART_InitTypeDef usart;
    usart.USART_BaudRate = baudrate;
    usart.USART_WordLength = USART_WordLength_8b;
    usart.USART_StopBits = USART_StopBits_1;
    usart.USART_Parity = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &usart);
    
    USART_Cmd(USART1, ENABLE);
    
    rx_head = 0;
    rx_tail = 0;
    tx_head = 0;
    tx_tail = 0;
    tx_busy = 0;
    line_pos = 0;
}

/*==============================================================================
 * Transmit Functions
 *============================================================================*/

void uart_putchar(uint8_t c)
{
    put_char_to_tx_buffer(c);
}

void uart_puts(const char* str)
{
    while (*str) {
        uart_putchar(*str++);
    }
}

void uart_send(const uint8_t* data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        uart_putchar(data[i]);
    }
}

void uart_printf(const char* format, ...)
{
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    uart_puts(buffer);
}

/*==============================================================================
 * Receive Functions
 *============================================================================*/

uint8_t uart_getchar(void)
{
    while (is_rx_buffer_empty()) {
        ; /* Wait for data */
    }
    
    uint8_t c = rx_buffer[rx_tail];
    rx_tail = (rx_tail + 1) % UART_RX_BUFFER_SIZE;
    return c;
}

uint8_t uart_available(void)
{
    return !is_rx_buffer_empty();
}

uint8_t uart_read(uint8_t* c)
{
    if (is_rx_buffer_empty()) {
        return 0;
    }
    
    *c = rx_buffer[rx_tail];
    rx_tail = (rx_tail + 1) % UART_RX_BUFFER_SIZE;
    return 1;
}

uint16_t uart_readline(char* buffer, uint16_t max_len)
{
    uint16_t pos = 0;
    
    while (1) {
        uint8_t c = uart_getchar();
        
        if (c == '\r' || c == '\n') {
            if (pos > 0) {
                buffer[pos] = '\0';
                return pos;
            }
            /* Ignore empty lines */
            continue;
        } else if (c == '\b' || c == 0x7F) {
            if (pos > 0) {
                pos--;
                uart_putchar('\b');
                uart_putchar(' ');
                uart_putchar('\b');
            }
        } else if (pos < max_len - 1) {
            buffer[pos++] = c;
            uart_putchar(c);
        }
    }
}

char* uart_get_rx_buffer(void)
{
    return (char*)rx_buffer;
}

void uart_clear_rx_buffer(void)
{
    rx_head = 0;
    rx_tail = 0;
    line_pos = 0;
}

/*==============================================================================
 * Callback Support
 *============================================================================*/

void uart_set_rx_callback(uart_rx_callback_t callback)
{
    rx_callback = callback;
}

void uart_set_line_callback(void (*callback)(const char* line, uint16_t len))
{
    line_callback = callback;
}

/*==============================================================================
 * Interrupt Control
 *============================================================================*/

void uart_enable_rx_interrupt(void)
{
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
    NVIC_EnableIRQ(USART1_IRQn);
}

void uart_disable_rx_interrupt(void)
{
    USART_ITConfig(USART1, USART_IT_RXNE, DISABLE);
}
