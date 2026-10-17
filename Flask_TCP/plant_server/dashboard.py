"""展示数据转换：将数据库行转为页面和 JSON 所需格式；不访问网络。"""

from .device_ids import AIR_DEVICE_ID, SOIL1_DEVICE_ID, SOIL2_DEVICE_ID


NO_AIR_DATA = "暂无数据"


def tfmt(x10):
    """温度显示：268 → "26.8"（负数也正确处理）"""
    if x10 is None:
        return NO_AIR_DATA
    return "%d.%d" % (int(x10 / 10), abs(x10) % 10)


def hfmt(x10):
    """湿度显示：391 → "39.1" """
    if x10 is None:
        return NO_AIR_DATA
    return "%d.%d" % (int(x10 / 10), x10 % 10)


def lfmt(lux):
    """光照显示；空气数据无效时不把 NULL 显示为零。"""
    return NO_AIR_DATA if lux is None else str(lux)


def soil_x10_value(value):
    """土壤温湿度/pH 的 x10 原始值转为 API 数值；NULL 保持为 JSON null。"""
    return None if value is None else value / 10


def soil_display(value, x10=False):
    """表格显示土壤读数；不可用数据不伪造为零。"""
    if value is None:
        return NO_AIR_DATA
    return ("%.1f" % (value / 10)) if x10 else str(value)


def dashboard_device_label(device_id, valid_mask):
    if device_id == AIR_DEVICE_ID:
        return "塔石传感器 (tas01 · 0x01)"
    if device_id == SOIL1_DEVICE_ID:
        return "土壤传感1 (soil02 · 0x02)"
    if device_id == SOIL2_DEVICE_ID:
        return "土壤传感2 (soil03 · 0x03)"
    return device_id


def dashboard_record(row):
    """把数据库行标准化为具名字段，同时兼容旧的五列测试/调用方。"""
    if len(row) == 5:
        device, temp, humi, lux, ts = row
        return {
            "device": device, "temp_x10": temp, "humi_x10": humi, "lux": lux,
            "valid_mask": 1 if any(value is not None for value in (temp, humi, lux)) else 0,
            "soil_temp_x10": None, "soil_humi_x10": None, "soil_ec": None,
            "soil_salinity": None, "soil_n": None, "soil_p": None,
            "soil_k": None, "soil_ph_x10": None, "ts": ts,
        }
    (device, temp, humi, lux, valid_mask, soil_temp, soil_humi, soil_ec,
     soil_salinity, soil_n, soil_p, soil_k, soil_ph, ts) = row
    return {
        "device": device, "temp_x10": temp, "humi_x10": humi, "lux": lux,
        "valid_mask": valid_mask, "soil_temp_x10": soil_temp,
        "soil_humi_x10": soil_humi, "soil_ec": soil_ec,
        "soil_salinity": soil_salinity, "soil_n": soil_n, "soil_p": soil_p,
        "soil_k": soil_k, "soil_ph_x10": soil_ph, "ts": ts,
    }


def make_points(records):
    """保持原 points 的空气数据布局，新增 soil_points 另行承载土壤数据。"""
    records = list(records)
    points = [[int(record["ts"].timestamp()), record["temp_x10"],
               record["humi_x10"], record["lux"]] for record in records]
    soil_points = [[int(record["ts"].timestamp()), record["soil_temp_x10"],
                    record["soil_humi_x10"], record["soil_ec"],
                    record["soil_salinity"], record["soil_n"], record["soil_p"],
                    record["soil_k"], record["soil_ph_x10"]] for record in records]
    return points, soil_points


def dashboard_series(records):
    """同一张表中的两只土壤探头按设备标识分别生成曲线。"""
    air = [record for record in records if record["device"] == AIR_DEVICE_ID]
    first_soil = [record for record in records if record["device"] == SOIL1_DEVICE_ID]
    second_soil = [record for record in records if record["device"] == SOIL2_DEVICE_ID]
    points, _ = make_points(reversed(air))
    _, soil_points = make_points(reversed(first_soil))
    _, soil2_points = make_points(reversed(second_soil))
    return points, soil_points, soil2_points


def api_row(record):
    """生成保留旧空气字段、补充土壤字段的 API 行。"""
    return {
        "device": record["device"], "temp": tfmt(record["temp_x10"]),
        "humi": hfmt(record["humi_x10"]), "lux": record["lux"],
        "time": record["ts"].strftime("%m-%d %H:%M:%S"),
        "valid_mask": record["valid_mask"],
        "soil_temp": soil_x10_value(record["soil_temp_x10"]),
        "soil_humi": soil_x10_value(record["soil_humi_x10"]),
        "soil_ec": record["soil_ec"], "soil_salinity": record["soil_salinity"],
        "soil_n": record["soil_n"], "soil_p": record["soil_p"],
        "soil_k": record["soil_k"], "soil_ph": soil_x10_value(record["soil_ph_x10"]),
    }


def grouped_table_rows(records, limit=20):
    """三张最近记录表各取最新的有效读数；摄像头记录不混入传感器表。"""
    groups = {"air": [], "soil1": [], "soil2": []}
    for record in records:
        device = record["device"]
        if device == AIR_DEVICE_ID:
            if (len(groups["air"]) < limit and
                    any(record[name] is not None
                        for name in ("temp_x10", "humi_x10", "lux"))):
                groups["air"].append(api_row(record))
        elif device == SOIL1_DEVICE_ID:
            if (len(groups["soil1"]) < limit and
                    record["soil_temp_x10"] is not None):
                groups["soil1"].append(api_row(record))
        elif (device == SOIL2_DEVICE_ID and len(groups["soil2"]) < limit and
              record["soil_temp_x10"] is not None):
            groups["soil2"].append(api_row(record))
    return groups
