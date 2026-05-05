#ifndef CMD_PARSER_H
#define CMD_PARSER_H

#include <stdint.h>
#include <stddef.h>

/*==============================================================================
 * Command Codes
 *============================================================================*/

/** @brief All recognized command codes exchanged over-the-air and via UART. */
typedef enum {
	CMD_UNKNOWN = 0,    /**< Unrecognized or empty command */
	CMD_INITIALIZE,     /**< Trigger full system initialization (enumeration + config) */
	CMD_RECONFIGURE,    /**< Re-run configuration phase without re-enumeration */
	CMD_START,          /**< Start ranging */
	CMD_STOP,           /**< Stop ranging */
	CMD_RESET,          /**< Reset system state */
	CMD_GET_STATUS,     /**< Query current status */
	CMD_GET_CONFIG,     /**< Query current configuration */
	CMD_DEBUG_ON,       /**< Enable debug output */
	CMD_DEBUG_OFF,      /**< Disable debug output */
	CMD_SET_PARAM,      /**< Set a named parameter */
	CMD_CALIBRATE,      /**< Run calibration procedure */
	CMD_CONFIG_START,   /**< Instruct an anchor to begin measuring distances */
	CMD_CONFIG_STOP,    /**< Instruct an anchor to finish configuration phase */
	CMD_RANGING_START,  /**< Start continuous ranging on an anchor */
	CMD_RANGING_STOP,   /**< Stop continuous ranging on an anchor */
	CMD_DISCOVER,       /**< Broadcast from master to discover all nodes */
	CMD_SYNC_LIST,      /**< Master sends the compiled device list to all nodes */
	CMD_OK,             /**< Positive acknowledgement */
	CMD_ERR,            /**< Negative acknowledgement / error */
	CMD_TEST_SS_TWR,    /**< UART: run SS-TWR test to all devices and print distances */
	CMD_PING,           /**< Handshake: host sends LOC_POS_SYS, device replies LPS */
	CMD_COUNT           /**< Sentinel — must remain last */
} cmd_code_t;

/*==============================================================================
 * Parse Result
 *============================================================================*/

/** @brief Result of parsing a command string. */
typedef struct {
	cmd_code_t code;  /**< Parsed command code, CMD_UNKNOWN if not recognized */
	char* args;       /**< Pointer to argument substring (after command keyword), or NULL */
	uint8_t valid;    /**< Non-zero if the command was recognized */
} cmd_parse_result_t;

/*==============================================================================
 * Public Functions
 *============================================================================*/

cmd_parse_result_t cmd_parse(const char* buffer);
const char* cmd_str(cmd_code_t cmd);
uint8_t cmd_len(cmd_code_t cmd);
uint8_t cmd_size(cmd_code_t cmd);

#endif /* CMD_PARSER_H */
