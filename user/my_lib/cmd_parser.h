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
	CMD_CALIBRATE,
	
	/* Anchor specific commands */
	CMD_CONFIG_START,
	CMD_CONFIG_STOP,
	CMD_RANGING_START,
	CMD_RANGING_STOP,

	/* Enumeration commands */
        CMD_DISCOVER,
	CMD_SYNC_LIST,
	CMD_OK,
	CMD_ERR,

        /* must be last command */
	CMD_COUNT
} cmd_code_t;

/*==============================================================================
 * Parse Result
 *============================================================================*/

typedef struct {
	cmd_code_t code;
	char* args;           /* Arguments after command (if any) */
	uint8_t valid;        /* 1 if command recognized */
} cmd_parse_result_t;

/*==============================================================================
 * Public Functions
 *============================================================================*/

cmd_parse_result_t cmd_parse(const char* buffer);
const char* cmd_str(cmd_code_t cmd);
uint8_t cmd_len(cmd_code_t cmd);
uint8_t cmd_size(cmd_code_t cmd);

#endif /* CMD_PARSER_H */
