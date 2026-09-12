# ESP-CAM 网关实施计划（UART 桥 + 双链路 HTTP）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 用 ESP32-CAM 网关固件 + STM32 串口桥 + Flask 双端点，替代 ESP8266 完成"传感器数据密报 + 照片低频上传"到 MySQL 的全链路。

**Architecture:** STM32 每 30s 把一行 `温度x10,湿度x10,光照\n` 写 USART3 推给 ESP-CAM（UART2）；ESP-CAM 跑双任务：数据任务逐行 HTTP POST `/api/data`，图像任务每 `PHOTO_INTERVAL_MIN` 分钟拍 JPEG POST `/api/photo`；Flask 落盘图片、路径+读数快照入 MySQL。参考规格：`docs/superpowers/specs/2026-09-08-espcam-gateway-design.md`。

**Tech Stack:** STM32F103 + FreeRTOS（HAL/现有 BSP）、Arduino core for ESP32 + esp32-camera、Flask + PyMySQL、MySQL 8、curl。

**实现层细化（相对设计文档的 1 处小改动，需用户知晓）：** 设计文档写"multipart 上传"；Arduino HTTPClient 无原生 multipart、手工组包易错，改为 **`POST /api/photo` 的 body 直接是 JPEG 原始字节，读数快照放 query 参数**（`?t=&h=&lux=&dev=`）。语义不变（图 + 读数快照 + 设备号），Flask 侧解析更简单可靠。

**实际对接修正（2026-09-09 读真实 `Flask_TCP/server.py` 后）：** 服务端**不做独立示例**，直接改现有 `Flask_TCP/server.py` 集成：库 `plant_db`、表沿用 `sensor_data(device_id,temp_x10,humi_x10,lux,ts)`、**端口 8080**（网页同口，CAM 固件 `HTTP_PORT=8080`）；已新增 `photo_records` 表（init_db 自动建）+ `/api/data`、`/api/photo`、`/photos/<file>` 三个路由；TCP:9000 旧通道保留双轨。T4 中"新建 server/schema.sql、server/espcam_api.py、curl 打 5000"的步骤以本修正为准，改为直接跑 `server.py` 后 curl 打 **8080**。

---

## 文件结构（本计划创建/修改的文件）

| 文件 | 职责 | 归属 |
|---|---|---|
| `app_config.h`（改 1 行） | `CFG_ENABLE_ESP8266 1→0`，ESP8266 驱动退役走空桩 | STM32 |
| `Core/Src/freertos.c`（改 3 段） | 上行改为直写 USART3 推给 ESP-CAM | STM32 |
| `firmware/esp_cam_gateway/esp_cam_gateway.ino`（新建） | ESP-CAM 网关固件（GW_STAGE 1/2/3 渐进启用） | ESP32-CAM |
| `server/schema.sql`（新建） | sensor_data / photo_records 建表 | 服务器 |
| `server/espcam_api.py`（新建） | Flask 独立示例：两个端点，先跑通再并入现有项目 | 服务器 |

> STM32 侧在**本工作区**可直接编辑并编译验证；`.ino` 与 `server/` 文件是交付物，由用户拷到自己的 Arduino/Flask 环境使用（T3/T4/T5 需要真机/真服务器，本工作区无法代跑，步骤以人工验证+期望输出形式给出）。

---

### Task 1: M1 —— 修复 CameraWebServer 0x105 并出图（用户 Arduino 环境）

**Files:** 用户机器上的 `CameraWebServer.ino`（esp32-camera 例程副本）——本工作区无此文件，以下编辑由用户在 Arduino IDE 完成。

- [ ] **Step 1: 注释掉 ESP_EYE 宏**

找到 .ino 顶部板型区，把 `#define CAMERA_MODEL_ESP_EYE  // Has PSRAM` 改为 `//#define CAMERA_MODEL_ESP_EYE  // Has PSRAM`，确认 `#define CAMERA_MODEL_AI_THINKER  // Has PSRAM` **是唯一未被注释的型号**。根因：两宏同时开启时 `camera_pins.h` 的 `#if/#elif` 链先命中 ESP_EYE → `pin_pwdn=-1` → AI Thinker 板上 OV2640(PWDN=GPIO32) 掉电 → SCCB 探测失败 0x105。

- [ ] **Step 2: IDE 板卡/PSRAM 配对**

