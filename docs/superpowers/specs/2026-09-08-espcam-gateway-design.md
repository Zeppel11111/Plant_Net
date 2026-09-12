# ESP-CAM 网关替代 ESP8266 —— 设计文档

- 日期：2026-09-08
- 状态：已获用户整体批准（方案 A：UART 桥 + 双链路 HTTP）；本文件为批准后的正式规格
- 关联目标：goal-6be49ad3（M1–M5 里程碑实施）

## 1. 背景与目标

Plant_Net 系统现状：STM32F103（FreeRTOS）挂 TAS_GZ 温湿度光照传感器（RS485，USART2），每 30 秒读一次，
通过 USART3（PB10/PB11, 115200 8N1）接 ESP8266-01S（AT 固件），由 STM32 主动建 TCP 连接把数据推到
PC 热点网关 `192.168.137.1:9000`，服务端入库（Flask + MySQL 生态）。

目标：用 **ESP32-CAM（AI Thinker，OV2640）取代 ESP8266**，同时承担：
1. **数据桥**：接收 STM32 每 30s 推送的传感器行，转发/上报到服务器（保持"数据密"的节奏与语义）；
2. **图像采集**：周期低频拍照（"图像疏"），图片与读数快照打包上传；
3. 不需要自带网页 UI；通信与图像链路相互独立、可分别调间隔。

当前阶段：实测/联调阶段，要求间隔可调、链路可独立开关。

## 2. 现状系统事实（代码审阅结论）

| 项 | 现状 | 出处 |
|---|---|---|
| 采集节奏 | 每 30s 读一次 TAS_GZ（温度x10、湿度x10、光照 lux） | `Core/Src/freertos.c` `vTaskDelayUntil(…, 30000)` |
| 上行数据契约 | 一行文本：`温度x10,湿度x10,光照\n`（逗号分隔，`\n` 为行结束符，服务器按行切分） | freertos.c L210-216 |
| 链路 | STM32 主动：`AT+CWJAP` 连网 → `AT+CIPSTART TCP` → `AT+CIPSEND` 发裸行；断线每轮重连 | `Driver/ESP8266.c` |
| 服务器地址 | `192.168.137.1:9000`（Windows 热点 ICS 网段，PC 即网关） | freertos.c L48-49 |
| WiFi 热点 | SSID / 密码（本地 `net_config.h` 凭据表） | ESP8266、ESP-CAM 配置一致 |
| 调试口 | USART1 printf | freertos.c |

接线现状：USART3 TX=PB10→模块 RXD、RX=PB11←模块 TXD；VCC 3.3V、CH_PD/RST 拉高。

## 3. 目标架构（方案 A）

```
TAS_GZ(USART2) ──> STM32F103 ──USART3(PB10/PB11)──> ESP32-CAM(UART2 GPIO16/17)
  每30s采样         每30s发一行文本                │   WiFi STA "YOUR_WIFI_SSID"
                    （不再管 WiFi/TCP）            │
                                                  ├─ 任务1 数据链: 逐行 JSON POST /api/data   （密）
                                                  ├─ 任务2 图像链: 每N分钟 JPEG multipart      （疏）
                                                  │            POST /api/photo
                                                  └─> PC 网关 192.168.137.1 ──> Flask ──> MySQL
```

原则：
- ESP-CAM 是"协议主人"：WiFi、断线重连、上传节奏都由它管；STM32 只管把采样行从串口推给它。
- 两条链路的调度完全独立（独立任务），互相不阻塞。
- 不做可靠传输/重传：数据是周期采样，丢一行 30s 后自然补齐（刻意取舍）。

## 4. 硬件层设计

| 信号 | STM32 侧 | ESP32-CAM 侧 | 说明 |
|---|---|---|---|
| STM32→CAM | USART3 TX = PB10 | **GPIO16（UART2 RX）** | 3.3V 逻辑直连 |
| CAM→STM32 | USART3 RX = PB11 | **GPIO17（UART2 TX）** | 3.3V 逻辑直连（测试期可只接单向） |
| GND | GND | GND | 必须共地 |
| 供电 | 原样 | **5V 独立供电**（模块自带 USB 口或独立 5V 电源） | 禁止从 STM32 3.3V 取电；摄像头+WiFi 峰值电流会拉崩 STM32 |

