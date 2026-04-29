#include "uart.h"
#include "stm32f10x.h"
// #include <stdarg.h>
// #include <stdio.h>

void uart_init(uint32_t baudrate)
{
	/* Enable clocks */
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);
	
	/* Configure GPIO */
	GPIO_InitTypeDef gpio;
	
	/* TX (PA9) - Alternate Function Push-Pull */
	gpio.GPIO_Pin = GPIO_Pin_9;
	gpio.GPIO_Mode = GPIO_Mode_AF_PP;
	gpio.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &gpio);
	
	/* RX (PA10) - Input Floating */
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
}

void uart_putchar(uint8_t c)
{
	while ((USART1->SR & USART_SR_TXE) == 0);
	USART1->DR = c;
}

void uart_puts(const char* str)
{
	while (*str) {
		uart_putchar(*str++);
	}
}

void uart_printf(const char* format, ...)
{
	(void)format;
	/* Do nothing - disabled for size optimization */
}

// void uart_printf(const char* format, ...)
// {
// 	char buffer[256];
// 	va_list args;
// 	va_start(args, format);
// 	vsnprintf(buffer, sizeof(buffer), format, args);
// 	va_end(args);
// 	uart_puts(buffer);
// }

void uart_readline(char* buffer, uint16_t max_len)
{
	uint16_t pos = 0;
	
	while (1) {
		/* Wait for character */
		while ((USART1->SR & USART_SR_RXNE) == 0);
		
		uint8_t c = USART1->DR;
		
		if (c == '\r' || c == '\n') {
			if (pos > 0) {
				buffer[pos] = '\0';
				uart_puts("\r\n");
				return;
			}
		} else if (c == '\b' || c == 0x7F) {
			if (pos > 0) {
				pos--;
				uart_puts("\b \b");
			}
		} else if (pos < max_len - 1) {
			buffer[pos++] = c;
			uart_putchar(c);
		}
	}
}