Tools → Board：`AI Thinker ESP32-CAM`（无则选带 PSRAM 的 Wrover 型号）；Tools → PSRAM：`Enabled`；Flash Size：`4MB`；Partition Scheme：`Huge APP (3MB APP/1MB SPIFFS)`。波特率 115200。

- [ ] **Step 3: 烧录并验证**

打开串口监视器，观察重启后日志。**期望**：不再出现 `Camera probe failed with error 0x105`；出现 `WiFi connected`、`Camera Ready! Use 'http://<IP>'`；浏览器打开该 IP 能看到实时画面。
若仍 0x105：进入硬件排查清单（排线重插→独立 5V/1A 供电→串口扫描 sketch 找 0x30），不要改代码。

- [ ] **Step 4: 记录结论**

把最终串口日志（含 IP 行）与是否出图的结论贴回会话。**M1 完成标准 = 浏览器出图。**

---

### Task 2: M2a —— STM32 上行改为直写 USART3（本工作区可编译验证）

**Files:**
- Modify: `app_config.h:29`
- Modify: `Core/Src/freertos.c`（L32 include、L42-49 删除、L134 删变量、L143-191 替换、L213-226 替换）

- [ ] **Step 1: 关闭 ESP8266 驱动编译**

`app_config.h` 第 29 行：
```c
#define CFG_ENABLE_ESP8266      1   /* ESP8266 WiFi 模块（USART3 → AT 指令，115200） */
```
改为：
```c
#define CFG_ENABLE_ESP8266      0   /* ESP8266 退役：WiFi 职责移交 ESP-CAM（USART3 → 直发行文本） */
```
（`ESP8266.h` 的空桩自动生效，调用点若残留也能编译通过。）

- [ ] **Step 2: freertos.c —— 换 include**

`Core/Src/freertos.c` 第 28-33 行，把
```c
#include <stdio.h>
#include "app_config.h"   /* 模块总开关：必须先于各模块头文件 */
#include "Debug.h"
#include "TAS_GZ.h"
#include "ESP8266.h"
```
改为
```c
#include <stdio.h>
#include "app_config.h"   /* 模块总开关：必须先于各模块头文件 */
#include "Debug.h"
#include "TAS_GZ.h"
#include "bsp_uart.h"     /* USART3 直发 ESP-CAM */
```

- [ ] **Step 3: freertos.c —— 删 NET_SERVER 定义**

删除第 42-49 行整段（含 `/* 【部署配置】…*/` 注释与两个 `#define NET_SERVER_*`）。服务器地址从此只存在于 ESP-CAM 固件 `SERVER_HOST` 常量。

- [ ] **Step 4: freertos.c —— 删 tcp_ok 变量**

第 134 行 `int tcp_ok = 0;` 删除（其注释同删）。

- [ ] **Step 5: freertos.c —— 替换 ESP8266 初始化/联网段**

第 143-191 行整段（从 `/* ESP8266 探活：…*/` 到 TCP 连接失败打印的 `}` 为止）替换为：
```c
  /* ESP-CAM 网关串口初始化：USART3 现在直连 ESP32-CAM 的 UART2（115200 8N1）。
     WiFi/上传由 CAM 负责，STM32 不再管 AT/TCP——只负责把采样行写出去 */
  if (bsp_uart_init(BSP_UART3) != 0)
  {
    printf("[E][ESP-CAM] USART3 初始化失败\n");
  }
```

- [ ] **Step 6: freertos.c —— 替换上行发送段**

第 213-226 行整段（`/* 上行：拼一行数据经 WiFi 发给服务器…*/` 的 `{ char uplink[32]; … }` 块）替换为：
```c
      /* 上行：拼一行数据直发 ESP-CAM（USART3）。行格式 = 数据契约，CAM 按 \n 转发。
         不重试不 ACK：下一行 30s 后自然补上（有意取舍，见设计文档 §5） */
      {
        char uplink[32];
        int  n = snprintf(uplink, sizeof(uplink), "%d,%d,%d\n",
                          (int)tas.temp_x10, (int)tas.humi_x10, (int)tas.lux);
        if (bsp_uart_send(BSP_UART3, (uint8_t*)uplink, (uint16_t)n, 100,
                          BSP_UART_MODE_POLLING) != 0)
        {
          printf("[E][ESP-CAM] USART3 发送失败\n");
        }
      }
```

