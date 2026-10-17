"""服务启动入口：初始化数据库并启动 MQTT、相机和网页服务。"""

import sys
import threading
from plant_server.config import WEB_PORT
from plant_server.storage import init_db
from plant_server.mqtt_receiver import mqtt_subscriber
from plant_server.camera import push_listener, recorder_loop
from plant_server.web import app


if __name__ == "__main__":
    print("===== 植物环境监测数据服务器 =====")
    try:
        init_db()
    except Exception as exc:
        print("[错误] 连不上 MySQL:", exc)
        print("       请确认：1) MySQL80 服务已启动  2) 数据库配置正确")
        input("按回车退出...")
        sys.exit(1)

    # MQTT 数据接收与摄像头收流互不占用端口；视频帧仍由本进程提供给网页。
    threading.Thread(target=mqtt_subscriber, daemon=True).start()
    threading.Thread(target=push_listener, daemon=True).start()
    threading.Thread(target=recorder_loop, daemon=True).start()

    print("网页地址: http://127.0.0.1:%d" % WEB_PORT)
    print("（按 Ctrl+C 停止）")
    app.run(host="0.0.0.0", port=WEB_PORT, threaded=True)
