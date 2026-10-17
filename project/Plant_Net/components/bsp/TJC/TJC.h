#ifndef     TJC_H
#define     TJC_H

#include "esp_err.h"
#include <stdint.h>

// 数值统一经此接口发送；decimals=1 显示一位小数，decimals=0 显示整数。
esp_err_t TJC_send(const char *cmd, double data, uint8_t decimals);

// 开发人员调试接口：仅用于土壤传感器地址查询和一次性改址，不接入正常采集流程。
// 查询时 probe_address 必须是传感器当前已知地址；Modbus 不支持无地址主动发现。
esp_err_t TJC_debug_soil_read_address(uint8_t probe_address, uint8_t *configured_address);
// current_address 是改址前的地址；总线上有多只传感器时，请先单独连接目标传感器。
esp_err_t TJC_debug_soil_write_address(uint8_t current_address, uint8_t new_address);

#endif