- [ ] **Step 7: 编译验证**

Run: `cmake --build build/Debug`
Expected: 编译通过，无 `esp8266_*`/`NET_SERVER` 未定义错误（若用 STM32CubeIDE 则在 IDE 内 Build）。出现 newlib `_printf_float` 类既有警告可忽略。

- [ ] **Step 8: Commit**

```bash
git add app_config.h Core/Src/freertos.c
git commit -m "feat(stm32): 上行改 USART3 直发 ESP-CAM，ESP8266 驱动退役(CFG=0)"
```

---

### Task 3: M2b —— ESP-CAM 网关固件（GW_STAGE=1 串口桥回显）

**Files:**
- Create: `firmware/esp_cam_gateway/esp_cam_gateway.ino`

- [ ] **Step 1: 写入完整固件（含 1/2/3 三阶段代码，默认 STAGE 1）**

创建 `firmware/esp_cam_gateway/esp_cam_gateway.ino`，内容如下（AI Thinker 引脚**硬编码**，刻意不用 camera_pins.h/板型宏，从根上杜绝"双宏 0x105"一类问题）：

```cpp
/* =====================================================================
 * ESP-CAM 网关固件 —— 数据桥(密) + 定时拍照(疏) 上传 Flask
 * 目标板：AI Thinker ESP32-CAM（OV2640）。Arduino core for ESP32。
 *
 * GW_STAGE 渐进启用（默认 1）：
 *   1 = 串口桥回显（STM32 行 → UART0 日志，验证接线；不连 WiFi 不拍照）
 *   2 = + 数据链：每收到一行 POST /api/data（先跑通密报）
 *   3 = + 图像链：每 PHOTO_INTERVAL_MIN 分钟拍 JPEG POST /api/photo
 * ===================================================================== */
#define GW_STAGE 1

/* ---------- 板级参数（AI Thinker ESP32-CAM，硬编码引脚，与官方 camera_pins.h 一致） ---------- */
#define PIN_PWDN   32
#define PIN_RESET  -1
#define PIN_XCLK   0
#define PIN_SIOD   26
#define PIN_SIOC   27
#define PIN_D7     35
#define PIN_D6     34
#define PIN_D5     39
#define PIN_D4     36
#define PIN_D3     21
#define PIN_D2     19
#define PIN_D1     18
#define PIN_D0     5
#define PIN_VSYNC  25
#define PIN_HREF   23
#define PIN_PCLK   22

/* ---------- 网络与业务参数 ---------- */
#define WIFI_SSID          "YOUR_WIFI_SSID"
#define WIFI_PASS          "YOUR_WIFI_PASSWORD"
#define SERVER_HOST        "192.168.137.1"
#define HTTP_PORT          8080    /* Flask_TCP/server.py 同端口（2026-09-09 修正） */
#define DEVICE_ID          "cam1"
#define PHOTO_INTERVAL_MIN 10     /* 图像疏：默认 10 分钟/张 */
#define PHOTO_QUALITY      12
#define UART2_RX           16     /* ← STM32 USART3 TX(PB10) 接这里 */
#define UART2_TX           17     /* → STM32 USART3 RX(PB11)（可暂不接） */

#include <WiFi.h>
#include <HTTPClient.h>
#include "esp_camera.h"

static volatile int g_t = -1, g_h = -1, g_lux = -1;   /* 最新读数（×10 整数） */
static uint32_t s_lastReconnect = 0;

/* ---------- WiFi 保持 ---------- */
static bool wifiEnsure()
{
  if (WiFi.status() == WL_CONNECTED) return true;
  if (millis() - s_lastReconnect < 5000) return false;   /* 5s 冷却防抖 */
  s_lastReconnect = millis();
  WiFi.reconnect();
  return WiFi.status() == WL_CONNECTED;
}

static void wifiInit()
{
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[GW] WiFi 连接 %s ...\n", WIFI_SSID);
}

/* ---------- 摄像头 ---------- */
static bool cameraInit()
{
  camera_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.ledc_channel = LEDC_CHANNEL_0;
  cfg.ledc_timer   = LEDC_TIMER_0;
  cfg.pin_d0 = PIN_D0;  cfg.pin_d1 = PIN_D1;  cfg.pin_d2 = PIN_D2;
  cfg.pin_d3 = PIN_D3;  cfg.pin_d4 = PIN_D4;  cfg.pin_d5 = PIN_D5;
  cfg.pin_d6 = PIN_D6;  cfg.pin_d7 = PIN_D7;
  cfg.pin_xclk = PIN_XCLK;  cfg.pin_pclk = PIN_PCLK;
  cfg.pin_vsync = PIN_VSYNC; cfg.pin_href = PIN_HREF;
  cfg.pin_sccb_sda = PIN_SIOD; cfg.pin_sccb_scl = PIN_SIOC;
  cfg.pin_pwdn = PIN_PWDN;  cfg.pin_reset = PIN_RESET;
  cfg.xclk_freq_hz = 20000000;
  cfg.pixel_format = PIXFORMAT_JPEG;
  cfg.frame_size   = FRAMESIZE_QVGA;          /* 测试期小图，省带宽 */
  cfg.jpeg_quality = PHOTO_QUALITY;
  cfg.fb_count     = 1;
  cfg.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;
  cfg.fb_location  = CAMERA_FB_IN_PSRAM;

  esp_err_t err = esp_camera_init(&cfg);
  if (err != ESP_OK) {
    Serial.printf("[GW] 摄像头初始化失败 0x%x（查排线/供电/PWDN=GPIO32）\n", err);
    return false;
  }
  sensor_t *s = esp_camera_sensor_get();
  if (s) s->set_framesize(s, FRAMESIZE_QVGA);
  Serial.println("[GW] 摄像头 OK");
  return true;
}

/* ---------- HTTP 上报 ---------- */
static bool postData(int t, int h, int lux)
{
  char url[160], body[96];
  snprintf(url, sizeof(url), "http://%s:%d/api/data", SERVER_HOST, HTTP_PORT);
  snprintf(body, sizeof(body), "{\"t\":%d,\"h\":%d,\"lux\":%d,\"dev\":\"%s\"}",
           t, h, lux, DEVICE_ID);
  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST((uint8_t*)body, strlen(body));
  bool ok = (code == 200);
  Serial.printf("[GW] data POST -> %d\n", code);
  http.end();
  return ok;
}

static bool postPhoto(camera_fb_t *fb, int t, int h, int lux)
{
  char url[200];
  snprintf(url, sizeof(url),
           "http://%s:%d/api/photo?t=%d&h=%d&lux=%d&dev=%s",
           SERVER_HOST, HTTP_PORT, t, h, lux, DEVICE_ID);
  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "image/jpeg");
  int code = http.POST(fb->buf, fb->len);   /* body 即 JPEG 原始字节 */
  bool ok = (code == 200);
  Serial.printf("[GW] photo POST (%u B) -> %d\n", fb->len, code);
  http.end();
  return ok;
}

/* ---------- 任务1：数据链（串口收行 → 回显/上报） ---------- */
static void dataTask(void*)
{
  static char line[128];
  size_t n = 0;
  for (;;) {
    while (Serial2.available()) {
      char c = (char)Serial2.read();
      if (c == '\n' || n >= sizeof(line) - 1) {
        line[n] = '\0';
        n = 0;
        Serial.printf("[GW] 收行: %s\n", line);       /* 回显：M2 验证点 */
        int t = -1, h = -1, l = -1;
        if (sscanf(line, "%d,%d,%d", &t, &h, &l) == 3) {
          g_t = t; g_h = h; g_lux = l;                 /* 快照供拍照任务用 */
#if GW_STAGE >= 2
          if (wifiEnsure()) postData(t, h, l);
          else              Serial.println("[GW] WiFi 未就绪，跳过上报");
#endif
        } else {
          Serial.printf("[GW] 坏帧丢弃: %s\n", line);
        }
      } else {
        line[n++] = c;
      }
    }
    vTaskDelay(20);
  }
}

/* ---------- 任务2：图像链（周期拍照上传） ---------- */
static void photoTask(void*)
{
  uint32_t lastShot = 0;
  for (;;) {
    uint32_t intervalMs = (uint32_t)PHOTO_INTERVAL_MIN * 60000UL;
    if (millis() - lastShot >= intervalMs) {
      lastShot = millis();
      camera_fb_t *fb = esp_camera_fb_get();
      if (!fb) { Serial.println("[GW] 拍照失败"); continue; }
      if (wifiEnsure()) postPhoto(fb, g_t, g_h, g_lux);
      esp_camera_fb_return(fb);
    }
    vTaskDelay(1000);
  }
}

/* ---------- 入口 ---------- */
void setup()
{
  Serial.begin(115200);
  delay(300);
  Serial2.begin(115200, SERIAL_8N1, UART2_RX, UART2_TX);
  Serial.printf("[GW] 启动 stage=%d，UART2 115200 8N1 (RX=GPIO%d)\n", GW_STAGE, UART2_RX);

#if GW_STAGE >= 2
  wifiInit();
#endif
#if GW_STAGE >= 3
  bool camOk = cameraInit();
  if (!camOk) Serial.println("[GW] 摄像头不可用：仅保留数据链");
#endif

  xTaskCreatePinnedToCore(dataTask,  "data",  4096, NULL, 1, NULL, 0);
#if GW_STAGE >= 3
  if (camOk) xTaskCreatePinnedToCore(photoTask, "photo", 4096, NULL, 1, NULL, 1);
#endif
}

void loop() { vTaskDelay(60000); }
```

