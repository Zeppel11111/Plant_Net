# Plant_Net 移植/配网配置地图

> 用途：换网络环境/换部署位置时，按本表逐项核对修改，避免漏改导致"数据有、图没有"之类错位。
> 三个平面互相独立：公网访问(cpolar) / 设备→Flask(局域网) / WiFi 凭据。

## 0. 统一网络配置（net_config.h）—— 优先读这里

换网/搬家时**只需改一处数据源** `config/net_config.h`（SSID/密码/服务器地址/数据端口/推流端口），然后：
```
python tools\deploy_config.py            # 交互填写 → 自动改源头+同步两份副本
python tools\deploy_config.py --sync     # 记事本直接改了源头后，只做同步
```
脚本会把内容同步到两份**编译副本**（两份必须与源头一致，烧录才生效）：
- `STM32/Core/Inc/net_config.h`（STM32 侧 include "net_config.h"）
- `ESP-CAM/CameraWebServer/net_config.h`（CAM 侧同目录 include）

之后分别重新编译烧录 STM32 与 ESP-CAM 即可。下文第 2/3 节的"逐文件改法"是底层说明，日常以本节的脚本流程为准。

## 1. 当前架构

```
公网浏览器 ──cpolar 隧道──► Flask:8080(本机)   ← 平面①：只与 cpolar 有关
ST ──UART──► ESP8266 ──TCP:9000──► Flask      ← 平面②+③（数据 dev01）
ESP32-CAM  ──WiFi──► TCP:9001 推帧 → Flask     ← 平面②+③（图像）
Flask ──► MySQL(plant_db) ──► 网页 /api/live /api/snap
```

## 2. 配置位置清单（改动点总表）

| 设备/文件 | 配置项 | 含义 | 平面 |
|---|---|---|---|
| `STM32/Core/Src/freertos.c`（STM32） | `NET_SERVER_IP` / `NET_SERVER_PORT(9000)` | Flask 电脑局域网 IP/端口 | ② |
| `config/net_config.h` | `PN_WIFI_SSID` / `PN_WIFI_PASS` | ESP8266 与 CAM 要连的热点 | ③ |
| `ESP-CAM/.../CameraWebServer.ino` | `ssid` / `password` | CAM 要连的热点 | ③ |
| 同上 | `gw_host` / `GW_PUSH_PORT(9001)` | Flask 电脑局域网 IP/推流口 | ② |
| `Flask_TCP/server.py` | `MYSQL_*`；`WEB_PORT 8080`；`TCP_PORT 9000`；`PUSH_PORT 9001`；`SNAP_INTERVAL_SEC` | 服务器自身与节奏 | ②/本机 |
| cpolar 控制台 | 隧道指向 `127.0.0.1:8080` | 公网入口 | ① |

## 3. 常见网络场景要改什么

| 场景 | ESP8266 SSID 表 | freertos `NET_SERVER_IP` | CAM `ssid/password` | CAM `gw_host` | cpolar |
|---|---|---|---|---|---|
| Windows 移动热点 | 填实际 SSID | `192.168.137.1`（固定） | 同左 | `192.168.137.1`（固定） | 不变 |
| iPhone 个人热点 | 改 iPhone 名 | 电脑 IP `172.20.10.2`（ipconfig 查） | 改 iPhone 名 | `172.20.10.2` | 不变 |
| 新路由器(如 192.168.1.x) | 改路由器 SSID | 电脑路由器 IP（如 `192.168.1.105`） | 改路由器 SSID | 电脑路由器 IP | 不变 |
| 纯公网访问（不搬家） | — | — | — | — | 不变 |

规则记忆：**ESP8266 与 CAM 必须连同一个热点，且都能到 Flask 电脑**；它们的"服务器 IP"填的是**电脑在局域网里的 IP**，与 cpolar 公网域名无关。

## 4. 验证清单（移植后 2 分钟）

1. Flask 控制台：`[OK] TCP 9000 / 收流 9001 / 定时截取` 三行都在；
2. STM32 串口：`AT 握手 OK → WiFi 已连接 IP=... → TCP 已连接 <NET_SERVER_IP>:9000`；
3. 网页 `http://<LAN IP>:8080`：数据卡 30s 更新、`/api/live` 有画面、每 60s 一张照片；
4. 外网：cpolar 域名打开同页成功。
