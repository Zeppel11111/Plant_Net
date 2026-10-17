#ifndef     SENSOR_DATA_H
#define     SENSOR_DATA_H

#include    <stdio.h>
#include    <stdint.h>
#include    <stdbool.h>
#include    "tastek.h"
#include    "soil_sensor.h"

typedef struct{
    tastek_data_t tas_data;
    soil_data_t soil_data_1;
    soil_data_t soil_data_2;
    bool light_valid;
    bool air_valid;
    bool soil_data_1_valid;
    bool soil_data_2_valid;
    // 每完成一次对应设备的读取就递增；队列覆盖后，消费者仍能识别哪些字段更新过。
    uint32_t light_revision;
    uint32_t air_revision;
    uint32_t soil_data_1_revision;
    uint32_t soil_data_2_revision;
}sensor_data_t;

typedef void (*sensor_data_update_fn_t)(const sensor_data_t *sensor_data, void *context);

// 按总线顺序采集；每完成一类数据的读取，就通知上层处理最新快照。
void Sensor_Data_get(sensor_data_t *sensor_data, sensor_data_update_fn_t on_update,
                     void *context);

#endif
