#ifndef     SOIL_SENSOR_H
#define     SOIL_SENSOR_H

#include <stdint.h>
#include "esp_err.h"

#define SOIL_SENSOR_DEFAULT_ADDRESS 0x02
#define SOIL_SENSOR_SECOND_ADDRESS  0x03

typedef struct {
    float temperature_c;       // 土壤温度，摄氏度
    float moisture_percent;    // 土壤湿度，百分比
    uint16_t ec_us_cm;          // 电导率，uS/cm
    uint16_t salt_ppm;          // 盐分，ppm
    uint16_t nitrogen_mg_kg;    // 氮含量，mg/kg
    uint16_t phosphorus_mg_kg;  // 磷含量，mg/kg
    uint16_t potassium_mg_kg;   // 钾含量，mg/kg
    float ph;                   // pH 值
} soil_data_t;

// 读取指定 Modbus 从站的全部 8 项参数。两只传感器必须配置为不同地址。
esp_err_t soil_read_all(uint8_t slave_address, soil_data_t *data);

#endif
