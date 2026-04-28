#ifndef DEVICE_ID_H
#define DEVICE_ID_H

#include <stdint.h>
#include "net_mac.h"

/*==============================================================================
 * Device Types
 *============================================================================*/

/** @brief Type of a network node. */
typedef enum {
	DEVICE_TYPE_NONE        = 0, /**< Unknown / unregistered device */
	DEVICE_TYPE_MAIN_ANCHOR,     /**< Master anchor station (with UART host interface) */
	DEVICE_TYPE_ANCHOR,          /**< Regular anchor (participates in ranging) */
	DEVICE_TYPE_TAG              /**< Tag (mobile node being located) */
} device_type_t;

/*==============================================================================
 * Device Configuration Structure
 *============================================================================*/

/**
 * @brief Runtime configuration of a specific device instance.
 *
 * Populated at startup by device_init_from_hardware() and used throughout
 * the system as the single source of truth for the current node's identity.
 */
typedef struct device_config {
	uint32_t      part_id;        /**< DW1000 Part ID read from OTP (unique per chip) */
	device_type_t type;           /**< Logical device type */
	net_eui64_t   eui64;          /**< EUI-64 address assigned to this device */
	void (*init_func)(void);      /**< Device-specific initialization entry point */
	void (*main_loop_func)(void); /**< Device-specific main loop entry point */
} device_config_t;

/**
 * @brief Static registration record that maps a Part ID to a device config and callbacks.
 *
 * Defined in main.c and passed to device_register() at startup.
 */
typedef struct {
	uint32_t       part_id;  /**< DW1000 Part ID this record matches */
	device_config_t* dev;    /**< Pointer to the corresponding device_config_t */
	void (*init_func)(void); /**< Initialization function for this device type */
	void (*loop_func)(void); /**< Main loop function for this device type */
} device_registration_t;

int device_register(const device_registration_t* reg);

/*==============================================================================
 * Predefined Device Configurations
 *============================================================================*/

extern device_config_t DEVICE_MAIN_ANCHOR; /**< Predefined config for the master anchor */
extern device_config_t DEVICE_ANCHOR;      /**< Predefined config for a regular anchor */
extern device_config_t DEVICE_TAG;         /**< Predefined config for a tag */

/** @brief Pointer to the active device config for the currently running node. */
extern device_config_t* curr_dev;

/*==============================================================================
 * Device Identification
 *============================================================================*/

/**
 * Initialize current device from hardware (reads Part ID and matches predefined devices)
 */
device_config_t* device_init_from_hardware(void);

#endif /* DEVICE_ID_H */
