#include "device_id.h"
#include "deca_device_api.h"
#include <string.h>

/*==============================================================================
 * Predefined Device Configurations
 *============================================================================*/

/* Функции по умолчанию */
static void default_init(device_config_t* dev) { (void)dev; }
static void default_loop(device_config_t* dev) { (void)dev; }

/* Предопределенные устройства */
static device_config_t dev_main_anchor = {
    .part_id = 0,
    .type = DEVICE_TYPE_MAIN_ANCHOR,
    .short_addr = 0x0001,
    // .eui64 = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    .eui64 = NULL,
    .init_func = default_init,
    .main_loop_func = default_loop,
    .net_ctx = NULL
};

static device_config_t dev_anchor = {
    .part_id = 0,
    .type = DEVICE_TYPE_ANCHOR,
    .short_addr = 0x0002,
    // .eui64 = {0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    .eui64 = NULL,
    .init_func = default_init,
    .main_loop_func = default_loop,
    .net_ctx = NULL
};

static device_config_t dev_tag = {
    .part_id = 0,
    .type = DEVICE_TYPE_TAG,
    .short_addr = 0x0003,
    // .eui64 = {0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    .eui64 = NULL,
    .init_func = default_init,
    .main_loop_func = default_loop,
    .net_ctx = NULL
};

/* Глобальные объекты */
device_config_t DEVICE_MAIN_ANCHOR = dev_main_anchor;
device_config_t DEVICE_ANCHOR = dev_anchor;
device_config_t DEVICE_TAG = dev_tag;

/*==============================================================================
 * Internal Data
 *============================================================================*/

static device_config_t* current_device = NULL;

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

/* Изменить device_init_from_hardware */
device_config_t* device_init_from_hardware(void)
{
    uint32_t part_id = dwt_getpartid();
    
    for (uint8_t i = 0; i < mapping_count; i++) {
        if (device_mappings[i].part_id == part_id) {
            device_set_current(device_mappings[i].dev);
            return current_device;
        }
    }
    
    return NULL;
}

#define MAPPING_COUNT (sizeof(device_mapping) / sizeof(device_mapping[0]))

/*==============================================================================
 * Serialization
 *============================================================================*/

int device_serialize(const device_config_t* dev, uint8_t* buffer, uint16_t buffer_size)
{
    if (!dev || !buffer || buffer_size < 32) {
        return -1;
    }
    
    uint16_t offset = 0;
    
    /* part_id (4 bytes) */
    buffer[offset++] = (dev->part_id >> 0) & 0xFF;
    buffer[offset++] = (dev->part_id >> 8) & 0xFF;
    buffer[offset++] = (dev->part_id >> 16) & 0xFF;
    buffer[offset++] = (dev->part_id >> 24) & 0xFF;
    
    /* type (1 byte) */
    buffer[offset++] = (uint8_t)dev->type;
    
    /* short_addr (2 bytes) */
    buffer[offset++] = (dev->short_addr >> 0) & 0xFF;
    buffer[offset++] = (dev->short_addr >> 8) & 0xFF;
    
    /* eui64 (8 bytes) */
    for (int i = 0; i < 8; i++) {
        if (dev->eui64)
            buffer[offset++] = dev->eui64.bytes[i];
    }
    
    return offset;
}

int device_deserialize(const uint8_t* buffer, uint16_t buffer_size, device_config_t* dev)
{
    uint16_t offset = 0;

    if (!buffer || !dev || buffer_size < 32) {
        return -1;
    }
    
    /* part_id */
    dev->part_id = buffer[offset++] |
                   (buffer[offset++] << 8) |
                   (buffer[offset++] << 16) |
                   (buffer[offset++] << 24);
    
    /* type */
    dev->type = (device_type_t)buffer[offset++];
    
    /* short_addr */
    dev->short_addr = buffer[offset++] | (buffer[offset++] << 8);
    
    /* eui64 */
    for (int i = 0; i < 8; i++) {
        dev->eui64.bytes[i] = buffer[offset++];
    }
    
    /* Устанавливаем функции по умолчанию */
    dev->init_func = default_init;
    dev->main_loop_func = default_loop;
    dev->net_ctx = NULL;
    
    return 0;
}

/*==============================================================================
 * Device Identification
 *============================================================================*/

device_config_t* device_get_current(void)
{
    return current_device;
}

void device_set_current(device_config_t* dev)
{
    current_device = dev;
}

device_config_t* device_init_from_hardware(void)
{
    uint32_t part_id = dwt_getpartid();
    
    /* Ищем в таблице маппинга */
    for (uint8_t i = 0; i < MAPPING_COUNT; i++) {
        if (device_mapping[i].part_id == part_id) {
            device_set_current(device_mapping[i].dev);
            return current_device;
        }
    }
    
    /* Если не нашли - возвращаем NULL */
    return NULL;
}
