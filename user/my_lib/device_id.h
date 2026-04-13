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
	net_eui64_t eui64;          /* 64-битный EUI адрес */
	
	/* Function pointers for device behavior */
	void (*init_func)(void);
	void (*main_loop_func)(void);
} device_config_t;

/* Структура для регистрации устройства */
typedef struct {
	uint32_t part_id;
	device_config_t* dev;
	void (*init_func)(void);
	void (*loop_func)(void);
} device_registration_t;

/* Регистрация устройства с функциями */
int device_register(const device_registration_t* reg);

/*==============================================================================
 * Predefined Device Configurations
 *============================================================================*/

/* Глобальные объекты для каждого типа устройства */
extern device_config_t DEVICE_MAIN_ANCHOR;
extern device_config_t DEVICE_ANCHOR;
extern device_config_t DEVICE_TAG;

extern device_config_t* curr_dev;

/*==============================================================================
 * Device Identification
 *============================================================================*/

/**
 * Initialize current device from hardware (reads Part ID and matches predefined devices)
 */
device_config_t* device_init_from_hardware(void);

#endif /* DEVICE_ID_H */
