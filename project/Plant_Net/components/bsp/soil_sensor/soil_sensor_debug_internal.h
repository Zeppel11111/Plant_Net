#ifndef SOIL_SENSOR_DEBUG_INTERNAL_H
#define SOIL_SENSOR_DEBUG_INTERNAL_H

#include "esp_err.h"
#include <stdint.h>

// 仅由 TJC 开发调试接口调用，不用于正常采集流程。
esp_err_t soil_debug_read_holding_register(uint8_t slave_address,
                                           uint16_t register_address,
                                           uint16_t *value);
esp_err_t soil_debug_write_address_register(uint8_t current_address,
                                            uint8_t new_address);

#endif
