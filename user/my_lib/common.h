#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

/*==============================================================================
 * Receive ring buffer
 *
 * Single-producer (ISR) / single-consumer (main loop) lockless ring buffer
 * with variable-length frame support. Each frame is stored as:
 *   [len_lo][len_hi][data bytes...]
 *
 * One byte is kept free at all times to distinguish empty (head == tail)
 * from full without a separate flag or counter.
 *============================================================================*/
#define RX_RING_SIZE 512u   /* must be a power of 2 */

/** Push a frame from ISR context. Returns 0 on success, -1 if full. */
int rx_rbuf_push(const uint8_t *data, uint16_t len);

/** Pop a frame in main-loop context. Returns 1 and fills data/out_len, 0 if empty. */
int rx_rbuf_pop(uint8_t *data, uint16_t *out_len);

/** Returns non-zero when the ring buffer has no pending frames. */
int rx_rbuf_is_empty(void);

/*==============================================================================
 * LCG pseudo-random generator
 *============================================================================*/

void     common_srand(uint32_t seed);
uint32_t common_rand(void);

#endif /* COMMON_H */
