"""MySQL 存储：建库、迁移及传感器记录读写；连接用完即关闭。"""

import pymysql
from .config import MYSQL_HOST, MYSQL_USER, MYSQL_PASSWORD, MYSQL_DB
from .telemetry import TelemetryRecord
from .device_ids import AIR_DEVICE_ID, SOIL1_DEVICE_ID, SOIL2_DEVICE_ID


SENSOR_DATA_CREATE_SQL = """
    CREATE TABLE IF NOT EXISTS sensor_data (
        id        INT AUTO_INCREMENT PRIMARY KEY,
        device_id VARCHAR(20) NOT NULL DEFAULT 'tas01',
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
    ) DEFAULT CHARSET=utf8mb4
"""


SENSOR_DATA_ADDITIONAL_COLUMNS = {
    "valid_mask": "TINYINT UNSIGNED NOT NULL DEFAULT 1 COMMENT 'bit0=air/light, bit1=soil'",
    "soil_temp_x10": "SMALLINT NULL COMMENT '土壤温度*10'",
    "soil_humi_x10": "SMALLINT NULL COMMENT '土壤湿度*10'",
    "soil_ec": "INT NULL COMMENT '土壤电导率'",
    "soil_salinity": "INT NULL COMMENT '土壤盐度'",
    "soil_n": "INT NULL COMMENT '土壤氮'",
    "soil_p": "INT NULL COMMENT '土壤磷'",
    "soil_k": "INT NULL COMMENT '土壤钾'",
    "soil_ph_x10": "SMALLINT NULL COMMENT '土壤pH*10'",
}


SENSOR_DATA_NULLABLE_AIR_COLUMNS = {
    "temp_x10": "SMALLINT NULL COMMENT '温度*10, 268=26.8C'",
    "humi_x10": "SMALLINT NULL COMMENT '湿度*10'",
    "lux": "INT NULL COMMENT '光照'",
}


SENSOR_INSERT_SQL = (
    "INSERT INTO sensor_data "
    "(device_id, valid_mask, temp_x10, humi_x10, lux, "
    "soil_temp_x10, soil_humi_x10, soil_ec, soil_salinity, "
    "soil_n, soil_p, soil_k, soil_ph_x10) "
    "VALUES (%s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s)"
)


def _record_values(record):
    return (record.device_id, record.valid_mask, record.temp_x10,
            record.humi_x10, record.lux, record.soil_temp_x10,
            record.soil_humi_x10, record.soil_ec, record.soil_salinity,
            record.soil_n, record.soil_p, record.soil_k, record.soil_ph_x10)


def save_records(records):
    """将一份 MQTT 快照的有效设备记录放进同一个事务。"""
    if not records:
        return
    conn = get_conn()
    try:
        with conn.cursor() as cur:
            cur.executemany(SENSOR_INSERT_SQL,
                            [_record_values(record) for record in records])
        conn.commit()
    except Exception:
        conn.rollback()
        raise
    finally:
        conn.close()


def get_conn():
    """连上 MySQL 返回连接对象。每次调用都是新连接，用完要关（防止多线程抢用）。"""
    return pymysql.connect(host=MYSQL_HOST, user=MYSQL_USER,
                           password=MYSQL_PASSWORD,
                           charset="utf8mb4", database=MYSQL_DB)


