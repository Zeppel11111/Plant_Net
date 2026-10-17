"""遥测协议：解析 MQTT 快照，保留旧文本解析工具供历史数据使用。"""

from dataclasses import dataclass
from decimal import Decimal
import json
import re
from typing import Optional

from .device_ids import AIR_DEVICE_ID, SOIL1_DEVICE_ID, SOIL2_DEVICE_ID


@dataclass(frozen=True)
class TelemetryRecord:
    """一条设备遥测记录；无效组的字段统一使用 None。"""
    device_id: str
    valid_mask: int
    temp_x10: Optional[int]
    humi_x10: Optional[int]
    lux: Optional[int]
    soil_temp_x10: Optional[int]
    soil_humi_x10: Optional[int]
    soil_ec: Optional[int]
    soil_salinity: Optional[int]
    soil_n: Optional[int]
    soil_p: Optional[int]
    soil_k: Optional[int]
    soil_ph_x10: Optional[int]

    @property
    def soil_fields(self):
        return (self.soil_temp_x10, self.soil_humi_x10, self.soil_ec,
                self.soil_salinity, self.soil_n, self.soil_p, self.soil_k,
                self.soil_ph_x10)


SMALLINT_RANGE = (-32768, 32767)


INT_RANGE = (-2147483648, 2147483647)


AIR_VALUE_RANGES = (SMALLINT_RANGE, SMALLINT_RANGE, INT_RANGE)


SOIL_VALUE_RANGES = (
    SMALLINT_RANGE, SMALLINT_RANGE, INT_RANGE, INT_RANGE,
    INT_RANGE, INT_RANGE, INT_RANGE, SMALLINT_RANGE,
)


def _reject_json_constant(value):
    raise ValueError("JSON 中不允许 %s" % value)


def _scaled_value(group, name, scale, value_range):
    """校验数值并按数据库约定转为整数；不接受布尔值和多余小数位。"""
    raw = group[name]
    if type(raw) not in (int, float):
        raise ValueError("%s 必须是数字" % name)
    value = Decimal(str(raw)) * scale
    if not value.is_finite() or value != value.to_integral_value():
        raise ValueError("%s 的精度无效" % name)
    result = int(value)
    if not value_range[0] <= result <= value_range[1]:
        raise ValueError("%s 超出数据库范围" % name)
    return result


def _soil_values(group):
    fields = ("temp", "moist", "ec", "salt", "n", "p", "k", "ph")
    scales = (10, 10, 1, 1, 1, 1, 1, 10)
    return tuple(_scaled_value(group, name, scale, limits)
                 for name, scale, limits in zip(fields, scales, SOIL_VALUE_RANGES))


def parse_mqtt_payload(payload):
    """将 ESP32 的一份 MQTT JSON 快照转换为最多三条设备记录。

    塔石 0x01、土壤 0x02 和土壤 0x03 各占一条记录。
    读取失败时 ESP32 仍携带旧数值，因此这里只保存有效组，其余字段置为 NULL。
    """
    if len(payload) > 2048:
        raise ValueError("MQTT 消息过长")
    data = json.loads(payload, parse_constant=_reject_json_constant)
    if not isinstance(data, dict):
        raise ValueError("MQTT 消息必须是 JSON 对象")

    valid = data.get("valid")
    if not isinstance(valid, dict):
        raise ValueError("缺少 valid 对象")
    for name in ("light", "air", "soil1", "soil2"):
        if type(valid.get(name)) is not bool:
            raise ValueError("valid.%s 必须是布尔值" % name)

    soil1 = data.get("soil1")
    soil2 = data.get("soil2")
    if not isinstance(soil1, dict) or soil1.get("address") != "0x02":
        raise ValueError("soil1 地址必须是 0x02")
    if not isinstance(soil2, dict) or soil2.get("address") != "0x03":
        raise ValueError("soil2 地址必须是 0x03")

    # bit0 表示塔石数据有效；bit1 表示土壤数据有效。无效字段保持 NULL。
    air = TelemetryRecord(
        AIR_DEVICE_ID, 1 if valid["air"] or valid["light"] else 0,
        _scaled_value(data, "air_temp", 10, SMALLINT_RANGE) if valid["air"] else None,
        _scaled_value(data, "air_hum", 10, SMALLINT_RANGE) if valid["air"] else None,
        _scaled_value(data, "light", 1, INT_RANGE) if valid["light"] else None,
        *((None,) * 8),
    )
    first_soil = TelemetryRecord(
        SOIL1_DEVICE_ID, 2 if valid["soil1"] else 0, None, None, None,
        *(_soil_values(soil1) if valid["soil1"] else (None,) * 8),
    )
    second_soil = TelemetryRecord(
        SOIL2_DEVICE_ID, 2 if valid["soil2"] else 0, None, None, None,
        *(_soil_values(soil2) if valid["soil2"] else (None,) * 8),
    )
    return tuple(record for record in (air, first_soil, second_soil)
                 if record.valid_mask)


def parse_line(line):
    """解析旧版空气数据或 v2 空气/土壤数据，失败返回 None。"""
    parts = line.strip().split(",")
    if len(parts) == 3:                 # 老格式：没有设备ID
        device_id = "dev01"
        nums = parts
    elif len(parts) == 4:               # 新格式：第一个字段是设备ID
        device_id = parts[0].strip()
        nums = parts[1:]
        if not valid_device_id(device_id):
            return None
    elif len(parts) == 14 and parts[0] == "v2":
        device_id = parts[1].strip()
        if not valid_device_id(device_id):
            return None
        try:
            valid_mask = int(parts[2])
            values = [int(value) for value in parts[3:]]
        except ValueError:
            return None
        if valid_mask < 0 or valid_mask > 3:
            return None
        air_values = values[:3] if valid_mask & 1 else [None, None, None]
        soil_values = values[3:] if valid_mask & 2 else [None] * 8
        if not values_within_ranges(air_values, AIR_VALUE_RANGES):
            return None
        if not values_within_ranges(soil_values, SOIL_VALUE_RANGES):
            return None
        return TelemetryRecord(device_id, valid_mask, *air_values, *soil_values)
    else:
        return None                     # 字段数不对，不是合法数据行

    try:
        temp_x10 = int(nums[0])
        humi_x10 = int(nums[1])
        lux      = int(nums[2])
    except ValueError:
        return None                     # 有非数字内容，不是合法数据行
    if not values_within_ranges((temp_x10, humi_x10, lux), AIR_VALUE_RANGES):
        return None
    return TelemetryRecord(device_id, 1, temp_x10, humi_x10, lux,
                           None, None, None, None, None, None, None, None)


def valid_device_id(device_id):
    """设备 ID 与 sensor_data.device_id 的 VARCHAR(20) 限制保持一致。"""
    return len(device_id) <= 20 and bool(re.fullmatch(r"[A-Za-z0-9_-]+", device_id))


def values_within_ranges(values, ranges):
    """检查有效遥测值能装进其对应的 MySQL 有符号整型列。"""
    return all(value is None or lower <= value <= upper
               for value, (lower, upper) in zip(values, ranges))
