# 建表参考（server.py 启动时会自动执行同样的建库建表，此文件仅供查看/手动备份用）
CREATE DATABASE IF NOT EXISTS plant_db DEFAULT CHARACTER SET utf8mb4;

USE plant_db;

CREATE TABLE IF NOT EXISTS sensor_data (
    id        INT AUTO_INCREMENT PRIMARY KEY,
    device_id VARCHAR(20) NOT NULL DEFAULT 'tas01',   -- tas01 / soil02 / soil03
    valid_mask TINYINT UNSIGNED NOT NULL DEFAULT 1 COMMENT 'bit0=air/light, bit1=soil',
    temp_x10  SMALLINT    NULL COMMENT '温度*10, 268=26.8C',
    humi_x10  SMALLINT    NULL COMMENT '湿度*10',
    lux       INT         NULL COMMENT '光照',
    soil_temp_x10 SMALLINT NULL COMMENT '土壤温度*10',
    soil_humi_x10 SMALLINT NULL COMMENT '土壤湿度*10',
    soil_ec       INT      NULL COMMENT '土壤电导率',
    soil_salinity INT      NULL COMMENT '土壤盐度',
    soil_n        INT      NULL COMMENT '土壤氮',
    soil_p        INT      NULL COMMENT '土壤磷',
    soil_k        INT      NULL COMMENT '土壤钾',
    soil_ph_x10   SMALLINT NULL COMMENT '土壤pH*10',
    ts        DATETIME    NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_ts (ts)
) DEFAULT CHARSET=utf8mb4;

-- ESP-CAM 网关照片表（server.py 启动时也会自动建，此文件仅供查看/手动备份用）
CREATE TABLE IF NOT EXISTS photo_records (
    id         INT AUTO_INCREMENT PRIMARY KEY,
    device_id  VARCHAR(20) NOT NULL DEFAULT 'cam1',   -- 设备ID
    image_path VARCHAR(255) NOT NULL COMMENT '相对路径 photos/xxx.jpg',
    temp_x10   SMALLINT    NOT NULL DEFAULT -1 COMMENT '拍照时读数快照*10',
    humi_x10   SMALLINT    NOT NULL DEFAULT -1,
    lux        INT         NOT NULL DEFAULT -1,
    ts         DATETIME    NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_ts (ts)
) DEFAULT CHARSET=utf8mb4;
