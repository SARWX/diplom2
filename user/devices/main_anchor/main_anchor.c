#include "main_anchor.h"
#include "net_devices.h"
#include "net_mac.h"
#include "uart.h"
#include "cmd_parser.h"
#include "sleep.h"
#include "port.h"
#include <string.h>
#include <stdlib.h>

#define DISCOVERY_PAYLOAD ((const uint8_t*)"DISCOVER")
#define DISCOVERY_PAYLOAD_LEN (sizeof("DISCOVER") - 1)
#define LISTEN_AFTR_BROADCAST_MS 2000

static net_devices_list_t devices;
static uint8_t debug_enabled = 0;

#define debug_printf(...) do { if (debug_enabled) uart_printf(__VA_ARGS__); } while(0)

/*==============================================================================
 * System Management
 *============================================================================*/

static int system_enumerate(void)
{
    uart_puts("\r\n=== Starting ENUMERATION ===\r\n");

    net_devices_init(&devices);
    net_state.mode = NET_MODE_ENUMERATION;

    dwt_forcetrxoff();
    
    if (net_send_broadcast(DISCOVERY_PAYLOAD, DISCOVERY_PAYLOAD_LEN) < 0)
        return -1;

    dwt_rxenable(DWT_START_RX_IMMEDIATE);
    sleep_ms(LISTEN_AFTR_BROADCAST_MS);
    net_state.mode = NET_MODE_IDLE;

    devices.initialized = 1;

    uart_puts("Enumeration completed\r\n");
    net_devices_print(&devices);
    uart_puts("===============================\r\n\r\n");

    return 0;
}

/*==============================================================================
 * Command Handlers
 *============================================================================*/

static void handle_initialize(void)
{
    uart_puts("\r\n>>> Handling INITIALIZE command\r\n");
    
    if (devices.initialized) {
        net_devices_clear(&devices);
    }
    
    if (system_enumerate() == 0) {
        uart_puts("System initialized successfully\r\n");
    } else {
        uart_puts("ERROR: System initialization failed\r\n");
    }
}

static void handle_reset(void)
{
    uart_puts("\r\n>>> Handling RESET command\r\n");
    net_devices_clear(&devices);
    devices.initialized = 0;
    uart_puts("System reset complete\r\n");
}

static void handle_get_status(void)
{
    uart_puts("\r\n>>> Handling GET STATUS command\r\n");
    uart_printf("Initialized: %s\r\n", devices.initialized ? "YES" : "NO");
    uart_printf("Total devices: %d\r\n", devices.total_anchors);
    uart_printf("Debug mode: %s\r\n", debug_enabled ? "ON" : "OFF");
    net_devices_print(&devices);
}

static void handle_debug(uint8_t enable)
{
    debug_enabled = enable;
    net_devices_set_debug(enable);
    uart_printf("Debug mode %s\r\n", enable ? "enabled" : "disabled");
}

static void process_command(cmd_parse_result_t cmd)
{
    if (!cmd.valid) {
        uart_puts("ERROR: Invalid command\r\n");
        return;
    }
    
    switch (cmd.code) {
        case CMD_INITIALIZE:    handle_initialize(); break;
        case CMD_RESET:         handle_reset(); break;
        case CMD_GET_STATUS:    handle_get_status(); break;
        case CMD_DEBUG_ON:      handle_debug(1); break;
        case CMD_DEBUG_OFF:     handle_debug(0); break;
        default:
            uart_printf("Command not implemented: %s\r\n", cmd_to_string(cmd.code));
            break;
    }
}

/*==============================================================================
 * DW1000 Callbacks
 *============================================================================*/

static void rx_ok_cb(const dwt_cb_data_t *cb_data)
{
    net_message_t msg;

    if (cb_data->datalength > sizeof(net_state.rx_buffer))
        return;

    dwt_readrxdata(net_state.rx_buffer, cb_data->datalength, 0);

    if (!net_parse_message(net_state.rx_buffer, cb_data->datalength, &msg))
        return;

    if (net_state.mode == NET_MODE_ENUMERATION && msg.payload_len >= 1 && msg.payload[0] == 'A') {
        uint8_t mac[MAC_ADDR_LEN] = {0};
        
        if (msg.src_is_eui64) {
            memcpy(mac, &msg.src_eui64, MAC_ADDR_LEN);
        } else {
            mac[0] = msg.src_addr16 & 0xFF;
            mac[1] = (msg.src_addr16 >> 8) & 0xFF;
        }
        
        net_device_t* device = net_device_create(mac, 0);
        if (device) {
            net_device_add(&devices, device);
        }
    }
    
    dwt_rxenable(DWT_START_RX_IMMEDIATE);
}

static void rx_err_cb(const dwt_cb_data_t *cb_data)
{
    (void)cb_data;
    dwt_rxenable(DWT_START_RX_IMMEDIATE);
}

/*==============================================================================
 * Device Interface
 *============================================================================*/

void main_anchor_init(void)
{
    uart_init(115200);
    
    port_set_deca_isr(dwt_isr);
    dwt_setcallbacks(NULL, rx_ok_cb, NULL, rx_err_cb);
    dwt_setinterrupt(DWT_INT_RFCG | DWT_INT_RPHE | DWT_INT_RFCE | DWT_INT_RFSL, 1);
    dwt_rxenable(DWT_START_RX_IMMEDIATE);

    uart_puts("\r\n========================================\r\n");
    uart_puts("Main Anchor Station\r\n");
    uart_puts("========================================\r\n");
    uart_puts("Commands: INITIALIZE, RESET, GET_STATUS, DEBUG_ON/OFF\r\n");
    uart_puts("> ");
}

void main_anchor_loop(void)
{
    static char line_buffer[128];
    
    uart_readline(line_buffer, sizeof(line_buffer));
    cmd_parse_result_t cmd = cmd_parse(line_buffer);
    process_command(cmd);
    
    uart_puts("\r\n> ");
}
