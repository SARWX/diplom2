#ifndef SS_TWR_H
#define SS_TWR_H

#include "net_mac.h"


/* SS TWR function codes */
#define SS_TWR_FUNC_POLL   0xE0
#define SS_TWR_FUNC_RESP   0xE1


int ss_twr_measure_distance(net_addr16_t dst_addr, float* distance);
void ss_twr_handle_rx_frame(const net_message_t* msg);

#endif
