#ifndef DEVICE_ID_H
#define DEVICE_ID_H

#include <stdint.h>
#include "net_mac.h"

/*==============================================================================
 * Device Types
 *============================================================================*/

typedef enum {
    DEVICE_TYPE_NONE = 0,
    DEVICE_TYPE_MAIN_ANCHOR,    /* Главная базовая станция */
    DEVICE_TYPE_ANCHOR,          /* Базовая станция */
    DEVICE_TYPE_TAG              /* Устройство-метка */
} device_type_t;

/*==============================================================================
 * Device Configuration Structure
 *============================================================================*/

typedef struct device_config {
    uint32_t part_id;           /* DW1000 Part ID (уникальный идентификатор чипа) */
    device_type_t type;         /* Тип устройства */
    net_addr16_t short_addr;    /* 16-битный короткий адрес */
    net_eui64_t eui64;          /* 64-битный EUI адрес */
    
    /* Function pointers for device behavior */
    void (*init_func)(struct device_config* dev);
    void (*main_loop_func)(struct device_config* dev);
    
    /* Network context */
    void* net_ctx;
} device_config_t;

/*==============================================================================
 * Predefined Device Configurations
 *============================================================================*/

/* Глобальные объекты для каждого типа устройства */
extern device_config_t DEVICE_MAIN_ANCHOR;
extern device_config_t DEVICE_ANCHOR;
extern device_config_t DEVICE_TAG;

/*==============================================================================
 * Serialization
 *============================================================================*/

/**
 * Serialize device configuration to buffer
 * @param dev - device configuration
 * @param buffer - output buffer (must be at least 32 bytes)
 * @param buffer_size - size of buffer
 * @return number of bytes written, or -1 on error
 */
int device_serialize(const device_config_t* dev, uint8_t* buffer, uint16_t buffer_size);

/**
 * Deserialize device configuration from buffer
 * @param buffer - input buffer
 * @param buffer_size - size of buffer
 * @param dev - output device configuration
 * @return 0 on success, -1 on error
 */
int device_deserialize(const uint8_t* buffer, uint16_t buffer_size, device_config_t* dev);

/*==============================================================================
 * Device Identification
 *============================================================================*/

/**
 * Get current device (local device) based on Part ID from hardware
 * @return device configuration or NULL if not found
 */
device_config_t* device_get_current(void);

/**
 * Set current device
 */
void device_set_current(device_config_t* dev);

/**
 * Initialize current device from hardware (reads Part ID and matches predefined devices)
 */
device_config_t* device_init_from_hardware(void);

#endif /* DEVICE_ID_H */