约束与待实测项：
- ESP32-CAM 用 UART2（GPIO16/17），避开 UART0（GPIO1/3 烧录/USB 日志口），烧录固件不必拔线。
- **待实测**：GPIO16/17 是否在用户这块 ESP32-CAM 上有引出焊点/排针（不同批次有差异）——M1 阶段先验证；若无引出，退路是 GPIO1/3（UART0）并在烧录时断开 STM32 方向，或换其他可用 GPIO + Serial1 重映射。
- ESP32 引脚**不耐 5V**，与 STM32 3.3V 逻辑互连安全；若 STM32 侧某 IO 被配成 5V 开漏需修正。
- WiFi 覆盖：CAM 与 STM32 同处热点覆盖范围（现状 ESP8266 已在该网工作，等价验证）。

## 5. STM32 ↔ CAM 帧协议

- 方向：STM32 → CAM（上行数据）；CAM → STM32 测试期仅用于回显/握手，业务不依赖。
- 帧格式：**沿用现有行契约** `温度x10,湿度x10,光照\n`（例 `254,601,12345\n`），STM32 端拼行代码不变，
  只把"AT+CIPSEND 发 WiFi"换成"直接写 USART3"。
- 波特率：115200 8N1（与现状一致）。
- CAM 收帧：行缓冲 128B，以 `\n` 切帧；超长/坏帧丢弃；收到完整行即进入数据链处理。
- 错误处理：丢帧不重传、不 ACK（见第 3 节原则）。测试期可加回显开关便于对打验证。

## 6. ESP-CAM 固件设计（Arduino sketch，双任务）

前置修复（M1，必须先做）：
- `CameraWebServer.ino`（或最终网关固件）顶部板型宏：**注释 `#define CAMERA_MODEL_ESP_EYE`，
  仅保留 `#define CAMERA_MODEL_AI_THINKER`**。原因：两宏同时开启时 `camera_pins.h` 的 `#if/#elif`
  链先命中 ESP_EYE 分支 → `pin_pwdn = -1` → AI Thinker 板上 OV2640 的 PWDN(GPIO32) 不被驱动、
  传感器掉电 → SCCB 探测失败 `0x105(ESP_ERR_NOT_FOUND)`。
- Arduino IDE：Board = AI Thinker ESP32-CAM（或带 PSRAM 的 Wrover），PSRAM = Enabled(QSPI)，
  Flash 4MB，Partition = Huge APP(3MB APP/1MB SPIFFS)。

任务结构（Arduino 内 xTaskCreate 两个任务 + loop 空闲）：

```
[Task:data]   UART2 读行 → 校验 → 取最新读数缓存 → HTTP POST /api/data
              （失败重试 1 次后丢弃，绝不影响下一行；30s 间隔天然节流）
[Task:photo]  每 PHOTO_INTERVAL_MIN 分钟：
              fb = esp_camera_fb_get() → JPEG
              multipart POST /api/photo（image 文件 + 读数快照字段）→ fb 归还
              失败：下个周期重试，不阻塞数据链
[main/loop]   WiFi 状态维护：STA 自动重连（setSleep(false)+reconnect），
              启动时等待联网；上报失败不影响循环
```

初始参数常量表：

| 常量 | 默认值 | 说明 |
|---|---|---|
| `DATA_RECV_BAUD` | 115200 | UART2 |
| `PHOTO_INTERVAL_MIN` | 10 | 分钟/张，唯一改此常量调图像频率 |
| `PHOTO_FRAME_SIZE` | FRAMESIZE_QVGA | 320×240，单张 JPEG ~十几 KB |
| `JPEG_QUALITY` | 12 | |
| `SERVER_HOST` | `192.168.137.1` | 与现状一致 |
| `HTTP_PORT` | 5000 | Flask HTTP（新端点，见第 7 节） |
| `DEVICE_ID` | `"cam1"` | 上报时附带的设备标识 |

其他约定：
- 时间戳：测试期**由服务器记录到达时间**，CAM 不跑 NTP（无电池 RTC，少一个变量）；需要时后补 NTP。
- 日志：UART0（USB 串口）printf 打印关键事件（连网、收到行、POST 结果、拍照周期）。
- 帧缓冲：`fb_count=2`、`grab_mode=CAMERA_GRAB_LATEST`（PSRAM 场景）；拍照任务持帧时间短，归还及时。

## 7. Flask / MySQL 设计

新增两个端点（用户本地 Flask 项目内添加，实施阶段提供示例代码）：

**`POST /api/data`** —— 数据链，逐条入库
- 请求体：JSON `{"t":254,"h":601,"lux":123,"dev":"cam1"}`（t/h/lux 为 ×10 原始整数，与旧契约一致）
- 行为：插入 `sensor_data`；返回 `{"ok":true}`，失败返回 500 + 日志

**`POST /api/photo`** —— 图像链
- 请求体：multipart/form-data：文件字段 `image`（JPEG）+ 字段 `t/h/lux/dev`
- 行为：图片落盘（如 `static/photos/YYYYMMDD_HHMMSS.jpg`），路径与快照插 `photo_records` 表
- 返回：`{"ok":true,"path":...}`