- [ ] **Step 2: 用户烧录（GW_STAGE=1）并验证串口桥**

用户操作：IDE 选 AI Thinker ESP32-CAM（PSRAM Enabled）→ 烧录 → 打开 115200 监视器；同时给 STM32 上电。
**期望输出**：每 30s 出现 `[GW] 收行: 254,601,12345`（数值=STM32 实际读数）；无乱码、无 `坏帧`。
若持续无行：查 USART3 波特率/接线（PB10→GPIO16 直连、共地）；用 USB-TTL 单独发一行 `1,2,3\n` 到 GPIO16 验证 CAM 收端。

- [ ] **Step 3: Commit**

```bash
git add firmware/esp_cam_gateway/esp_cam_gateway.ino
git commit -m "feat(esp-cam): 网关固件 v1 框架 + UART2 串口桥（stage1 回显）"
```

---

### Task 4: M3 —— 数据链打通（固件 stage2 + Flask /api/data + MySQL）

**Files:**
- Modify: `firmware/esp_cam_gateway/esp_cam_gateway.ino`（`#define GW_STAGE 1` → `2`）
- Create: `server/schema.sql`
- Create: `server/espcam_api.py`

- [ ] **Step 1: 固件开 stage2**

`esp_cam_gateway.ino` 顶部 `#define GW_STAGE 1` 改为 `#define GW_STAGE 2`。

