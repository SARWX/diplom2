#ifndef SS_TWR_H
#define SS_TWR_H

#include "net_mac.h"

/* SS TWR function codes */
#define SS_TWR_FUNC_POLL   0xE0
#define SS_TWR_FUNC_RESP   0xE1

void ss_twr_resp_init(void);
int  ss_twr_measure_distance(net_addr16_t dst_addr, float* distance, float* temperature);
int  ss_twr_handle_rx_frame(const net_message_t* msg);
void ss_twr_isr(const uint8_t *frame, uint16_t len);

/* Runtime parameter adjustment */
float ss_twr_read_temperature(void);
void  ss_twr_set_ant_dly(uint16_t tx_dly, uint16_t rx_dly);
void  ss_twr_set_temp_coef(float k);

#endif /* SS_TWR_H */
