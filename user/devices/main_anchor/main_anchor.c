#include "main_anchor.h"
#include "device_id.h"
#include "net_mac.h"
#include "uart.h"
#include "cmd_parser.h"
#include <string.h>
#include <stdarg.h>

/*==============================================================================
 * Internal State
 *============================================================================*/

static uint8_t debug_enabled = 0;
static system_context_t system_ctx;

/*==============================================================================
 * Debug Output
 *============================================================================*/

static void debug_printf(const char* format, ...)
{
    if (debug_enabled) {
        va_list args;
        va_start(args, format);
        char buffer[128];
        vsnprintf(buffer, sizeof(buffer), format, args);
        uart_puts(buffer);
        va_end(args);
    }
}

/*==============================================================================
 * Anchor List Management
 *============================================================================*/

anchor_t* anchor_create(const uint8_t* mac, uint8_t seq_id)
{
    anchor_t* new_anchor = (anchor_t*)malloc(sizeof(anchor_t));
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

void anchor_add(system_context_t* ctx, anchor_t* new_anchor)
{
    if (!ctx || !new_anchor) return;
    
    if (anchor_find_by_mac(ctx, new_anchor->mac_address)) {
        uart_printf("ERROR: Anchor with MAC %02X:%02X:%02X:%02X:%02X:%02X already exists\r\n",
                    new_anchor->mac_address[0], new_anchor->mac_address[1],
                    new_anchor->mac_address[2], new_anchor->mac_address[3],
                    new_anchor->mac_address[4], new_anchor->mac_address[5]);
        free(new_anchor->distances);
        free(new_anchor);
        return;
    }
    
    new_anchor->seq_id = ctx->total_anchors + 1;
    new_anchor->next = ctx->head;
    ctx->head = new_anchor;
    ctx->total_anchors++;
    
    debug_printf("Added anchor seq_id=%d, Total: %d\r\n",
                 new_anchor->seq_id, ctx->total_anchors);
}

void anchor_remove(system_context_t* ctx, uint8_t seq_id)
{
    if (!ctx || !ctx->head) return;
    
    anchor_t* current = ctx->head;
    anchor_t* prev = NULL;
    
    while (current) {
        if (current->seq_id == seq_id) {
            if (prev) {
                prev->next = current->next;
            } else {
                ctx->head = current->next;
            }
            free(current->distances);
            free(current);
            ctx->total_anchors--;
            debug_printf("Removed anchor with seq_id %d\r\n", seq_id);
            return;
        }
        prev = current;
        current = current->next;
    }
    
    uart_printf("ERROR: Anchor with seq_id %d not found\r\n", seq_id);
}

anchor_t* anchor_find_by_seq(system_context_t* ctx, uint8_t seq_id)
{
    if (!ctx) return NULL;
    
    anchor_t* current = ctx->head;
    while (current) {
        if (current->seq_id == seq_id) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

anchor_t* anchor_find_by_mac(system_context_t* ctx, const uint8_t* mac)
{
    if (!ctx) return NULL;
    
    anchor_t* current = ctx->head;
    while (current) {
        if (memcmp(current->mac_address, mac, 6) == 0) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

void anchor_free_all(system_context_t* ctx)
{
    if (!ctx) return;
    
    anchor_t* current = ctx->head;
    while (current) {
        anchor_t* next = current->next;
        free(current->distances);
        free(current);
        current = next;
    }
    ctx->head = NULL;
    ctx->total_anchors = 0;
    debug_printf("Freed all anchors\r\n");
}

void anchor_print_list(system_context_t* ctx)
{
    if (!ctx) return;
    
    uart_printf("=== Anchor List (Total: %d) ===\r\n", ctx->total_anchors);
    anchor_t* current = ctx->head;
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

void anchor_update_distance(anchor_t* from, uint8_t to_seq_id, float distance)
{
    if (!from || to_seq_id >= MAX_DISTANCES) return;
    from->distances[to_seq_id] = distance;
    debug_printf("Updated distance: from seq_id %d to %d = %.2f\r\n",
                 from->seq_id, to_seq_id, distance);
}

/*==============================================================================
 * System Management
 *============================================================================*/

int system_init(system_context_t* ctx, const uint8_t* my_mac)
{
    if (!ctx) return -1;
    
    memset(ctx, 0, sizeof(system_context_t));
    memcpy(ctx->my_mac, my_mac, 6);
    ctx->initialized = 0;
    ctx->head = NULL;
    ctx->total_anchors = 0;
    
    uart_printf("System initialized. My MAC: %02X:%02X:%02X:%02X:%02X:%02X\r\n",
                my_mac[0], my_mac[1], my_mac[2], my_mac[3], my_mac[4], my_mac[5]);
    
    return 0;
}

int system_enumerate(system_context_t* ctx)
{
    if (!ctx) return -1;
    
    uart_puts("\r\n=== Starting ENUMERATION ===\r\n");
    uart_puts("Sending BROADCAST discovery messages...\r\n");
    
    /* Отправляем broadcast запрос и ждем ответы */
    uint8_t discovery_payload[] = {'D', 'I', 'S', 'C', 'O', 'V', 'E', 'R'};
    int response = net_send_broadcast_with_response(discovery_payload, sizeof(discovery_payload));
    
    if (response == 1) {
        uart_puts("Received responses from anchors\r\n");
        /* TODO: Парсить полученные ответы и добавлять в список */
    } else {
        uart_puts("No responses received\r\n");
    }
    
    /* TODO: Назначить себе seq_id */
    ctx->my_seq_id = 1;
    ctx->initialized = 1;
    
    uart_printf("Enumeration completed. I am seq_id: %d\r\n", ctx->my_seq_id);
    anchor_print_list(ctx);
    uart_puts("===============================\r\n\r\n");
    
    return 0;
}

int system_configure(system_context_t* ctx)
{
    if (!ctx || !ctx->initialized) {
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

void handle_initialize_system(system_context_t* ctx)
{
    uart_puts("\r\n>>> Handling INITIALIZE SYSTEM command\r\n");
    
    if (ctx->initialized) {
        uart_puts("System already initialized. Resetting...\r\n");
        handle_reset(ctx);
    }
    
    if (system_enumerate(ctx) == 0) {
        uart_puts("System initialized successfully\r\n");
    } else {
        uart_puts("ERROR: System initialization failed\r\n");
    }
}

void handle_reconfigure(system_context_t* ctx)
{
    uart_puts("\r\n>>> Handling RECONFIGURE command\r\n");
    
    if (!ctx->initialized) {
        uart_puts("ERROR: System not initialized. Please run INITIALIZE SYSTEM first\r\n");
        return;
    }
    
    system_configure(ctx);
}

void handle_start(system_context_t* ctx)
{
    uart_puts("\r\n>>> Handling START command\r\n");
    
    if (!ctx->initialized) {
        uart_puts("ERROR: System not initialized\r\n");
        return;
    }
    
    uart_puts("Starting positioning system...\r\n");
}

void handle_stop(system_context_t* ctx)
{
    uart_puts("\r\n>>> Handling STOP command\r\n");
    uart_puts("Stopping positioning system...\r\n");
}

void handle_get_status(system_context_t* ctx)
{
    uart_puts("\r\n>>> Handling GET STATUS command\r\n");
    
    uart_printf("System Status:\r\n");
    uart_printf("  Initialized: %s\r\n", ctx->initialized ? "YES" : "NO");
    uart_printf("  Total anchors: %d\r\n", ctx->total_anchors);
    uart_printf("  My seq_id: %d\r\n", ctx->my_seq_id);
    uart_printf("  Debug mode: %s\r\n", debug_enabled ? "ON" : "OFF");
}

void handle_get_config(system_context_t* ctx)
{
    uart_puts("\r\n>>> Handling GET CONFIG command\r\n");
    anchor_print_list(ctx);
}

void handle_set_param(system_context_t* ctx, const char* args)
{
    uart_puts("\r\n>>> Handling SET PARAM command\r\n");
    
    if (!args) {
        uart_puts("ERROR: Missing arguments. Usage: SET PARAM <param_name> <value>\r\n");
        return;
    }
    
    uart_printf("Setting parameter: %s\r\n", args);
}

void handle_calibrate(system_context_t* ctx)
{
    uart_puts("\r\n>>> Handling CALIBRATE command\r\n");
    
    if (!ctx->initialized) {
        uart_puts("ERROR: System not initialized\r\n");
        return;
    }
    
    uart_puts("Starting calibration process...\r\n");
}

void handle_reset(system_context_t* ctx)
{
    uart_puts("\r\n>>> Handling RESET command\r\n");
    
    anchor_free_all(ctx);
    ctx->initialized = 0;
    ctx->total_anchors = 0;
    ctx->my_seq_id = 0;
    
    uart_puts("System reset complete\r\n");
}

void handle_debug_on(system_context_t* ctx)
{
    uart_puts("\r\n>>> Handling DEBUG ON command\r\n");
    debug_enabled = 1;
    uart_puts("Debug mode enabled\r\n");
}

void handle_debug_off(system_context_t* ctx)
{
    uart_puts("\r\n>>> Handling DEBUG OFF command\r\n");
    debug_enabled = 0;
    uart_puts("Debug mode disabled\r\n");
}

void handle_save_config(system_context_t* ctx)
{
    uart_puts("\r\n>>> Handling SAVE CONFIG command\r\n");
    
    if (!ctx->initialized) {
        uart_puts("ERROR: System not initialized\r\n");
        return;
    }
    
    uart_puts("Saving configuration to non-volatile memory...\r\n");
}

void handle_load_config(system_context_t* ctx)
{
    uart_puts("\r\n>>> Handling LOAD CONFIG command\r\n");
    uart_puts("Loading configuration from non-volatile memory...\r\n");
}

void process_command(system_context_t* ctx, cmd_parse_result_t cmd)
{
    if (!cmd.valid) {
        uart_puts("ERROR: Invalid command\r\n");
        return;
    }
    
    switch (cmd.code) {
        case CMD_INITIALIZE_SYSTEM: handle_initialize_system(ctx); break;
        case CMD_RECONFIGURE:       handle_reconfigure(ctx);       break;
        case CMD_START:             handle_start(ctx);             break;
        case CMD_STOP:              handle_stop(ctx);              break;
        case CMD_GET_STATUS:        handle_get_status(ctx);        break;
        case CMD_GET_CONFIG:        handle_get_config(ctx);        break;
        case CMD_SET_PARAM:         handle_set_param(ctx, cmd.args); break;
        case CMD_CALIBRATE:         handle_calibrate(ctx);         break;
        case CMD_RESET:             handle_reset(ctx);             break;
        case CMD_DEBUG_ON:          handle_debug_on(ctx);          break;
        case CMD_DEBUG_OFF:         handle_debug_off(ctx);         break;
        case CMD_SAVE_CONFIG:       handle_save_config(ctx);       break;
        case CMD_LOAD_CONFIG:       handle_load_config(ctx);       break;
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
    
    /* Инициализация системы с MAC адресом из device_config */
    uint8_t my_mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x00};
    system_init(&system_ctx, my_mac);
    
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
    net_receive_once();
    
    /* Небольшая задержка */
    for (volatile int i = 0; i < 1000; i++);
}

/* Callback для UART команд */
void uart_line_callback(const char* line, uint16_t len)
{
    (void)len;
    
    cmd_parse_result_t cmd = cmd_parse(line);
    process_command(&system_ctx, cmd);
    
    uart_puts("\r\n> ");
}