- [ ] **Step 2: 建表 SQL**

创建 `server/schema.sql`：
```sql
CREATE DATABASE IF NOT EXISTS plant_net CHARACTER SET utf8mb4;
USE plant_net;

CREATE TABLE IF NOT EXISTS sensor_data (
  id INT AUTO_INCREMENT PRIMARY KEY,
  time DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  dev VARCHAR(16) NOT NULL DEFAULT 'cam1',
  temp_x10 INT NOT NULL,
  humi_x10 INT NOT NULL,
  lux INT NOT NULL
);

CREATE TABLE IF NOT EXISTS photo_records (
  id INT AUTO_INCREMENT PRIMARY KEY,
  time DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  dev VARCHAR(16) NOT NULL DEFAULT 'cam1',
  image_path VARCHAR(255) NOT NULL,
  temp_x10 INT NOT NULL DEFAULT -1,
  humi_x10 INT NOT NULL DEFAULT -1,
  lux INT NOT NULL DEFAULT -1
);
```

- [ ] **Step 3: Flask 示例（含两个端点，独立可跑，先验证再并入现有项目）**

创建 `server/espcam_api.py`：
```python
# -*- coding: utf-8 -*-
"""ESP-CAM 网关接收端示例（先独立跑通，再并入现有 Flask 项目）。
运行:  pip install flask pymysql
       python espcam_api.py        (默认 0.0.0.0:5000)
"""
import os
import datetime
import pymysql
from flask import Flask, request, jsonify

app = Flask(__name__)
PHOTO_DIR = os.path.join(os.path.dirname(__file__), "photos")
os.makedirs(PHOTO_DIR, exist_ok=True)

DB = dict(host="127.0.0.1", user="root", password="你的密码",
          database="plant_net", charset="utf8mb4")


def db():
    return pymysql.connect(**DB)


@app.route("/api/data", methods=["POST"])
def api_data():
    """数据链：JSON {'t','h','lux','dev'}，逐条入库（t/h/lux 为 ×10 整数）"""
    j = request.get_json(force=True, silent=True) or {}
    t, h, lux = j.get("t"), j.get("h"), j.get("lux")
    dev = j.get("dev", "cam1")
    if t is None or h is None or lux is None:
        return jsonify(ok=False, err="missing t/h/lux"), 400
    conn = db()
    try:
        with conn.cursor() as cur:
            cur.execute(
                "INSERT INTO sensor_data(dev,temp_x10,humi_x10,lux) VALUES(%s,%s,%s,%s)",
                (dev, int(t), int(h), int(lux)))
        conn.commit()
    finally:
        conn.close()
    return jsonify(ok=True)


@app.route("/api/photo", methods=["POST"])
def api_photo():
    """图像链：body 为 JPEG 原始字节；query: t/h/lux/dev。图片落盘，路径入库"""
    jpeg = request.get_data()
    if not jpeg:
        return jsonify(ok=False, err="empty body"), 400
    dev = request.args.get("dev", "cam1")
    t = request.args.get("t", "-1")
    h = request.args.get("h", "-1")
    lux = request.args.get("lux", "-1")
    name = datetime.datetime.now().strftime("%Y%m%d_%H%M%S") + ".jpg"
    path = os.path.join(PHOTO_DIR, name)
    with open(path, "wb") as f:
        f.write(jpeg)
    rel = "photos/" + name
    conn = db()
    try:
        with conn.cursor() as cur:
            cur.execute(
                "INSERT INTO photo_records(dev,image_path,temp_x10,humi_x10,lux) "
                "VALUES(%s,%s,%s,%s,%s)",
                (dev, rel, int(t), int(h), int(lux)))
        conn.commit()
    finally:
        conn.close()
    return jsonify(ok=True, path=rel)


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000)
```
> 提示：并入现有项目时把两个 `@app.route` 函数拷进去即可；`DB` 字典按你现有 Flask 的 MySQL 配置替换（或复用其连接池）。测试前先 `mysql -uroot -p < server/schema.sql` 建库建表。