def init_db():
    """第一次启动自动建库建表。表已存在时不会报错（IF NOT EXISTS）。"""
    if not MYSQL_PASSWORD or MYSQL_PASSWORD == "YOUR_MYSQL_PASSWORD":
        raise RuntimeError(
            "未配置 MySQL 密码：复制 local_config.example.py 为 local_config.py 并填写密码，"
            "或设置环境变量 PLANT_NET_MYSQL_PASSWORD"
        )
    # 第一步：先连一个"没指定库"的连接，用来创建数据库本身
    conn = pymysql.connect(host=MYSQL_HOST, user=MYSQL_USER,
                           password=MYSQL_PASSWORD, charset="utf8mb4")
    try:
        with conn.cursor() as cur:
            cur.execute("CREATE DATABASE IF NOT EXISTS %s "
                        "DEFAULT CHARACTER SET utf8mb4" % MYSQL_DB)
        conn.commit()
    finally:
        conn.close()

    # 第二步：连到目标库，创建数据表
    conn = get_conn()
    try:
        with conn.cursor() as cur:
            cur.execute(SENSOR_DATA_CREATE_SQL)
            cur.execute(
                "SELECT COLUMN_NAME, IS_NULLABLE FROM INFORMATION_SCHEMA.COLUMNS "
                "WHERE TABLE_SCHEMA=%s AND TABLE_NAME=%s",
                (MYSQL_DB, "sensor_data"))
            existing_columns = {row[0]: row[1] for row in cur.fetchall()}
            for name, definition in SENSOR_DATA_ADDITIONAL_COLUMNS.items():
                if name not in existing_columns:
                    cur.execute("ALTER TABLE sensor_data ADD COLUMN %s %s" %
                                (name, definition))
            for name, definition in SENSOR_DATA_NULLABLE_AIR_COLUMNS.items():
                if existing_columns.get(name) == "NO":
                    cur.execute("ALTER TABLE sensor_data MODIFY COLUMN %s %s" %
                                (name, definition))
        conn.commit()
    finally:
        conn.close()

    # 第三步：照片记录表（ESP-CAM 网关：图片落盘 photos/，库中只存路径+读数快照）
    conn = get_conn()
    try:
        with conn.cursor() as cur:
            cur.execute("""
                CREATE TABLE IF NOT EXISTS photo_records (
                    id         INT AUTO_INCREMENT PRIMARY KEY,
                    device_id  VARCHAR(20) NOT NULL DEFAULT 'cam1',
                    image_path VARCHAR(255) NOT NULL COMMENT '相对路径 photos/xxx.jpg',
                    temp_x10   SMALLINT    NOT NULL DEFAULT -1 COMMENT '拍照时读数快照',
                    humi_x10   SMALLINT    NOT NULL DEFAULT -1,
                    lux        INT         NOT NULL DEFAULT -1,
                    ts         DATETIME    NOT NULL DEFAULT CURRENT_TIMESTAMP,
                    INDEX idx_ts (ts)
                ) DEFAULT CHARSET=utf8mb4
            """)
        conn.commit()
    finally:
        conn.close()
    print("[OK] 数据库已就绪:", MYSQL_DB)


def save_row(record, temp_x10=None, humi_x10=None, lux=None):
    """把一行数据插进数据库。出错只打印原因，绝不让服务器崩溃。"""
    # 兼容 ESP-CAM 的旧调用形式：save_row(dev, t, h, lux)。
    if isinstance(record, str):
        record = TelemetryRecord(record, 1, temp_x10, humi_x10, lux,
                                 None, None, None, None, None, None, None, None)
    conn = get_conn()
    try:
        with conn.cursor() as cur:
            cur.execute(SENSOR_INSERT_SQL, _record_values(record))
        conn.commit()
    except Exception as e:
        print("[错误] 存库失败:", e)
    finally:
        conn.close()


def fetch_recent(limit):
    """三个传感设备分别取最近记录，并保留每个设备最后一次有效读数。"""
    conn = get_conn()
    try:
        with conn.cursor() as cur:
            columns = ("id, device_id, temp_x10, humi_x10, lux, valid_mask, "
                       "soil_temp_x10, soil_humi_x10, soil_ec, soil_salinity, "
                       "soil_n, soil_p, soil_k, soil_ph_x10, ts ")
            queries = (
                ("WHERE device_id=%s ORDER BY id DESC LIMIT %s", (AIR_DEVICE_ID, limit)),
                ("WHERE device_id=%s ORDER BY id DESC LIMIT %s", (SOIL1_DEVICE_ID, limit)),
                ("WHERE device_id=%s ORDER BY id DESC LIMIT %s", (SOIL2_DEVICE_ID, limit)),
                ("WHERE device_id NOT IN (%s,%s,%s) ORDER BY id DESC LIMIT %s",
                 (AIR_DEVICE_ID, SOIL1_DEVICE_ID, SOIL2_DEVICE_ID, limit)),
                ("WHERE device_id=%s AND temp_x10 IS NOT NULL "
                 "AND humi_x10 IS NOT NULL AND lux IS NOT NULL "
                 "ORDER BY id DESC LIMIT 1", (AIR_DEVICE_ID,)),
                ("WHERE device_id=%s AND soil_temp_x10 IS NOT NULL "
                 "ORDER BY id DESC LIMIT 1", (SOIL1_DEVICE_ID,)),
                ("WHERE device_id=%s AND soil_temp_x10 IS NOT NULL "
                 "ORDER BY id DESC LIMIT 1", (SOIL2_DEVICE_ID,)),
            )
            by_id = {}
            for suffix, params in queries:
                cur.execute("SELECT " + columns + "FROM sensor_data " + suffix,
                            params)
                for row in cur.fetchall():
                    by_id[row[0]] = row[1:]
            return [by_id[row_id] for row_id in sorted(by_id, reverse=True)]
    finally:
        conn.close()
