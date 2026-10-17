"""运行配置：统一管理端口、数据库连接和照片存放路径；不启动服务。"""

import os


try:
    # 本机配置仅用于开发环境；云端也可使用环境变量。
    from local_config import MYSQL_HOST, MYSQL_USER, MYSQL_PASSWORD, MYSQL_DB
except ImportError:
    MYSQL_HOST = os.getenv("PLANT_NET_MYSQL_HOST", "127.0.0.1")
    MYSQL_USER = os.getenv("PLANT_NET_MYSQL_USER", "root")
    MYSQL_PASSWORD = os.getenv("PLANT_NET_MYSQL_PASSWORD", "")
    MYSQL_DB = os.getenv("PLANT_NET_MYSQL_DB", "plant_db")

MQTT_HOST = os.getenv("PLANT_NET_MQTT_HOST", "broker.emqx.io")
MQTT_PORT = int(os.getenv("PLANT_NET_MQTT_PORT", "1883"))
MQTT_TOPIC = os.getenv("PLANT_NET_MQTT_TOPIC", "plant_net/Zeppel1/test")
WEB_PORT = 8080       # 浏览器 HTTP 入口
PUSH_PORT = 9001      # 摄像头 JPEG 推帧入口
SNAP_INTERVAL_SEC = 5 * 60  # 自动截图间隔：5 分钟；0 为关闭
RECORD_DEVICE = "cam1"

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PHOTO_DIR = os.path.join(BASE_DIR, "photos")
