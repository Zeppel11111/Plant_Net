#include "Sensor_data.h"

#include <inttypes.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void Sensor_Data_get(sensor_data_t *sensor_data, sensor_data_update_fn_t on_update,
                     void *context)
{
    if (sensor_data == NULL)
    {
        return;
    }

    esp_err_t light_err;
    esp_err_t air_err;
    esp_err_t soil_1_err, soil_2_err;
    // 快照跨轮次保留最近一次有效值；每类数据的 valid 标记表示本次读取是否成功。

        //塔石数据读取
        light_err = tastek_read_light(&sensor_data->tas_data);
        sensor_data->light_valid = light_err == ESP_OK;
        sensor_data->light_revision++;
        if (light_err == ESP_OK)
        {
            ESP_LOGI("塔石", "光照: %" PRIu32 " lux", sensor_data->tas_data.light_data);
        }
        else
        {
            ESP_LOGE("塔石", "读取光照失败: %s", esp_err_to_name(light_err));
        }
        if (on_update != NULL)
        {
            on_update(sensor_data, context);
        }

        // 手册建议两次轮询至少间隔 200 ms。
        vTaskDelay(pdMS_TO_TICKS(250));

        air_err = tastek_read_air_temp_humidity(&sensor_data->tas_data);
        sensor_data->air_valid = air_err == ESP_OK;
        sensor_data->air_revision++;
        if (air_err == ESP_OK)
        {
            ESP_LOGI("塔石", "空气温度: %.1f C，空气湿度: %.1f %%RH",
                     sensor_data->tas_data.air_temp_data, sensor_data->tas_data.air_water_data);
        }
        else
        {
            ESP_LOGE("塔石", "读取空气温湿度失败: %s", esp_err_to_name(air_err));
        }
        if (on_update != NULL)
        {
            on_update(sensor_data, context);
        }

        vTaskDelay(pdMS_TO_TICKS(20));

        //开始土壤1采集 0x02
        soil_1_err = soil_read_all(SOIL_SENSOR_DEFAULT_ADDRESS, &sensor_data->soil_data_1);
        sensor_data->soil_data_1_valid = soil_1_err == ESP_OK;
        sensor_data->soil_data_1_revision++;
        if (soil_1_err == ESP_OK)
        {
            ESP_LOGI("土壤传感1", "采集成功：温度 %.1f °C，湿度 %.1f%%，EC %u µS/cm，盐分 %u ppm",
                     sensor_data->soil_data_1.temperature_c,
                     sensor_data->soil_data_1.moisture_percent,
                     (unsigned int)sensor_data->soil_data_1.ec_us_cm,
                     (unsigned int)sensor_data->soil_data_1.salt_ppm);
            ESP_LOGI("土壤传感1", "氮 %u mg/kg，磷 %u mg/kg，钾 %u mg/kg，pH %.1f",
                     (unsigned int)sensor_data->soil_data_1.nitrogen_mg_kg,
                     (unsigned int)sensor_data->soil_data_1.phosphorus_mg_kg,
                     (unsigned int)sensor_data->soil_data_1.potassium_mg_kg,
                     sensor_data->soil_data_1.ph);
        }
        else
        {
            ESP_LOGE("土壤传感1","采集失败");
        }
        if (on_update != NULL)
        {
            on_update(sensor_data, context);
        }

        //开始土壤2采集 0x03
        soil_2_err = soil_read_all(SOIL_SENSOR_SECOND_ADDRESS, &sensor_data->soil_data_2);
        sensor_data->soil_data_2_valid = soil_2_err == ESP_OK;
        sensor_data->soil_data_2_revision++;
        if (soil_2_err == ESP_OK)
        {
            ESP_LOGI("土壤传感2", "采集成功：温度 %.1f °C，湿度 %.1f%%，EC %u µS/cm，盐分 %u ppm",
                     sensor_data->soil_data_2.temperature_c,
                     sensor_data->soil_data_2.moisture_percent,
                     (unsigned int)sensor_data->soil_data_2.ec_us_cm,
                     (unsigned int)sensor_data->soil_data_2.salt_ppm);
            ESP_LOGI("土壤传感2", "氮 %u mg/kg，磷 %u mg/kg，钾 %u mg/kg，pH %.1f",
                     (unsigned int)sensor_data->soil_data_2.nitrogen_mg_kg,
                     (unsigned int)sensor_data->soil_data_2.phosphorus_mg_kg,
                     (unsigned int)sensor_data->soil_data_2.potassium_mg_kg,
                     sensor_data->soil_data_2.ph);
        }
        else
        {
            ESP_LOGE("土壤传感2", "采集失败：%s", esp_err_to_name(soil_2_err));
        }
        if (on_update != NULL)
        {
            on_update(sensor_data, context);
        }

}
