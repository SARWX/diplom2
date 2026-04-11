#ifndef CMD_PARSER_H
#define CMD_PARSER_H

#include <stdint.h>
#include <stddef.h>

/*==============================================================================
 * Command Codes
 *============================================================================*/

typedef enum {
    CMD_UNKNOWN = 0,
    
    /* System commands */
    CMD_INITIALIZE,
    CMD_RECONFIGURE,
    CMD_START,
    CMD_STOP,
    CMD_RESET,
    
    /* Status commands */
    CMD_GET_STATUS,
    CMD_GET_CONFIG,
    
    /* Debug commands */
    CMD_DEBUG_ON,
    CMD_DEBUG_OFF,
    
    /* Config commands */
    CMD_SET_PARAM,
    CMD_SAVE_CONFIG,
    CMD_LOAD_CONFIG,
    CMD_CALIBRATE,
    
    /* Anchor specific commands */
    CMD_DISCOVER,
    CMD_CONFIG_START,
    CMD_CONFIG_STOP,
    CMD_RANGING_START,
    CMD_RANGING_STOP,
    
    CMD_COUNT
} cmd_code_t;

/*==============================================================================
 * Parse Result
 *============================================================================*/

typedef struct {
    cmd_code_t code;
    char* args;           /* Arguments after command (if any) */
    size_t args_len;      /* Length of arguments */
    uint8_t valid;        /* 1 if command recognized */
} cmd_parse_result_t;

/*==============================================================================
 * Public Functions
 *============================================================================*/

cmd_parse_result_t cmd_parse(const char* buffer);
const char* cmd_to_string(cmd_code_t cmd);

#endif /* CMD_PARSER_H */
