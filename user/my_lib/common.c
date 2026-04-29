#include "common.h"

/*==============================================================================
 * Receive ring buffer
 *============================================================================*/

#define RX_RING_MASK  (RX_RING_SIZE - 1u)
#define RX_FRAME_HDR  2u   /* 2-byte little-endian length prefix per frame */

static uint8_t           ring_buf[RX_RING_SIZE];
static volatile uint16_t ring_head;   /* consumer advances — main loop only */
static volatile uint16_t ring_tail;   /* producer advances — ISR only       */

static uint16_t ring_used(void)
{
	return (uint16_t)((ring_tail - ring_head) & RX_RING_MASK);
}

/* Called from ISR — must be fast, no blocking */
int rx_rbuf_push(const uint8_t *data, uint16_t len)
{
	uint16_t avail = (uint16_t)(RX_RING_SIZE - 1u - ring_used());

	if (avail < (uint16_t)(RX_FRAME_HDR + len))
		return -1;

	uint16_t t = ring_tail;

	ring_buf[t] = (uint8_t)(len & 0xFF);
	t = (t + 1u) & RX_RING_MASK;
	ring_buf[t] = (uint8_t)(len >> 8);
	t = (t + 1u) & RX_RING_MASK;

	for (uint16_t i = 0; i < len; i++) {
		ring_buf[t] = data[i];
		t = (t + 1u) & RX_RING_MASK;
	}

	ring_tail = t;   /* single aligned store — atomic on Cortex-M3 */
	return 0;
}

/* Called from main loop only */
int rx_rbuf_pop(uint8_t *data, uint16_t *out_len)
{
	if (ring_head == ring_tail)
		return 0;

	uint16_t h = ring_head;

	uint16_t len = ring_buf[h];
	h = (h + 1u) & RX_RING_MASK;
	len |= (uint16_t)ring_buf[h] << 8;
	h = (h + 1u) & RX_RING_MASK;

	if (len == 0 || len > RX_RING_SIZE - RX_FRAME_HDR) {
		ring_head = ring_tail;   /* corrupt state — reset */
		return 0;
	}

	for (uint16_t i = 0; i < len; i++) {
		data[i] = ring_buf[h];
		h = (h + 1u) & RX_RING_MASK;
	}

	*out_len  = len;
	ring_head = h;   /* single aligned store — atomic on Cortex-M3 */
	return 1;
}

int rx_rbuf_is_empty(void)
{
	return ring_head == ring_tail;
}

/*==============================================================================
 * LCG pseudo-random generator
 *============================================================================*/

static uint32_t lcg_state = 1u;

void     common_srand(uint32_t seed) { lcg_state = seed ? seed : 1u; }
uint32_t common_rand(void) { return (lcg_state = lcg_state * 1664525u + 1013904223u); }
