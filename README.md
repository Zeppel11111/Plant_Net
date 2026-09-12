# Plant_Net —— 植物生长环境监测系统

STM32 采集温湿度/光照 + ESP32-CAM 图像监控 + Flask/MySQL 服务器，局域网绑定运行、可远程访问（cpolar）。

## 目录（三区 + 共享）

```
Plant_Net/
├─ 首次配置.bat  双击后填写 3 项本机信息，生成全部私有配置
├─ STM32/        STM32F103 工程（传感器采集 + ESP8266 数据上送）
│                 子件：Driver/ESP8266.c(AT驱动)、Core/Src/freertos.c(30s任务)
├─ ESP-CAM/      ESP32-CAM 固件（CameraWebServer 改造版：纯客户端推流，XCLK 自动降频）
├─ Flask_TCP/    服务器（Flask + MySQL，收 TCP 数据/收流/网页）
├─ config/       公开网络配置模板及向导生成的本机配置
├─ docs/         配置地图 deployment-config-map.md、设计规格等
└─ tools/        首次配置向导 first_run_setup.py
```

## 架构

```
STM32(塔石传感器,30s) ─USART3─► ESP8266 ─TCP:9000─►┐
                                                   ├─► Flask/MySQL(plant_db) ─► 网页
ESP32-CAM(VGA@10fps 推流) ────────TCP:9001────────►┘      /api/live 实时画面
                                                           60s 定时截图存 photos/
```

- **数据**：ESP8266 走 TCP 9000（server.py TCP 线程），表 `sensor_data`(dev01)
- **图像**：ESP-CAM 纯客户端推 JPEG 帧到 9001（`L:<len>\n`+字节），Flask 内存最新帧 →
  `/api/live` 实时流 + 定时截取入库 `photo_records`，图落盘 `Flask_TCP/photos/`
- **网页**：http://<服务器IP>:8080 —— 数据卡 + 实时监控 + 拍照存库

## 快速上手

1. **首次配置**：在项目根目录双击 `首次配置.bat`，只需依次填写 MySQL 密码、Windows
   移动热点名称、移动热点密码。向导会生成服务器配置和两份一致的固件配置；真实密码不会提交。
2. **服务器**：确保 MySQL 在运行，然后执行向导完成页给出的服务器启动命令（该命令会自动
   使用本机实际可用的 Python）。
3. **烧录**：STM32 用 CubeIDE/CMake 编译并烧录 `STM32/`；ESP-CAM 用 Arduino 打开
   `ESP-CAM/CameraWebServer/CameraWebServer.ino` 上传
4. **外网访问**：cpolar 把本地 8080 建 Web 隧道（远程看网页）；设备异地部署时可给
   TCP 9000/9001 建 TCP 隧道并把 `PN_SERVER_HOST/端口` 改为公网地址（见配置地图）

## 注意事项

- 两块板连**同一热点**；绑定模式下服务器开"移动热点"地址恒 192.168.137.1
- ESP-CAM 要点：初始化 20MHz → 上电自动降 15MHz（模块时序稳定）；GPIO16 收 STM32 数据为预留（当前数据走 ESP8266）
- 换网/换地方全流程见 `docs/deployment-config-map.md`
