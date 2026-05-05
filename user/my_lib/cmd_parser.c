#include "cmd_parser.h"
#include <string.h>

#define TO_UPPER(c) (((c) >= 'a' && (c) <= 'z') ? (c) - 32 : (c))

/*==============================================================================
 * Command Table
 *============================================================================*/

typedef struct {
	const char* str;
	uint8_t len;
	cmd_code_t code;
} cmd_map_t;

#define CMD_STR(cmd) (cmd_table[cmd].str)
#define CMD_LEN(cmd) (cmd_table[cmd].len)

static const cmd_map_t cmd_table[] = {
	/* First command is UNKNOWN */
	{"UNKNOWN",         7,  CMD_UNKNOWN},

	/* System commands */
	{"INITIALIZE",      10, CMD_INITIALIZE},
	{"RECONFIGURE",     11, CMD_RECONFIGURE},
	{"START",           5,  CMD_START},
	{"STOP",            4,  CMD_STOP},
	{"RESET",           5,  CMD_RESET},

	/* Status commands */
	{"GET_STATUS",      10, CMD_GET_STATUS},
	{"GET_CONFIG",      10, CMD_GET_CONFIG},

	/* Debug commands */
	{"DEBUG_ON",        8,  CMD_DEBUG_ON},
	{"DEBUG_OFF",       9,  CMD_DEBUG_OFF},

	/* Config commands */
	{"SET_PARAM",       9,  CMD_SET_PARAM},
	{"CALIBRATE",       9,  CMD_CALIBRATE},

	/* Anchor specific commands */
	{"CONFIG_START",    12, CMD_CONFIG_START},
	{"CONFIG_STOP",     11, CMD_CONFIG_STOP},
	{"RANGING_START",   13, CMD_RANGING_START},
	{"RANGING_STOP",    12, CMD_RANGING_STOP},

	/* Enumeration commands */
	{"DSCVR",           5,  CMD_DISCOVER},
	{"SYNC_LIST",       9,  CMD_SYNC_LIST},
	{"OK",              2,  CMD_OK},
	{"ERR",             3,  CMD_ERR},
	{"TEST_SS_TWR",     11, CMD_TEST_SS_TWR},
	{"LOC_POS_SYS",    11, CMD_PING},
};

#define CMD_TABLE_SIZE (sizeof(cmd_table) / sizeof(cmd_map_t))

/*==============================================================================
 * Internal Data
 *============================================================================*/

static char cmd_result_args[128];

/*==============================================================================
 * Internal Functions
 *============================================================================*/

static void str_to_upper(char* dest, const char* src, size_t max_len)
{
	size_t i = 0;
	while (src[i] && i < max_len - 1) {
		dest[i] = TO_UPPER(src[i]);
		i++;
	}
	dest[i] = '\0';
}

static void trim_newline(char* str)
{
	size_t len = strlen(str);
	while (len > 0 && (str[len-1] == '\r' || str[len-1] == '\n')) {
		str[len-1] = '\0';
		len--;
	}
}

/*==============================================================================
 * Public Functions
 *============================================================================*/

cmd_parse_result_t cmd_parse(const char* buffer)
{
	cmd_parse_result_t result;
	result.code = CMD_UNKNOWN;
	result.args = NULL;
	result.valid = 0;
	
	if (!buffer || buffer[0] == '\0') {
		return result;
	}
	
	char cmd_buffer[128];
	size_t buffer_len = strlen(buffer);
	
	if (buffer_len >= sizeof(cmd_buffer)) {
		return result;
	}
	
	/* Copy buffer for processing */
	strcpy(cmd_buffer, buffer);
	trim_newline(cmd_buffer);
	buffer_len = strlen(cmd_buffer);
	
	if (buffer_len == 0) {
		return result;
	}
	
	/* Find separator between command and arguments (space or tab) */
	size_t arg_start_idx = 0;
	size_t cmd_len = 0;
	
	for (size_t i = 0; i < buffer_len; i++) {
		if (cmd_buffer[i] == ' ' || cmd_buffer[i] == '\t') {
			cmd_len = i;
			arg_start_idx = i;
			break;
		}
	}
	
	if (cmd_len == 0) {
		cmd_len = buffer_len;
	}
	
	/* Extract command and convert to uppercase */
	char cmd_upper[64];
	size_t copy_len = (cmd_len < sizeof(cmd_upper) - 1) ? cmd_len : sizeof(cmd_upper) - 1;
	str_to_upper(cmd_upper, cmd_buffer, copy_len + 1);
	cmd_upper[copy_len] = '\0';
	
	/* Look up command in table */
	for (size_t i = 0; i < CMD_TABLE_SIZE; i++) {
		if (strcmp(cmd_upper, cmd_table[i].str) == 0) {
			result.code = cmd_table[i].code;
			result.valid = 1;
			break;
		}
	}
	
	/* Store arguments if any — copy to static buffer to avoid dangling pointer */
	if (arg_start_idx > 0 && result.valid) {
		const char* src_args = cmd_buffer + arg_start_idx;
		while (*src_args == ' ' || *src_args == '\t') src_args++;
		if (*src_args != '\0') {
			strncpy(cmd_result_args, src_args, sizeof(cmd_result_args) - 1);
			cmd_result_args[sizeof(cmd_result_args) - 1] = '\0';
			result.args = cmd_result_args;
		}
	}
	
	return result;
}

const char* cmd_str(cmd_code_t cmd)
{
	for (size_t i = 0; i < CMD_TABLE_SIZE; i++) {
		if (cmd_table[i].code == cmd) {
			return cmd_table[i].str;
		}
	}
	return cmd_table[CMD_UNKNOWN].str;
}

uint8_t cmd_len(cmd_code_t cmd)
{
	for (size_t i = 0; i < CMD_TABLE_SIZE; i++) {
		if (cmd_table[i].code == cmd)
			return cmd_table[i].len;
	}
	return cmd_table[CMD_UNKNOWN].len;
}

uint8_t cmd_size(cmd_code_t cmd)
{
	return cmd_len(cmd) + 1; /* includes null terminator */
}
