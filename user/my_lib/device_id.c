#include "device_id.h"
#include "deca_device_api.h"
#include <string.h>

/*==============================================================================
 * Predefined Device Configurations
 *============================================================================*/

/* Функции по умолчанию */
static void default_init(void) { ; }
static void default_loop(void) { ; }

/* Предопределенные устройства */
device_config_t DEVICE_MAIN_ANCHOR = {
	.part_id = 0,
	.type = DEVICE_TYPE_MAIN_ANCHOR,
	.eui64 = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
	.init_func = default_init,
	.main_loop_func = default_loop,
};

device_config_t DEVICE_ANCHOR = {
	.part_id = 0,
	.type = DEVICE_TYPE_ANCHOR,
	.eui64 = {0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
	.init_func = default_init,
	.main_loop_func = default_loop,
};

device_config_t DEVICE_TAG = {
	.part_id = 0,
	.type = DEVICE_TYPE_TAG,
	.eui64 = {0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
	.init_func = default_init,
	.main_loop_func = default_loop,
};

/*==============================================================================
 * Internal Data
 *============================================================================*/

device_config_t* curr_dev = NULL;

/* Таблица маппинга Part ID -> тип устройства */
#define MAX_MAPPINGS 16

static struct {
	uint32_t part_id;
	device_config_t* dev;
} device_mappings[MAX_MAPPINGS];

static uint8_t mapping_count = 0;

int device_register(const device_registration_t* reg)
{
	if (mapping_count >= MAX_MAPPINGS) {
		return -1;
	}
	
	/* Устанавливаем функции */
	reg->dev->init_func = reg->init_func;
	reg->dev->main_loop_func = reg->loop_func;
	
	/* Добавляем маппинг */
	device_mappings[mapping_count].part_id = reg->part_id;
	device_mappings[mapping_count].dev = reg->dev;
	mapping_count++;
	
	return 0;
}

device_config_t* device_init_from_hardware(void)
{
	uint32_t part_id = dwt_getpartid();
	
	for (uint8_t i = 0; i < mapping_count; i++) {
		if (device_mappings[i].part_id == part_id) {
			curr_dev = device_mappings[i].dev;
			curr_dev->part_id = part_id;
			return curr_dev;
		}
	}
	
	return NULL;
}

#define MAPPING_COUNT (sizeof(device_mapping) / sizeof(device_mapping[0]))

/*==============================================================================
 * Device Identification
 *============================================================================*/

static void device_set_current(device_config_t* dev, uint32_t part_id)
{
	curr_dev = dev;
	curr_dev->part_id = part_id;
}
