#ifndef CMD_PARSER_H
#define CMD_PARSER_H

#include <stdint.h>
#include <stddef.h>

/*==============================================================================
 * Command Codes
 *============================================================================*/

typedef enum {
    CMD_UNKNOWN = 0,
    CMD_INITIALIZE_SYSTEM,
    CMD_RECONFIGURE,
    CMD_START,
    CMD_STOP,
    CMD_GET_STATUS,
    CMD_GET_CONFIG,
    CMD_SET_PARAM,
    CMD_CALIBRATE,
    CMD_RESET,
    CMD_DEBUG_ON,
    CMD_DEBUG_OFF,
    CMD_SAVE_CONFIG,
    CMD_LOAD_CONFIG,
    CMD_COUNT
} cmd_code_t;

/*==============================================================================
 * Parse Result
 *============================================================================*/

typedef struct {
    cmd_code_t code;
    char* args;
    size_t args_len;
    uint8_t valid;
} cmd_parse_result_t;

/*==============================================================================
 * Public Functions
 *============================================================================*/

cmd_parse_result_t cmd_parse(const char* buffer);
const char* cmd_to_string(cmd_code_t cmd);

#endif /* CMD_PARSER_H */
