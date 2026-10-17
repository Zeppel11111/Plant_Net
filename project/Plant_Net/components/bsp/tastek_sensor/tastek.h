#ifndef     TASTEK_H
#define     TASTEK_H

#include <stdint.h>
#include "esp_err.h"

typedef struct{

    uint32_t light_data;      // 单位：lux
    float air_temp_data;      // 单位：摄氏度，允许负温度
    float air_water_data;     // 空气湿度，单位：%RH

}tastek_data_t;

esp_err_t tastek_read_light(tastek_data_t *data);
esp_err_t tastek_read_air_temp_humidity(tastek_data_t *data);

#endif
