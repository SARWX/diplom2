#ifndef NET_DISPATCH_H
#define NET_DISPATCH_H

#include "net_mac.h"
#include "net_devices.h"

/*==============================================================================
 * Common radio initialisation
 *
 * Sets the DW1000 ISR, registers shared callbacks, enables all relevant
 * interrupts, and arms the receiver.  Call once from every device's init
 * function (after dwt_configure / net_init).
 *============================================================================*/
void net_radio_init(void);

/*==============================================================================
 * Common RX dispatch
 *
 * Pops one frame from the ring buffer and routes it:
 *   1. ss_twr  — handled unconditionally (time-critical, highest priority)
 *   2. mode-based — enumeration, sync-wait, config handled automatically
 *   3. idle_fn  — device-specific handler for NET_MODE_IDLE / RANGING
 *                 (may be NULL if the device has nothing to do in idle)
 *
 * Re-arms the DW1000 receiver before returning.
 * Returns 1 if a frame was processed, 0 if the ring buffer was empty.
 *============================================================================*/
typedef void (*net_idle_fn_t)(net_devices_list_t *devices, net_message_t *msg);

int net_process(net_devices_list_t *devices, net_idle_fn_t idle_fn);

#endif /* NET_DISPATCH_H */
