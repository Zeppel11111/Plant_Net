# Plant_Net 移植指南

本仓库的 `BISHE` 分支包含 ESP32-S3 采集固件、ESP-CAM 推流固件、Flask 网页与数据库服务、嘉立创 EDA 工程和陶晶驰 HMI 工程。新电脑上先克隆仓库，再分别配置两块板和服务器。真实账号、运行照片、构建缓存、个人申报材料以及旧 STM32 工程不在当前文件树中。

## 1. 克隆并确认目录

```powershell
git clone --branch BISHE https://github.com/Zeppel11111/Plant_Net.git
cd Plant_Net
```

主要目录：`project/Plant_Net/` 是 ESP-IDF 工程；`ESP-CAM/CameraWebServer/` 是 Arduino 工程；`Flask_TCP/` 是服务端；`PCB/毕业设计集成版.eprj2` 是当前集成板工程；`taojingchi/` 包含 HMI 工程和页面素材。`project/Plant_Net/任务架构/` 有任务调度图。

## 2. ESP32-S3：传感器、串口屏和 MQTT

1. 安装 ESP-IDF **v5.1.2** 和对应工具链，在 `project/Plant_Net/` 打开工程。不要把旧电脑生成的 `build/` 或 `sdkconfig` 复制过来；仓库的 `sdkconfig.defaults` 记录了当前 ESP32-S3、DIO、80 MHz、2 MB Flash 设置。新电脑运行 `idf.py set-target esp32s3`，核对实际模组的 Flash 容量后再构建和烧录。
2. 复制 `project/Plant_Net/components/Wifi/wifi_config.example.h` 为同目录的 `wifi_config.h`。在 `WIFI_CONFIG_NETWORKS` 中按优先级写入已知热点，每条是 `{ "SSID", "密码" }`，相邻两条之间要有逗号，跨行宏的前一行末尾要有 `\`。设备启动和掉线后会扫描并依次尝试可见热点。新增热点需要重新烧录一次，已写入热点之间的切换无需烧录。
3. 如 MQTT Broker 改动，更新 `components/mqtt_service/mqtt_service_config.h`；发布 topic 在 `main/app_main.c`，服务端订阅 topic 必须一致。当前示例默认 `mqtt://broker.emqx.io:1883` 和 `plant_net/Zeppel1/test`。
4. 集成板连线以 `components/bsp/bsp_config.h` 为准：RS485 使用 UART1，TX=GPIO17、RX=GPIO16、DE 与 /RE 共用 GPIO18（高电平发送）；陶晶驰使用 UART2，TX=GPIO8、RX=GPIO9，两个串口均为 9600、8N1。土壤地址为 `0x02`、`0x03`，塔石为 `0x01`。移植到不同板子前核对实际引脚和供电。

## 3. ESP-CAM：视频推流

1. 在 Arduino IDE 中打开 `ESP-CAM/CameraWebServer/CameraWebServer.ino`，选择工程中定义的 **AI Thinker ESP32-CAM** 板型，启用 PSRAM，并选择能容纳至少 3 MB 应用程序的分区方案。`partitions.csv` 是本工程保存的分区参考；实际烧录设置以所用 Arduino ESP32 板包为准。
2. 复制 `ESP-CAM/CameraWebServer/net_config.example.h` 为同目录的 `net_config.h`。在 `PN_WIFI_NETWORKS` 中按优先级写入 `{ "SSID", "密码", "Flask电脑地址" }`。第三项应填写该热点下运行 Flask 的电脑 IP 或主机名。Windows 移动热点通常由电脑充当网关；普通路由器的网关通常不是 Flask 电脑。CAM 会先连接配置地址，失败后尝试网关。
3. 确认 CAM 与 Flask 电脑能互访，电脑防火墙允许 TCP **9001**；CAM 通过此端口向 Flask 推送 JPEG 帧。网页和 API 使用 TCP **8080**。切换网络后 Flask 电脑的 IP 可能改变，要同步修改该热点对应的第三项并重新烧录。

## 4. Flask、MQTT 与 MySQL

1. 安装 Python、MySQL 8，创建虚拟环境，在 `Flask_TCP/` 执行 `pip install -r requirements.txt`。
2. 复制 `local_config.example.py` 为 `local_config.py`，填写本机 MySQL 地址、用户、密码和库名；也可使用 `PLANT_NET_MYSQL_HOST`、`PLANT_NET_MYSQL_USER`、`PLANT_NET_MYSQL_PASSWORD`、`PLANT_NET_MYSQL_DB` 环境变量。不要提交真实配置。
3. 在 `Flask_TCP/` 执行 `python server.py`。程序会按当前结构建表；`schema.sql` 可用于人工核对。若从旧版 `dev01` 混合数据迁移，先备份数据库，再参考 `migrations/001_split_three_devices.sql`，不要对新数据库重复运行旧数据迁移。
4. 默认订阅 `plant_net/Zeppel1/test`，可用 `PLANT_NET_MQTT_HOST`、`PLANT_NET_MQTT_PORT`、`PLANT_NET_MQTT_TOPIC` 调整。网页地址为 `http://电脑IP:8080/`。空气与两只土壤设备分别按 `tas01`、`soil02`、`soil03` 入库；每条 MQTT 快照最多写三条设备记录。
5. `Flask_TCP/photos/` 是运行时照片目录，不在 Git 仓库。迁移历史照片时，要连同 MySQL 中的 `photo_records` 和对应 JPEG 文件一起单独备份；只复制数据库会留下失效图片路径。

## 5. PCB 与串口屏

使用嘉立创 EDA 打开 `PCB/毕业设计集成版.eprj2`；BOM 表和原理图参考图在 `PCB/`。使用陶晶驰编辑器打开 `taojingchi/TJC_UI_soil_compare.HMI` 查看土壤对照版；`TJC_UI.HMI` 保留较早版本。页面背景和说明放在 `taojingchi/素材/`。移植到另一型号屏幕时先确认分辨率、字库和控件名称，再核对 ESP32 的 UART2 发送指令。

## 6. 上线前核对

- ESP32、CAM 都能扫描并连接目标热点；两个设备不一定必须连同一 SSID，但必须能分别到达 MQTT Broker 与 Flask 电脑。
- ESP32 串口日志出现获取 IP、MQTT 连接和三只 485 设备采集结果；MQTTX 能收到完整 JSON。
- Flask 启动后订阅成功，MySQL 的 `sensor_data` 按 `tas01`、`soil02`、`soil03` 分别增长；网页空气页和土壤对照页显示最新读数。
- CAM 串口显示推流已连接，Flask 网页能看到视频，`photo_records` 与 `photos/` 每个周期新增一张照片。

本仓库不附带账号密码；`.gitignore` 排除了三端真实配置、构建文件和运行照片。公开仓库提交前仍应查看暂存清单，避免把新建的私有配置文件放到未忽略的路径。
