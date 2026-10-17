# Plant_Net ESP32-S3 固件

基于 ESP-IDF v5.1.2。ESP32-S3 通过 UART1/RS485 顺序读取塔石空气传感器（0x01）和两只土壤传感器（0x02、0x03），通过 UART2 更新陶晶驰串口屏，每轮采集完成后向 MQTT 发送一次 JSON 快照。

## 主要目录

| 目录 | 作用 |
| --- | --- |
| `components/bsp/` | UART、RS485 方向及硬件引脚初始化 |
| `components/Wifi/` | 扫描已知热点、按优先级连接及断线切换 |
| `components/mqtt_service/` | MQTT 客户端封装 |
| `APP/` | 传感器轮询、解析与显示接口 |
| `main/` | FreeRTOS 任务、队列和数据上报 |

## 本机配置与构建

1. 复制 `components/Wifi/wifi_config.example.h` 为同目录的 `wifi_config.h`，按优先级填写多组热点名称和密码。真实配置文件被工作区根目录的 `.gitignore` 排除。
2. 使用 ESP-IDF v5.1.2 打开本目录，按自己的开发板配置目标芯片和串口引脚。
3. 在 ESP-IDF 环境中运行 `idf.py build`，烧录后查看串口日志。

`build/`、`sdkconfig` 和本机 Wi-Fi 配置都不上传。MQTT 服务端地址见 `components/mqtt_service/mqtt_service_config.h`；Flask 服务订阅的 topic 必须与固件一致。