建议表结构（沿用现状命名风格）：

```sql
CREATE TABLE sensor_data (
  id INT AUTO_INCREMENT PRIMARY KEY,
  time DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  dev VARCHAR(16) DEFAULT 'cam1',
  temp_x10 INT, humi_x10 INT, lux INT
);
CREATE TABLE photo_records (
  id INT AUTO_INCREMENT PRIMARY KEY,
  time DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  dev VARCHAR(16) DEFAULT 'cam1',
  image_path VARCHAR(255),
  temp_x10 INT, humi_x10 INT, lux INT
);
```

兼容策略：旧 TCP:9000 接收通道**双轨保留**一段时间，对比数据一致后再下线。

## 8. 里程碑与验证（M1–M5）

| # | 内容 | 验证标准 |
|---|---|---|
| M1 | 修 0x105：宏改对、板卡/PSRAM 配对，CameraWebServer 能出图 | 浏览器看到实时画面 |
| M2 | 裸串口打通：STM32 行 → CAM UART2 收到并回显 | 串口对打/回显一致，无乱码 |
| M3 | 数据链：CAM 收到行 → POST /api/data | curl 手测端点 + MySQL 查到新行 |
| M4 | 图像链：手动拍一张 → multipart POST /api/photo | 库中 photo_records 有记录、图片可打开 |
| M5 | 参数化 + 稳定性：间隔常量、WiFi 断线重连、24h 长跑 | 无内存泄漏/任务崩溃/断链堆积，数据连续 |

产出物：可烧录的 Arduino 网关固件（含双任务）、Flask 两个端点示例代码、STM32 侧 UART 桥驱动改动说明。

## 9. 风险与开放项

1. GPIO16/17 引出情况未实测（M1 一并验证；退路见第 4 节）。
2. ESP32-CAM 供电稳定性：需独立 5V；测试期观察 brownout 日志。
3. Windows 热点 IP 固定性：192.168.137.1 为 ICS 网关地址，若换网络需改 `SERVER_HOST` 常量。
4. Flask 端点改动在用户本地代码上进行（我提供示例，用户合并/或按此实现）。
5. STM32 侧改动面：`freertos.c` 上行段 + 新增轻量串口发送驱动；ESP8266 驱动退役（保留裁剪桩策略，
   `CFG_ENABLE_ESP8266=0` 时走空桩，不动调用点）。

## 10. 范围外（YAGNI，本阶段不做）

- 不做自带网页/WebSocket/`/control`/SD 卡存储（图片直接走 WiFi，不落 SD）。
- 不做帧重传/ACK、不做 NTP 校时、不做 OTA、不做双 CAM、不做图像分析。
- 传感器数据仍为 30s 行契约，不做二进制协议升级（未来需要再议）。

## 11. 契约速查

- STM32→CAM 帧：`<t_x10>,<h_x10>,<lux>\n`（ASCII，`\n` 结尾，115200 8N1）
- 数据上报：`POST /api/data` body `{"t":<int>,"h":<int>,"lux":<int>,"dev":"cam1"}`
- 图像上报：`POST /api/photo` body 为 JPEG 原始字节 + query `t/h/lux/dev`（原"multipart"表述作废，见 §12）

## 12. 对接修正（2026-09-09，读取真实 server.py 后）

1. 服务端为现有 `Flask_TCP/server.py`（Flask + MySQL，库 **plant_db**，网页端口 **8080**，TCP:9000 旧通道保留双轨对比）。
2. `sensor_data` 实际列名：`device_id/temp_x10/humi_x10/lux/ts`（非 §7 假设的 dev/time）；已新增 `photo_records` 表（server.py init_db 自动建）与 `/api/data`、`/api/photo`、`/photos/<file>` 三个路由。
3. §7 中"5000 端口独立示例、plant_net 库、multipart"表述作废；CAM 固件 `HTTP_PORT=8080`，`SERVER_HOST` 按热点类型切换（电脑热点 192.168.137.1 / iPhone 热点下电脑 172.20.10.2）。
4. **监控模式 v3（2026-09-09 用户定案）**：摄像头端 = 常开监控，不再自动推图——CAM 提供 `:81/snap`（单帧）与 `:82/stream`（MJPEG 流，独立任务）；Flask 端 `SNAP_INTERVAL_SEC` 定时（默认 300s，0=关）拉 `/snap` 存 `photo_records`，网页嵌入 `:82/stream` 实时画面 + "拍照存库"按钮；数据仍 30s 一报。
