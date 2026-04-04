#include "deca_device_api.h"
#include "main_anchor.h"
#include "device_id.h"
#include "net_mac.h"
#include "uart.h"
#include "cmd_parser.h"
#include "sleep.h"
#include "port.h"
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
// DEBUG
#include "stm32f10x_usart.h"
#include "stm32f10x_rcc.h"

extern struct net_state;

/*==============================================================================
 * Internal State
 *============================================================================*/

static uint8_t debug_enabled = 0;
static net_devices_list_t net_devices_list;

/*==============================================================================
 * Debug Output
 *============================================================================*/

static void debug_printf(const char* format, ...)
{
	;
}

/*==============================================================================
 * Anchor List Management
 *============================================================================*/

net_device_t* anchor_create(const uint8_t* mac, uint8_t seq_id)
{
    net_device_t* new_anchor = (net_device_t*)malloc(sizeof(net_device_t));
    if (!new_anchor) {
        uart_puts("ERROR: Failed to allocate memory for anchor\r\n");
        return NULL;
    }
    
    memcpy(new_anchor->mac_address, mac, 6);
    new_anchor->seq_id = seq_id;
    new_anchor->next = NULL;
    
    new_anchor->distances = (float*)calloc(MAX_DISTANCES, sizeof(float));
    if (!new_anchor->distances) {
        uart_puts("ERROR: Failed to allocate memory for distances array\r\n");
        free(new_anchor);
        return NULL;
    }
    
    for (int i = 0; i < MAX_DISTANCES; i++) {
        new_anchor->distances[i] = -1.0f;
    }
    
    debug_printf("Created anchor: seq_id=%d, MAC=%02X:%02X:%02X:%02X:%02X:%02X\r\n",
                 seq_id, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    return new_anchor;
}

void anchor_add(net_devices_list_t* lst, net_device_t* new_anchor)
{
    if (!lst || !new_anchor) return;
    
    if (anchor_find_by_mac(lst, new_anchor->mac_address)) {
        uart_printf("ERROR: Anchor with MAC %02X:%02X:%02X:%02X:%02X:%02X already exists\r\n",
                    new_anchor->mac_address[0], new_anchor->mac_address[1],
                    new_anchor->mac_address[2], new_anchor->mac_address[3],
                    new_anchor->mac_address[4], new_anchor->mac_address[5]);
        free(new_anchor->distances);
        free(new_anchor);
        return;
    }
    
    new_anchor->seq_id = lst->total_anchors + 1;
    new_anchor->next = lst->head;
    lst->head = new_anchor;
    lst->total_anchors++;
    
    debug_printf("Added anchor seq_id=%d, Total: %d\r\n",
                 new_anchor->seq_id, lst->total_anchors);
}

void anchor_remove(net_devices_list_t* lst, uint8_t seq_id)
{
    if (!lst || !lst->head) return;
    
    net_device_t* current = lst->head;
    net_device_t* prev = NULL;
    
    while (current) {
        if (current->seq_id == seq_id) {
            if (prev) {
                prev->next = current->next;
            } else {
                lst->head = current->next;
            }
            free(current->distances);
            free(current);
            lst->total_anchors--;
            debug_printf("Removed anchor with seq_id %d\r\n", seq_id);
            return;
        }
        prev = current;
        current = current->next;
    }
    
    uart_printf("ERROR: Anchor with seq_id %d not found\r\n", seq_id);
}

net_device_t* anchor_find_by_seq(net_devices_list_t* lst, uint8_t seq_id)
{
    if (!lst) return NULL;
    
    net_device_t* current = lst->head;
    while (current) {
        if (current->seq_id == seq_id) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

net_device_t* anchor_find_by_mac(net_devices_list_t* lst, const uint8_t* mac)
{
    if (!lst) return NULL;
    
    net_device_t* current = lst->head;
    while (current) {
        if (memcmp(current->mac_address, mac, 6) == 0) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

void anchor_free_all(net_devices_list_t* lst)
{
    if (!lst) return;
    
    net_device_t* current = lst->head;
    while (current) {
        net_device_t* next = current->next;
        free(current->distances);
        free(current);
        current = next;
    }
    lst->head = NULL;
    lst->total_anchors = 0;
    debug_printf("Freed all anchors\r\n");
}

void anchor_print_list(net_devices_list_t* lst)
{
    if (!lst) return;
    
    uart_printf("=== Anchor List (Total: %d) ===\r\n", lst->total_anchors);
    net_device_t* current = lst->head;
    while (current) {
        uart_printf("  Seq ID: %d, MAC: %02X:%02X:%02X:%02X:%02X:%02X\r\n",
                    current->seq_id,
                    current->mac_address[0], current->mac_address[1],
                    current->mac_address[2], current->mac_address[3],
                    current->mac_address[4], current->mac_address[5]);
        current = current->next;
    }
    uart_puts("==============================\r\n");
}

void anchor_update_distance(net_device_t* from, uint8_t to_seq_id, float distance)
{
    if (!from || to_seq_id >= MAX_DISTANCES) return;
    from->distances[to_seq_id] = distance;
    debug_printf("Updated distance: from seq_id %d to %d = %.2f\r\n",
                 from->seq_id, to_seq_id, distance);
}

/*==============================================================================
 * System Management
 *============================================================================*/
int system_enumerate(net_devices_list_t* lst)
{
    if (!lst) return -1;

    uart_puts("\r\n=== Starting ENUMERATION ===\r\n");

    // подготовка
    lst->head = NULL;
    lst->total_anchors = 0;
    lst->initialized = 0;

    set_net_mode(NET_MODE_ENUMERATION);

    if (net_send_broadcast(DISCOVERY_PAYLOAD, DISCOVERY_PAYLOAD_LEN) < 0)
        return -1;

    sleep_ms(LISTEN_AFTR_BROADCAST_MS);

    set_net_mode(NET_MODE_IDLE);

    lst->initialized = 1;

    uart_puts("Enumeration completed\r\n");
    anchor_print_list(lst);
    uart_puts("===============================\r\n\r\n");

    return 0;
}

int system_configure(net_devices_list_t* lst)
{
    if (!lst || !lst->initialized) {
        uart_puts("ERROR: System not initialized\r\n");
        return -1;
    }
    
    uart_puts("=== Reconfiguring system ===\r\n");
    uart_puts("System reconfigured\r\n");
    return 0;
}

/*==============================================================================
 * Command Handlers
 *============================================================================*/
void handle_reset(net_devices_list_t* lst);

void handle_initialize_system(net_devices_list_t* lst)
{
    uart_puts("\r\n>>> Handling INITIALIZE SYSTEM command\r\n");
    
    if (lst->initialized) {
        uart_puts("System already initialized. Resetting...\r\n");
        handle_reset(lst);
    }
    
    if (system_enumerate(lst) == 0) {
        uart_puts("System initialized successfully\r\n");
    } else {
        uart_puts("ERROR: System initialization failed\r\n");
    }
}

void handle_reconfigure(net_devices_list_t* lst)
{
    uart_puts("\r\n>>> Handling RECONFIGURE command\r\n");
    
    if (!lst->initialized) {
        uart_puts("ERROR: System not initialized. Please run INITIALIZE SYSTEM first\r\n");
        return;
    }
    
    system_configure(lst);
}

void handle_start(net_devices_list_t* lst)
{
    uart_puts("\r\n>>> Handling START command\r\n");
    
    if (!lst->initialized) {
        uart_puts("ERROR: System not initialized\r\n");
        return;
    }
    
    uart_puts("Starting positioning system...\r\n");
}

void handle_stop(net_devices_list_t* lst)
{
    uart_puts("\r\n>>> Handling STOP command\r\n");
    uart_puts("Stopping positioning system...\r\n");
}

void handle_get_status(net_devices_list_t* lst)
{
    uart_puts("\r\n>>> Handling GET STATUS command\r\n");
    
    uart_printf("System Status:\r\n");
    uart_printf("  Initialized: %s\r\n", lst->initialized ? "YES" : "NO");
    uart_printf("  Total anchors: %d\r\n", lst->total_anchors);
    uart_printf("  Debug mode: %s\r\n", debug_enabled ? "ON" : "OFF");
}

void handle_get_config(net_devices_list_t* lst)
{
    uart_puts("\r\n>>> Handling GET CONFIG command\r\n");
    anchor_print_list(lst);
}

void handle_set_param(net_devices_list_t* lst, const char* args)
{
    uart_puts("\r\n>>> Handling SET PARAM command\r\n");
    
    if (!args) {
        uart_puts("ERROR: Missing arguments. Usage: SET PARAM <param_name> <value>\r\n");
        return;
    }
    
    uart_printf("Setting parameter: %s\r\n", args);
}

void handle_calibrate(net_devices_list_t* lst)
{
    uart_puts("\r\n>>> Handling CALIBRATE command\r\n");
    
    if (!lst->initialized) {
        uart_puts("ERROR: System not initialized\r\n");
        return;
    }
    
    uart_puts("Starting calibration process...\r\n");
}

void handle_reset(net_devices_list_t* lst)
{
    uart_puts("\r\n>>> Handling RESET command\r\n");
    
    anchor_free_all(lst);
    lst->initialized = 0;
    lst->total_anchors = 0;
    
    uart_puts("System reset complete\r\n");
}

void handle_debug_on(net_devices_list_t* lst)
{
    uart_puts("\r\n>>> Handling DEBUG ON command\r\n");
    debug_enabled = 1;
    uart_puts("Debug mode enabled\r\n");
}

void handle_debug_off(net_devices_list_t* lst)
{
    uart_puts("\r\n>>> Handling DEBUG OFF command\r\n");
    debug_enabled = 0;
    uart_puts("Debug mode disabled\r\n");
}

void handle_save_config(net_devices_list_t* lst)
{
    uart_puts("\r\n>>> Handling SAVE CONFIG command\r\n");
    
    if (!lst->initialized) {
        uart_puts("ERROR: System not initialized\r\n");
        return;
    }
    
    uart_puts("Saving configuration to non-volatile memory...\r\n");
}

void handle_load_config(net_devices_list_t* lst)
{
    uart_puts("\r\n>>> Handling LOAD CONFIG command\r\n");
    uart_puts("Loading configuration from non-volatile memory...\r\n");
}

void process_command(net_devices_list_t* lst, cmd_parse_result_t cmd)
{
    if (!cmd.valid) {
        uart_puts("ERROR: Invalid command\r\n");
        return;
    }
    
    switch (cmd.code) {
        case CMD_INITIALIZE_SYSTEM: handle_initialize_system(lst); break;
        case CMD_RECONFIGURE:       handle_reconfigure(lst);       break;
        case CMD_START:             handle_start(lst);             break;
        case CMD_STOP:              handle_stop(lst);              break;
        case CMD_GET_STATUS:        handle_get_status(lst);        break;
        case CMD_GET_CONFIG:        handle_get_config(lst);        break;
        case CMD_SET_PARAM:         handle_set_param(lst, cmd.args); break;
        case CMD_CALIBRATE:         handle_calibrate(lst);         break;
        case CMD_RESET:             handle_reset(lst);             break;
        case CMD_DEBUG_ON:          handle_debug_on(lst);          break;
        case CMD_DEBUG_OFF:         handle_debug_off(lst);         break;
        case CMD_SAVE_CONFIG:       handle_save_config(lst);       break;
        case CMD_LOAD_CONFIG:       handle_load_config(lst);       break;
        default:
            uart_printf("Command not implemented: %s\r\n", cmd_to_string(cmd.code));
            break;
    }
}

/*==============================================================================
 * Device Interface Functions
 *============================================================================*/

void main_anchor_init(device_config_t* dev)
{
    (void)dev;

    // привязка ISR к пину IRQ
    port_set_deca_isr(dwt_isr);

    // коллбеки DW1000
    dwt_setcallbacks(NULL, dw1000_rx_ok_cb, NULL, dw1000_rx_err_cb);

    // включаем прерывания
    dwt_setinterrupt(
        DWT_INT_RFCG |
        DWT_INT_RPHE |
        DWT_INT_RFCE |
        DWT_INT_RFSL,
        1
    );

    /* Инициализация системы с MAC адресом из device_config */
    // включаем прием
    dwt_rxenable(DWT_START_RX_IMMEDIATE);
   system_enumerate(&net_devices_list);
    // uart_init(9600);

    // RCC_ClocksTypeDef clocks;
    // RCC_GetClocksFreq(&clocks);

    // while (1) {
    //     USART_SendData(USART1, (uint16_t)'B');
    //     while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    //     sleep_ms(100);
    // }

    /* Настройка UART для команд */
    uart_set_line_callback(uart_line_callback);
    
    uart_puts("\r\n========================================\r\n");
    uart_puts("Main Anchor Station - Local Positioning System\r\n");
    uart_puts("========================================\r\n\r\n");
    uart_puts("System ready. Enter commands:\r\n");
   uart_puts("  INITIALIZE SYSTEM, RECONFIGURE, START, STOP, GET STATUS,\r\n");
    uart_puts("  GET CONFIG, SET PARAM <args>, CALIBRATE, RESET,\r\n");
    uart_puts("  DEBUG ON/OFF, SAVE CONFIG, LOAD CONFIG\r\n\r\n");
    uart_puts("> ");
}

void main_anchor_loop(device_config_t* dev)
{
    (void)dev;
    
    /* Основной цикл - обрабатываем сетевые события */
    
    /* Небольшая задержка */
    for (volatile int i = 0; i < 1000; i++);
}

/* Callback для UART команд */
void uart_line_callback(const char* line, uint16_t len)
{
    (void)len;
    
    cmd_parse_result_t cmd = cmd_parse(line);
    process_command(&net_devices_list, cmd);
    
    uart_puts("\r\n> ");
}
