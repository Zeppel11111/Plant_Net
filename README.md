# Plant_Net 毕业设计工作区

植物环境监测项目，包含 ESP32-S3 固件、ESP-CAM 固件、Flask/MQTT/MySQL 服务、集成板 PCB 工程和陶晶驰串口屏 UI。

| 路径 | 内容 |
| --- | --- |
| `project/Plant_Net/` | ESP-IDF v5.1.2 固件；RS485 三设备采集、串口屏和 MQTT 上报 |
| `ESP-CAM/CameraWebServer/` | Arduino ESP-CAM 固件；视频推流到 Flask |
| `Flask_TCP/` | MQTT 订阅、MySQL 入库、Flask 网页、摄像头推流与截图 |
| `PCB/` | 集成板工程、BOM 与制造资料 |
| `taojingchi/` | 串口屏 HMI 工程和素材 |

开始前阅读 [固件说明](project/Plant_Net/README.md)和 [服务器说明](Flask_TCP/README.md)。数据库结构参考 `Flask_TCP/schema.sql`；旧版 `dev01` 混合数据的迁移脚本在 `Flask_TCP/migrations/`。

换电脑或换网络时按 [移植指南](MIGRATION.md) 操作。`BISHE` 分支以当前 ESP32 项目为准；旧 STM32 工程、第三方学习例程、运行数据和本机配置不放在分支当前文件树中。

仓库不包含本机密码、构建目录、Python 虚拟环境、运行时照片和个人申报材料。请从 `.example` 文件复制本机配置；不要把真实配置改名后提交。

ESP32-S3 与 ESP-CAM 都会扫描已知热点，按各自配置表的先后顺序连接。新增热点要分别填写 `project/Plant_Net/components/Wifi/wifi_config.h` 和 `ESP-CAM/CameraWebServer/net_config.h`，然后重新烧录一次；此后在这些已知热点之间切换无需再烧录。CAM 配置表的第三列是该热点下 Flask 电脑的地址。