- [ ] **Step 4: curl 手测 /api/data**

Run:
```bash
mysql -uroot -p < server/schema.sql
python server/espcam_api.py            # 窗口1，先改好 DB 密码
curl -s -X POST http://192.168.137.1:5000/api/data \
     -H "Content-Type: application/json" \
     -d '{"t":254,"h":601,"lux":123,"dev":"cam1"}'
```
Expected: `{"ok":true}`；随后
```bash
mysql -uroot -p -e "USE plant_net; SELECT * FROM sensor_data ORDER BY id DESC LIMIT 3;"
```
能看到刚插入的行（temp_x10=254 …）。失败排查：Flask 窗口报错/防火墙放行 5000 端口。

- [ ] **Step 5: 真机联调**

STM32 + 固件(stage2) 上电运行 ≥2 分钟。
**期望**：CAM UART0 日志每 30s `data POST -> 200`；MySQL `sensor_data` 行数持续增长（约 2 行/分钟），数值与 STM32 打印一致。偶发失败（POST 非 200/超时）≤ 每 5 分钟 1 次，且下一行自动恢复。

- [ ] **Step 6: Commit**

```bash
git add firmware/esp_cam_gateway/esp_cam_gateway.ino server/schema.sql server/espcam_api.py
git commit -m "feat: 数据链 M3 —— 固件 stage2 + Flask /api/data + 建表SQL"
```

---

### Task 5: M4 —— 图像链打通（固件 stage3 + /api/photo）

**Files:**
- Modify: `firmware/esp_cam_gateway/esp_cam_gateway.ino`（`#define GW_STAGE 2` → `3`）

- [ ] **Step 1: 固件开 stage3**

`#define GW_STAGE 2` 改为 `#define GW_STAGE 3`。

- [ ] **Step 2: curl 手测 /api/photo**

先造一张测试图并上传：
```bash
# 任选：用 CAM 之前在浏览器存的图，或生成纯色 jpg（python 一行）
python -c "import struct;d=open('t.jpg','wb');d.write(bytes.fromhex('ffd8ffdb00430001010101010101010101ffc0000b080001000101011100ffd9'));d.close()"
curl -s -X POST "http://192.168.137.1:5000/api/photo?t=254&h=601&lux=123&dev=cam1" \
     -H "Content-Type: image/jpeg" --data-binary @t.jpg
mysql -uroot -p -e "USE plant_net; SELECT id,dev,image_path,temp_x10 FROM photo_records ORDER BY id DESC LIMIT 3;"
```
Expected: `{"ok":true,"path":"photos/....jpg"}`，库里出现记录且 `server/photos/` 下能看到同名文件（用图片查看器打开确认非空）。

