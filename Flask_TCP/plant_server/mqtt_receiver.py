"""订阅 ESP32 传感器快照，将有效读数写入现有 MySQL 数据表。"""

import paho.mqtt.client as mqtt

from .config import MQTT_HOST, MQTT_PORT, MQTT_TOPIC
from .storage import save_records
from .telemetry import parse_mqtt_payload


def mqtt_subscriber():
    """由 server.py 的后台线程运行；断线后持续重连并重新订阅。"""
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)

    def on_connect(client, userdata, flags, reason_code, properties):
        if reason_code != 0:
            print("[MQTT] Broker 拒绝连接:", reason_code)
            return
        result, _ = client.subscribe(MQTT_TOPIC, qos=0)
        if result == mqtt.MQTT_ERR_SUCCESS:
            print("[MQTT] 已订阅:", MQTT_TOPIC)
        else:
            print("[MQTT] 订阅失败:", result)

    def on_disconnect(client, userdata, disconnect_flags, reason_code, properties):
        if reason_code != 0:
            print("[MQTT] 连接断开，等待重连:", reason_code)

    def on_message(client, userdata, message):
        if message.topic != MQTT_TOPIC:
            return
        try:
            records = parse_mqtt_payload(message.payload)
            if not records:
                print("[MQTT] 本次没有有效传感器读数，跳过入库")
                return
            save_records(records)
        except (UnicodeDecodeError, ValueError, KeyError, TypeError) as exc:
            print("[MQTT] 数据格式错误:", exc)
            return
        except Exception as exc:
            print("[MQTT] 入库失败:", exc)
            return
        print("[MQTT] 已入库:", ", ".join(record.device_id for record in records))

    client.on_connect = on_connect
    client.on_disconnect = on_disconnect
    client.on_message = on_message
    client.reconnect_delay_set(min_delay=1, max_delay=30)
    client.connect_async(MQTT_HOST, MQTT_PORT, keepalive=60)
    print("[MQTT] 正在连接 %s:%s" % (MQTT_HOST, MQTT_PORT))
    client.loop_forever(retry_first_connection=True)
