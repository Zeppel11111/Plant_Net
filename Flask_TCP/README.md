# 植物环境监测服务（MQTT + Flask + MySQL）

ESP32 将塔石、土壤传感1（0x02）和土壤传感2（0x03）的采集快照发布到 MQTT Broker。本服务订阅消息，保存有效读数到 MySQL，并通过 Flask 提供网页和数据接口。摄像头收流、截图和视频页面继续使用原有链路。

## 模块

| 文件 | 职责 |
|---|---|
| `server.py` | 初始化数据库，启动 MQTT 订阅、摄像头和 Flask |
| `plant_server/config.py` | Broker、数据库、网页与摄像头端口配置 |
| `plant_server/mqtt_receiver.py` | 连接 Broker、订阅传感器 topic、处理重连和入库 |
| `plant_server/telemetry.py` | 校验 ESP32 JSON，把有效读数转换为数据库记录 |
| `plant_server/storage.py` | 建库、数据库迁移及传感器记录读写 |
| `plant_server/camera.py` | 摄像头 TCP 9001 收流和截图 |
| `plant_server/dashboard.py`、`plant_server/web.py` | 网页数据转换与 HTTP 接口 |

传感器不再连接原来的 TCP 9000 入口。摄像头 TCP 9001、网页 HTTP 8080、ESP-CAM 的 `/api/data` 接口保持原样。

## 数据约定

默认订阅 `plant_net/Zeppel1/test`。一条 ESP32 JSON 消息包含 `light`、`air_temp`、`air_hum`、`soil1`、`soil2` 和 `valid`。`soil1.address` 为 `0x02`，`soil2.address` 为 `0x03`。

- 塔石 0x01、土壤传感1 0x02、土壤传感2 0x03 分别写入 `device_id=tas01`、`soil02`、`soil03`。一条完整 MQTT 消息最多产生三条数据库记录，三组数据各占一行。
- `valid=false` 时 ESP32 可能仍带有上次成功的数值。服务会把本组数据库字段置为 `NULL`；若某设备在该消息中全无有效读数，则不新增该设备记录。
- `valid_mask` 的 bit 0 表示塔石记录有效；bit 1 表示土壤记录有效。各列是否真正有效以 `NULL` 判断。
- 数据库存储温度、湿度、pH 的 10 倍整数，例如 `19.8` 存为 `198`。网页按原来的单位展示。ESP32 每轮采集完成后发布一份快照。

旧版 `dev01` 行同时包含塔石和土壤 0x02 数据。升级已有数据库前先停止 Flask、备份 `sensor_data`，再执行 `migrations/001_split_three_devices.sql`；脚本保留时间戳并把混合行拆成两条。新安装直接由 `server.py` 建表即可。

## 启动

1. 启动 MySQL；复制 `local_config.example.py` 为 `local_config.py`，填写本机连接信息，也可使用环境变量。真实配置文件已被工作区根目录的 `.gitignore` 排除。
2. 安装 `requirements.txt` 中的依赖，其中包含 `paho-mqtt`。
3. 运行 `server.py`，等待日志出现 `[MQTT] 已订阅: plant_net/Zeppel1/test`。
4. 打开 `http://127.0.0.1:8080`。页面每 5 秒请求 `/api/latest` 更新数据。

网页分为空气与土壤对照两个页签：空气页只展示塔石读数和记录；土壤页同时展示两只探头各自的记录表。每个土壤指标使用一张对照图，在同一时间轴和纵轴上叠加 0x02、0x03 两条曲线。

Broker 默认是 `broker.emqx.io:1883`。如需切换，可设置环境变量 `PLANT_NET_MQTT_HOST`、`PLANT_NET_MQTT_PORT`、`PLANT_NET_MQTT_TOPIC`，并保持 topic 与 ESP32 发布地址一致。

摄像头最新视频帧保存在服务进程内，因此 Flask 与摄像头接收程序仍需运行在同一个进程。