- [ ] **Step 3: 真机联调（关键：PWDN 上电序列）**

用户操作：给 **ESP-CAM 单独 5V 供电**（独立 USB/电源，勿用 STM32 3.3V）；等待第一个拍照周期（≤ PHOTO_INTERVAL_MIN 分钟）。
**期望**：日志 `photo POST (N B) -> 200`；`photo_records` 每周期 +1 行，图片可打开且能看到植物/场景。
若日志出现摄像头初始化失败：查排线/供电；若 `拍照失败`：查 PSRAM 是否 Enabled（板卡选 AI Thinker 时默认开）。

- [ ] **Step 4: 调 PHOTO_INTERVAL_MIN**

按测试节奏把间隔调小（如临时 `#define PHOTO_INTERVAL_MIN 1`）验证周期逻辑，确认无误后改回目标值（默认 10）。

- [ ] **Step 5: Commit**

```bash
git add firmware/esp_cam_gateway/esp_cam_gateway.ino
git commit -m "feat(esp-cam): 图像链 M4 —— 固件 stage3 定时拍照上传"
```

---

### Task 6: M5 —— 参数化复核 + 断线重连 + 24h 稳定性

**Files:** 无代码新增（复用 T3 固件的 `wifiEnsure`/冷却与任务隔离）；本任务是验证与记录。

- [ ] **Step 1: 参数化复核**

确认以下均为单点常量、改后无需动逻辑：`PHOTO_INTERVAL_MIN`（图像疏）、STM32 侧 `vTaskDelayUntil(…, 30000)`（数据密）、`SERVER_HOST`/`HTTP_PORT`/`DEVICE_ID`。检查 `esp_cam_gateway.ino` 中无散落的魔法数字影响节奏。

- [ ] **Step 2: 断线重连演练**

运行中关掉 PC 热点 60s 再开。
**期望**：期间日志出现 `WiFi 未就绪，跳过上报`/POST 失败但不崩溃；热点恢复 ≤10s 内自动恢复 `POST -> 200`，任务不重启、FreeRTOS 无看门狗复位。若 `WiFi.reconnect()` 恢复慢，把 `wifiEnsure()` 中 `WiFi.reconnect()` 改为 `WiFi.disconnect(); WiFi.begin(...)` 组合（记录在 commit message 里）。

- [ ] **Step 3: 24h 稳定性长跑**

STM32+CAM 全链路跑 24h（数据 30s、照片 10min）。**验收**：
- 日志无栈溢出/`Guru Meditation`/持续 `坏帧`；FreeRTOS 栈钩子未触发；
- `sensor_data` 行数 ≈ 2880±容忍偏差（30s×24h=2880），无 >10 分钟空洞；
- `photo_records` ≈ 144 行，`server/photos/` 文件数与行数一致且均可打开；
- CAM 无内存增长迹象（每周期打印的 `fb->len` 波动正常即可）。

- [ ] **Step 4: 记录结果并提交**

把长跑结论（行数、空洞次数、异常截图/日志片段）写入 `docs/superpowers/specs/2026-09-08-espcam-gateway-design.md` 末尾"验证记录"一节或新文件 `docs/superpowers/plans/2026-09-08-espcam-gateway-verification.md`。

```bash
git add -A
git commit -m "docs: M5 24h 稳定性验证记录"
```

---

## 验收总览（M1–M5 与规格映射）

| 规格 § | 验收 | 覆盖任务 |
|---|---|---|
| §6 前置修复（0x105） | 浏览器出图 | T1 |
| §4/§5 接线与帧协议 | STM32 行 30s 准时出现在 CAM 日志 | T2、T3 |
| §6 数据任务 | 逐行 POST 200 + MySQL 逐行增长 | T4 |
| §6 图像任务 | 周期 multipart(等价 raw)上传 + photo_records 增长 | T5 |
| §7 Flask/MySQL | 双端点 + 双表 + 落盘路径 | T4、T5 |
| §8 M5 | 断线自愈 + 24h 行数/空洞达标 | T6 |
