#include "uart.h"
#include "stm32f10x.h"
#include <stdarg.h>

static void uart_vprintf(const char *fmt, va_list ap);

static uint8_t uart_ready = 0;
static uint8_t debug_enabled = 1;

void uart_dbg_set(uint8_t enable)
{
	debug_enabled = enable;
}

void uart_dbg(const char* fmt, ...)
{
	if (!uart_ready || !debug_enabled)
		return;
	va_list ap;
	va_start(ap, fmt);
	uart_vprintf(fmt, ap);
	va_end(ap);
}

void uart_init(uint32_t baudrate)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);

	GPIO_InitTypeDef gpio;

	gpio.GPIO_Pin   = GPIO_Pin_9;
	gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
	gpio.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &gpio);

	gpio.GPIO_Pin  = GPIO_Pin_10;
	gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
	GPIO_Init(GPIOA, &gpio);

	USART_InitTypeDef usart;
	usart.USART_BaudRate            = baudrate;
	usart.USART_WordLength          = USART_WordLength_8b;
	usart.USART_StopBits            = USART_StopBits_1;
	usart.USART_Parity              = USART_Parity_No;
	usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	usart.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
	USART_Init(USART1, &usart);
	USART_Cmd(USART1, ENABLE);
	uart_ready = 1;
}

void uart_putchar(uint8_t c)
{
	if (!uart_ready)
		return;
	while ((USART1->SR & USART_SR_TXE) == 0)
		;
	USART1->DR = c;
}

void uart_puts(const char *str)
{
	while (*str)
		uart_putchar(*str++);
}

static void put_uint(uint32_t n, uint8_t base, uint8_t upper,
		     uint8_t width, uint8_t zpad)
{
	char buf[11];
	uint8_t i = 0;

	if (n == 0) {
		buf[i++] = '0';
	} else {
		while (n) {
			uint8_t d = n % base;
			buf[i++] = d < 10 ? '0' + d
					  : (upper ? 'A' : 'a') + d - 10;
			n /= base;
		}
	}
	while (i < width)
		buf[i++] = zpad ? '0' : ' ';
	while (i--)
		uart_putchar(buf[i]);
}

static void uart_vprintf(const char *fmt, va_list ap)
{

	for (; *fmt; fmt++) {
		if (*fmt != '%') {
			uart_putchar(*fmt);
			continue;
		}
		fmt++;

		uint8_t zpad = (*fmt == '0');
		if (zpad)
			fmt++;

		uint8_t width = 0;
		while (*fmt >= '0' && *fmt <= '9')
			width = width * 10 + (*fmt++ - '0');

		uint8_t precision = 6;
		if (*fmt == '.') {
			fmt++;
			precision = 0;
			while (*fmt >= '0' && *fmt <= '9')
				precision = precision * 10 + (*fmt++ - '0');
		}

		switch (*fmt) {
		case 'd': {
			int v = va_arg(ap, int);
			if (v < 0) {
				uart_putchar('-');
				v = -v;
			}
			put_uint((uint32_t)v, 10, 0, width, zpad);
			break;
		}
		case 'u':
			put_uint(va_arg(ap, uint32_t), 10, 0, width, zpad);
			break;
		case 'x':
			put_uint(va_arg(ap, uint32_t), 16, 0, width, zpad);
			break;
		case 'X':
			put_uint(va_arg(ap, uint32_t), 16, 1, width, zpad);
			break;
		case 'f': {
			double v = va_arg(ap, double);
			if (v < 0) { uart_putchar('-'); v = -v; }
			uint32_t int_part = (uint32_t)v;
			put_uint(int_part, 10, 0, width, zpad);
			if (precision > 0) {
				uart_putchar('.');
				double frac = v - int_part;
				for (uint8_t p = 0; p < precision; p++) frac *= 10.0;
				put_uint((uint32_t)frac, 10, 0, precision, 1);
			}
			break;
		}
		case 's':
			uart_puts(va_arg(ap, const char *));
			break;
		case '%':
			uart_putchar('%');
			break;
		default:
			uart_putchar(*fmt);
			break;
		}
	}

}

void uart_printf(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	uart_vprintf(fmt, ap);
	va_end(ap);
}

void uart_readline(char *buffer, uint16_t max_len)
{
	uint16_t pos = 0;

	while (1) {
		while ((USART1->SR & USART_SR_RXNE) == 0)
			;

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
