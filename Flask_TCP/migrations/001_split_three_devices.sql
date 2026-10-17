-- 将旧版 dev01 混合记录拆成塔石 tas01 与土壤 soil02；soil03 保持不变。
-- 运行前停止 Flask 服务并备份 sensor_data。本脚本只处理 device_id='dev01'，重复执行不会再次拆分。
USE plant_db;

START TRANSACTION;

-- 同一旧行里同时有塔石与土壤数据时，先保留土壤部分为新行。
INSERT INTO sensor_data
    (device_id, valid_mask, temp_x10, humi_x10, lux,
     soil_temp_x10, soil_humi_x10, soil_ec, soil_salinity,
     soil_n, soil_p, soil_k, soil_ph_x10, ts)
SELECT 'soil02', 2, NULL, NULL, NULL,
       soil_temp_x10, soil_humi_x10, soil_ec, soil_salinity,
       soil_n, soil_p, soil_k, soil_ph_x10, ts
FROM sensor_data
WHERE device_id = 'dev01'
  AND (temp_x10 IS NOT NULL OR humi_x10 IS NOT NULL OR lux IS NOT NULL)
  AND (soil_temp_x10 IS NOT NULL OR soil_humi_x10 IS NOT NULL
       OR soil_ec IS NOT NULL OR soil_salinity IS NOT NULL
       OR soil_n IS NOT NULL OR soil_p IS NOT NULL
       OR soil_k IS NOT NULL OR soil_ph_x10 IS NOT NULL);

-- 旧行只有土壤数据时直接改名，不产生额外记录。
UPDATE sensor_data
SET device_id = 'soil02', valid_mask = 2
WHERE device_id = 'dev01'
  AND temp_x10 IS NULL AND humi_x10 IS NULL AND lux IS NULL
  AND (soil_temp_x10 IS NOT NULL OR soil_humi_x10 IS NOT NULL
       OR soil_ec IS NOT NULL OR soil_salinity IS NOT NULL
       OR soil_n IS NOT NULL OR soil_p IS NOT NULL
       OR soil_k IS NOT NULL OR soil_ph_x10 IS NOT NULL);

-- 原混合行留下塔石部分；无有效值的旧行标为无效。
UPDATE sensor_data
SET device_id = 'tas01',
    valid_mask = IF(temp_x10 IS NOT NULL OR humi_x10 IS NOT NULL
                    OR lux IS NOT NULL, 1, 0),
    soil_temp_x10 = NULL, soil_humi_x10 = NULL,
    soil_ec = NULL, soil_salinity = NULL,
    soil_n = NULL, soil_p = NULL, soil_k = NULL, soil_ph_x10 = NULL
WHERE device_id = 'dev01';

COMMIT;

-- 新记录始终显式传入 device_id；默认值仅用于手工插入时避免旧 dev01。
ALTER TABLE sensor_data ALTER COLUMN device_id SET DEFAULT 'tas01';
