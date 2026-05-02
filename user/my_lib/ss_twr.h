#ifndef SS_TWR_H
#define SS_TWR_H

#include "net_mac.h"

#define SS_TWR_FUNC_POLL 0xE0

int ss_twr_measure_distance(net_addr16_t dst_addr, float *distance);
int ss_twr_handle_poll(const uint8_t *frame, uint16_t frame_len);

#endif /* SS_TWR_H */
