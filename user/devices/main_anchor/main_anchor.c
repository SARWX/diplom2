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
#include <stdarg.h>
#include <stdio.h>
// DEBUG
#include "stm32f10x_usart.h"
#include "stm32f10x_rcc.h"

extern struct net_state;

/*==============================================================================
 * Internal State
 *============================================================================*/

static uint8_t debug_enabled = 0;
static net_devices_list_t net_devices_list;
static net_devices_list_t* lst = &net_devices_list;

/*==============================================================================
 * Debug Output
 *============================================================================*/

#define debug_printf(...) do { if (debug_enabled) uart_printf(__VA_ARGS__); } while(0)

/*==============================================================================
 * Anchor List Management
 *============================================================================*/
static net_device_t* anchor_find_by_seq(uint8_t seq_id)
{    
    net_device_t* current = lst->head;
    while (current) {
        if (current->seq_id == seq_id) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

static net_device_t* anchor_find_by_mac(const uint8_t* mac)
{
    net_device_t* current = lst->head;
    while (current) {
        if (memcmp(current->mac_address, mac, MAC_ADDR_LEN) == 0) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

static net_device_t* anchor_create(const uint8_t* mac, uint8_t seq_id)
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

static int anchor_add(net_device_t* new_anchor)
{
    if (!new_anchor) return -1;

    if (anchor_find_by_mac(new_anchor->mac_address)) {
        uart_printf("ERROR: Anchor with MAC %02X:%02X:%02X:%02X:%02X:%02X already exists\r\n",
                    new_anchor->mac_address[0], new_anchor->mac_address[1],
                    new_anchor->mac_address[2], new_anchor->mac_address[3],
                    new_anchor->mac_address[4], new_anchor->mac_address[5]);
        free(new_anchor->distances);
        free(new_anchor);
        return -2;
    }
    
    new_anchor->seq_id = lst->total_anchors + 1;
    new_anchor->next = lst->head;
    lst->head = new_anchor;
    lst->total_anchors++;
    
    debug_printf("Added anchor seq_id=%d, Total: %d\r\n",
                 new_anchor->seq_id, lst->total_anchors);

    return new_anchor->seq_id;
}

static void anchor_remove(uint8_t seq_id)
{
    if (!lst->head) return;
    
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

static void anchor_free_all(void)
{
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

static void anchor_print_list(void)
{    
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

static void anchor_update_distance(net_device_t* from, uint8_t to_seq_id, float distance)
{
    if (!from || to_seq_id >= MAX_DISTANCES) return;
    from->distances[to_seq_id] = distance;
    debug_printf("Updated distance: from seq_id %d to %d = %.2f\r\n",
                 from->seq_id, to_seq_id, distance);
}

/*==============================================================================
 * System Management
 *============================================================================*/
static int system_enumerate(void)
{
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
    anchor_print_list();
    uart_puts("===============================\r\n\r\n");

    return 0;
}

/*==============================================================================
 * Command Handlers
 *============================================================================*/
static void handle_reset(void);

static void handle_initialize_system(void)
{
    uart_puts("\r\n>>> Handling INITIALIZE SYSTEM command\r\n");
    
    if (lst->initialized) {
        uart_puts("System already initialized. Resetting...\r\n");
        handle_reset();
    }

    if (system_enumerate() == 0) {
        uart_puts("System initialized successfully\r\n");
    } else {
        uart_puts("ERROR: System initialization failed\r\n");
    }
}

/* NOT IMPLEMENTED */
static void handle_start(void)
{
    uart_puts("\r\n>>> Handling START command\r\n");
    
    if (!lst->initialized) {
        uart_puts("ERROR: System not initialized\r\n");
        return;
    }

    uart_puts("Starting positioning system...\r\n");
}

/* NOT IMPLEMENTED */
static void handle_stop(void)
{
    uart_puts("\r\n>>> Handling STOP command\r\n");
    uart_puts("Stopping positioning system...\r\n");
}

static void handle_get_status(void)
{
    uart_puts("\r\n>>> Handling GET STATUS command\r\n");
    
    uart_printf("System Status:\r\n");
    uart_printf("  Initialized: %s\r\n", net_devices_list.initialized ? "YES" : "NO");
    uart_printf("  Total anchors: %d\r\n", net_devices_list.total_anchors);
    uart_printf("  Debug mode: %s\r\n", debug_enabled ? "ON" : "OFF");
    
    uart_puts("\r\n>>> Anchor List:\r\n");
    anchor_print_list();
}

/* NOT IMPLEMENTED */
static void handle_set_param(const char* args)
{
    uart_puts("\r\n>>> Handling SET PARAM command\r\n");
    if (!args) {
        uart_puts("ERROR: Missing arguments. Usage: SET PARAM <param_name> <value>\r\n");
        return;
    }
    uart_printf("Setting parameter: %s\r\n", args);
}

/* NOT IMPLEMENTED */
static void handle_calibrate(void)
{
    uart_puts("\r\n>>> Handling CALIBRATE command\r\n");
    if (!lst->initialized) {
        uart_puts("ERROR: System not initialized\r\n");
        return;
    }
    uart_puts("Starting calibration process...\r\n");
}

static void handle_reset(void)
{
    uart_puts("\r\n>>> Handling RESET command\r\n");
    anchor_free_all();
    lst->initialized = 0;
    lst->total_anchors = 0;
    uart_puts("System reset complete\r\n");
}

static void handle_debug(uint8_t enable)
{
    uart_printf("\r\n>>> Handling DEBUG %s command\r\n", enable ? "ON" : "OFF");
    debug_enabled = enable;
    uart_printf("Debug mode %s\r\n", enable ? "enabled" : "disabled");
}

void process_command(net_devices_list_t* lst, cmd_parse_result_t cmd)
{
    if (!cmd.valid) {
        uart_puts("ERROR: Invalid command\r\n");
        return;
    }
    
    switch (cmd.code) {
        case CMD_INITIALIZE_SYSTEM: handle_initialize_system(); break;
        case CMD_START:             handle_start();             break;
        case CMD_STOP:              handle_stop();              break;
        case CMD_GET_STATUS:        handle_get_status();        break;
        case CMD_SET_PARAM:         handle_set_param(cmd.args); break;
        case CMD_CALIBRATE:         handle_calibrate();         break;
        case CMD_RESET:             handle_reset();             break;
        case CMD_DEBUG_ON:          handle_debug(1);          break;
        case CMD_DEBUG_OFF:         handle_debug(0);         break;
        default:
            uart_printf("Command not implemented: %s\r\n", cmd_to_string(cmd.code));
            break;
    }
}

/*==============================================================================
 * Net communication Interface Functions
 *============================================================================*/

static void dw1000_rx_ok_cb(const dwt_cb_data_t *cb_data)
{
    net_message_t msg;

    dwt_rxenable(DWT_START_RX_IMMEDIATE | DWT_NO_SYNC_PTRS);

    if (cb_data->datalength > sizeof(net_state.rx_buffer))
        return;

    dwt_readrxdata(net_state.rx_buffer, cb_data->datalength, 0);

    if (!net_parse_message(net_state.rx_buffer, cb_data->datalength, &msg))
        return;

    switch (net_state.mode)
    {
        case NET_MODE_ENUMERATION:
            /* Ответ анкера - просто маркер 'A' в payload */
            if (msg.payload_len >= 1 && msg.payload[0] == 'A')
            {
                /* Создаём временную структуру с MAC из заголовка */
                net_device_t temp_anchor;
                memset(&temp_anchor, 0, sizeof(temp_anchor));
                
                /* Берём MAC из распарсенного сообщения (64-bit или 16-bit) */
                if (msg.src_is_eui64) {
                    memcpy(temp_anchor.mac_address, &msg.src_eui64, 6);
                } else {
                    /* 16-bit адрес расширяем до 6 байт (старшие байты 0) */
                    /* это временное решение :( */
                    temp_anchor.mac_address[0] = msg.src_addr16 & 0xFF;
                    temp_anchor.mac_address[1] = (msg.src_addr16 >> 8) & 0xFF;
                    temp_anchor.mac_address[2] = 0;
                    temp_anchor.mac_address[3] = 0;
                    temp_anchor.mac_address[4] = 0;
                    temp_anchor.mac_address[5] = 0;
                }

                anchor_add(&temp_anchor);
            }
            break;

        default:
            break;
    }
}

static void dw1000_rx_err_cb(const dwt_cb_data_t *cb_data)
{
    (void)cb_data;
    dwt_rxenable(DWT_START_RX_IMMEDIATE);
}


/*==============================================================================
 * Device Interface Functions
 *============================================================================*/

void main_anchor_init(device_config_t* dev)
{
    (void)dev;

    // Регистрируем обработчик прерывания от DW1000
    port_set_deca_isr(dwt_isr);

    // Регистрируем коллбеки на события DW1000 (успешный приём и ошибка)
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

    // TRY IT WITH NEW USB-UART CONVERTER old is broken
    // while (1) {
    //     USART_SendData(USART1, (uint16_t)'B');
    //     while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    //     sleep_ms(100);
    // }

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
    
    static char line_buffer[128];
    
    uart_readline(line_buffer, sizeof(line_buffer));
    cmd_parse_result_t cmd = cmd_parse(line_buffer);
    process_command(&net_devices_list, cmd);
    
    /* Выводим приглашение */
    uart_puts("\r\n> ");
}
