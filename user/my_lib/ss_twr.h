#ifndef SS_TWR_H
#define SS_TWR_H

#include <stdint.h>

int ss_twr_measure_distance(float *distance);
int ss_twr_handle_poll(const uint8_t *frame, uint16_t frame_len);

#endif /* SS_TWR_H */
