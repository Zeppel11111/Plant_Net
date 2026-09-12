# Hotspot Gateway Discovery Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make ESP8266 and ESP32-CAM discover the Flask server from the Windows hotspot default gateway, while retaining `PN_SERVER_HOST` as a fallback.

**Architecture:** Keep the current two-uplink hardware architecture. STM32 queries the ESP8266 STA configuration with `AT+CIPSTA?`, parses and drains the complete response, then tries the discovered gateway before the configured fallback; ESP32-CAM uses `WiFi.gatewayIP()` with the same candidate order. Both paths re-evaluate Wi-Fi state and gateway address on every reconnect cycle.

**Tech Stack:** STM32F103, FreeRTOS/CMSIS-RTOS2, ESP8266 ESP-AT v1.5.4.1, C11, ESP32 Arduino `WiFi`/`WiFiClient`, CMake/Ninja, Arduino IDE or `arduino-cli`.

**Design:** `docs/superpowers/specs/2026-09-11-hotspot-gateway-discovery-design.md`

---

## File map

- Modify `.gitignore`: track the ESP32-CAM firmware files required to reproduce the project while continuing to ignore the large reference document.
- Create `STM32/Driver/ESP8266_AT_Parse.h`: pure AT-line parser interface, with no HAL/BSP dependency.
- Create `STM32/Driver/ESP8266_AT_Parse.c`: extract a quoted gateway IPv4 value from one `+CIPSTA:gateway:` response line.
- Create `STM32/tests/CMakeLists.txt`: native unit-test target for the pure parser.
- Create `STM32/tests/test_esp8266_at_parse.c`: parser success and failure vectors.
- Modify `STM32/Driver/ESP8266.h`: declare `esp8266_get_gateway()` and its disabled-module stub.
- Modify `STM32/Driver/ESP8266.c`: query and fully drain `AT+CIPSTA?`; also drain the existing `AT+CIFSR` query through its final `OK` so stale responses cannot satisfy the next command.
- Modify `STM32/App/Src/app_uplink.c`: reconnect Wi-Fi when gateway discovery proves STA is offline; try gateway and fallback without duplicate attempts.
- Modify `ESP-CAM/CameraWebServer/CameraWebServer.ino`: bounded Wi-Fi recovery and runtime gateway/fallback selection.
- Modify `README.md`: document the no-reflash hotspot workflow.
- Modify `docs/deployment-config-map.md`: document address selection and现场 verification logs.

## Preconditions

- Keep the server binding unchanged: `server.py` already binds TCP 9000, TCP 9001 and Flask 8080 to `0.0.0.0`.
- Keep hotspot SSID/password unchanged in this feature; only server address discovery changes.
- Before hardware tests, start `Flask_TCP/server.py` and enable the Windows mobile hotspot used by both boards.
- Run all Git commands from `D:\STM32_CubeMAX\project\Plant_Net`.

### Task 1: Put the ESP32-CAM firmware under version control

**Files:**
- Modify: `.gitignore:5-11`

- [ ] **Step 1: Record the current failing repository check**

Run:

```powershell
git check-ignore -v ESP-CAM/CameraWebServer/CameraWebServer.ino
```

Expected: output identifies `.gitignore` line `ESP-CAM/`, proving the firmware is excluded.

- [ ] **Step 2: Narrow the ignore rule to reference material only**

Replace the `ESP-CAM/` rule with exact allow rules:

```gitignore
# ESP-CAM 厂商说明文档不入库；实际固件必须入库，便于换电脑复现
ESP-CAM/*
!ESP-CAM/CameraWebServer/
ESP-CAM/CameraWebServer/*
!ESP-CAM/CameraWebServer/CameraWebServer.ino
!ESP-CAM/CameraWebServer/app_httpd.cpp
!ESP-CAM/CameraWebServer/camera_index.h
!ESP-CAM/CameraWebServer/camera_pins.h
!ESP-CAM/CameraWebServer/net_config.h
!ESP-CAM/CameraWebServer/partitions.csv
```

Do not alter the `Flask_TCP/` rule in this feature because no server file changes are required.

- [ ] **Step 3: Verify only firmware files become visible**

Run:

```powershell
git check-ignore ESP-CAM/CameraWebServer/CameraWebServer.ino
git status --short -- ESP-CAM
```

Expected: the first command prints nothing; the second lists `ESP-CAM/CameraWebServer/` and does not list the `.docx` reference file.

- [ ] **Step 4: Commit the repository fix and current firmware baseline**

```powershell
git add .gitignore ESP-CAM/CameraWebServer
git commit -m "build: track esp32-cam firmware"
```

Expected: one commit containing `.gitignore` and the six firmware files, with no photos, logs or `.docx` file.

### Task 2: Build a pure, tested ESP-AT gateway-line parser

**Files:**
- Create: `STM32/Driver/ESP8266_AT_Parse.h`
- Create: `STM32/Driver/ESP8266_AT_Parse.c`
- Create: `STM32/tests/CMakeLists.txt`
- Create: `STM32/tests/test_esp8266_at_parse.c`

- [ ] **Step 1: Write the native test target**

Create `STM32/tests/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(plant_net_native_tests C)

enable_testing()

add_executable(test_esp8266_at_parse
    test_esp8266_at_parse.c
    ../Driver/ESP8266_AT_Parse.c
)
target_include_directories(test_esp8266_at_parse PRIVATE ../Driver)
target_compile_features(test_esp8266_at_parse PRIVATE c_std_11)

if(MSVC)
    target_compile_options(test_esp8266_at_parse PRIVATE /W4 /WX)
else()
    target_compile_options(test_esp8266_at_parse PRIVATE -Wall -Wextra -Werror)
endif()

add_test(NAME esp8266_at_parse COMMAND test_esp8266_at_parse)
```

Create `STM32/tests/test_esp8266_at_parse.c`:

```c
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "ESP8266_AT_Parse.h"

static void expect_ok(const char *line, const char *expected)
{
    char out[32] = "unchanged";
    assert(esp8266_at_parse_gateway(line, out, sizeof(out)) == 0);
    assert(strcmp(out, expected) == 0);
}

int main(void)
{
    char out[32] = "sentinel";
    char small[8] = "keep";

    expect_ok("+CIPSTA:gateway:\"192.168.137.1\"\r\n", "192.168.137.1");
    expect_ok("+CIPSTA:gateway:\"192.168.173.1\"", "192.168.173.1");

    assert(esp8266_at_parse_gateway("+CIPSTA:ip:\"192.168.137.20\"",
                                    out, sizeof(out)) == -1);
    assert(strcmp(out, "sentinel") == 0);

    assert(esp8266_at_parse_gateway("+CIPSTA:gateway:192.168.137.1",
                                    out, sizeof(out)) == -2);
    assert(esp8266_at_parse_gateway("+CIPSTA:gateway:\"192.168.137.1\"",
                                    small, sizeof(small)) == -3);
    assert(strcmp(small, "keep") == 0);

    assert(esp8266_at_parse_gateway(NULL, out, sizeof(out)) == -2);
    assert(esp8266_at_parse_gateway("+CIPSTA:gateway:\"1.2.3.4\"",
                                    NULL, sizeof(out)) == -2);
    assert(esp8266_at_parse_gateway("+CIPSTA:gateway:\"1.2.3.4\"",
                                    out, 0) == -2);
    return 0;
}
```

- [ ] **Step 2: Run the tests and verify the missing parser fails the build**

Run with a native desktop C compiler, not the ARM toolchain:

```powershell
cmake -S STM32/tests -B STM32/build/native-tests -G Ninja
cmake --build STM32/build/native-tests
```

Expected: FAIL because `ESP8266_AT_Parse.h` or `ESP8266_AT_Parse.c` does not exist.

- [ ] **Step 3: Add the parser interface**

Create `STM32/Driver/ESP8266_AT_Parse.h`:

```c
#ifndef ESP8266_AT_PARSE_H
#define ESP8266_AT_PARSE_H

#include <stdint.h>

/* Parse one +CIPSTA:gateway:"x.x.x.x" response line.
 *  0 success
 * -1 line is not a gateway line
 * -2 invalid arguments or malformed quoted value
 * -3 output buffer is too small
 * On failure, out is not modified. */
int esp8266_at_parse_gateway(const char *line, char *out, uint16_t out_len);

#endif
```

- [ ] **Step 4: Implement the minimal parser**

Create `STM32/Driver/ESP8266_AT_Parse.c`:

```c
#include <stddef.h>
#include <string.h>
#include "ESP8266_AT_Parse.h"

int esp8266_at_parse_gateway(const char *line, char *out, uint16_t out_len)
{
    static const char marker[] = "+CIPSTA:gateway:";
    const char *field;
    const char *begin;
    const char *end;
    size_t len;

    if (line == NULL || out == NULL || out_len == 0u) {
        return -2;
    }

    field = strstr(line, marker);
    if (field == NULL) {
        return -1;
    }

    begin = strchr(field + sizeof(marker) - 1u, '"');
    if (begin == NULL) {
        return -2;
    }
    begin++;
    end = strchr(begin, '"');
    if (end == NULL || end == begin) {
        return -2;
    }

    len = (size_t)(end - begin);
    if (len >= out_len) {
        return -3;
    }

    memcpy(out, begin, len);
    out[len] = '\0';
    return 0;
}
```

- [ ] **Step 5: Run the parser tests**

```powershell
cmake --build STM32/build/native-tests
ctest --test-dir STM32/build/native-tests --output-on-failure
```

Expected: `100% tests passed, 0 tests failed`.

- [ ] **Step 6: Verify the parser also compiles in the firmware build**

```powershell
cmake --build --preset Debug
```

Expected: `Plant_Net.elf` links successfully and memory use remains below 20 KB RAM and 64 KB Flash.

- [ ] **Step 7: Commit the parser and tests**

```powershell
git add STM32/Driver/ESP8266_AT_Parse.h STM32/Driver/ESP8266_AT_Parse.c STM32/tests
git commit -m "test: add esp-at gateway parser"
```

### Task 3: Add a complete `AT+CIPSTA?` gateway query

**Files:**
- Modify: `STM32/Driver/ESP8266.h:56-59,65-76`
- Modify: `STM32/Driver/ESP8266.c:1-8,194-239`

- [ ] **Step 1: Add a compile-time caller before the declaration exists**

Temporarily add this line immediately after the existing `esp8266_get_ip()` call in
`app_uplink_join()`:

```c
if (esp8266_get_gateway(ip, sizeof(ip)) == 0) { LOG_I(TAG, "网关=%s", ip); }
```

Run:

```powershell
cmake --build --preset Debug
```

Expected: FAIL with an implicit-declaration or undefined-reference error for
`esp8266_get_gateway`.

- [ ] **Step 2: Declare the driver API and disabled-module stub**

Add after `esp8266_get_ip()` in the enabled section of `ESP8266.h`:

```c
/* 查询 STA 默认网关（AT+CIPSTA?），并读到本次响应最终 OK，防止残留响应
 * 干扰下一条命令。成功 0；-1 参数错 -2 未初始化 -3 发送失败
 * -4 模块报错 -5 超时或未返回网关 -6 网关字段格式错误/缓冲不足。 */
int esp8266_get_gateway(char *gateway_buf, uint16_t buf_len);
```

Add to the disabled-module stubs:

```c
static inline int esp8266_get_gateway(char *gateway_buf, uint16_t buf_len)
{
    (void)gateway_buf;
    (void)buf_len;
    return -1;
}
```

- [ ] **Step 3: Include the pure parser and implement the query**

Add to `ESP8266.c` includes:

```c
#include "ESP8266_AT_Parse.h"
```

Add after `esp8266_get_ip()`:

```c
int esp8266_get_gateway(char *gateway_buf, uint16_t buf_len)
{
    char line[ESP8266_RX_LINE_MAX];
    char candidate[32];
    uint32_t waited = 0;
    int found = 0;

    if (gateway_buf == NULL || buf_len == 0u) return -1;
    if (s_uartx >= BSP_UART_COUNT) return -2;

    if (bsp_uart_send(s_uartx, (uint8_t *)"AT+CIPSTA?",
                      strlen("AT+CIPSTA?"), 100,
                      BSP_UART_MODE_POLLING) != 0 ||
        bsp_uart_send(s_uartx, (uint8_t *)"\r\n", 2, 100,
                      BSP_UART_MODE_POLLING) != 0) {
        return -3;
    }

    while (waited < 5000u) {
        if (bsp_uart_rec_line(s_uartx, (uint8_t *)line, sizeof(line),
                              ESP8266_POLL_SLICE_MS,
                              BSP_UART_MODE_POLLING) == 0) {
            int parsed = esp8266_at_parse_gateway(line, candidate,
                                                   sizeof(candidate));
            if (parsed == 0) {
                found = 1;
            } else if (parsed == -2 || parsed == -3) {
                return -6;
            }

            if (strstr(line, "ERROR") != NULL ||
                strstr(line, "FAIL") != NULL) {
                return -4;
            }
            if (strstr(line, "OK") != NULL) {
                if (!found || strcmp(candidate, "0.0.0.0") == 0) {
                    return -5;
                }
                if (strlen(candidate) >= buf_len) {
                    return -6;
                }
                strcpy(gateway_buf, candidate);
                return 0;
            }
        }
        waited += ESP8266_POLL_SLICE_MS;
    }
    return -5;
}
```

- [ ] **Step 4: Remove the temporary call from `app_uplink_join()`**

Delete only the temporary `esp8266_get_gateway()` logging line added in Step 1.
Task 4 will add gateway discovery at the correct reconnect boundary.

- [ ] **Step 5: Drain `esp8266_get_ip()` through its final `OK`**

Replace `esp8266_get_ip()` with the following implementation. It preserves the existing
truncating-buffer behavior, but drains the current command through its final `OK` before
allowing the next AT command to start:

```c
int esp8266_get_ip(char *ip_buf, uint16_t buf_len)
{
    char line[ESP8266_RX_LINE_MAX];
    char candidate[32] = {0};
    uint32_t waited = 0;
    int found = 0;

    if (ip_buf == NULL || buf_len == 0u) return -1;
    if (s_uartx >= BSP_UART_COUNT) return -2;

    if (bsp_uart_send(s_uartx, (uint8_t *)"AT+CIFSR",
                      strlen("AT+CIFSR"), 100,
                      BSP_UART_MODE_POLLING) != 0 ||
        bsp_uart_send(s_uartx, (uint8_t *)"\r\n", 2, 100,
                      BSP_UART_MODE_POLLING) != 0) {
        return -3;
    }

    while (waited < 5000u) {
        if (bsp_uart_rec_line(s_uartx, (uint8_t *)line, sizeof(line),
                              ESP8266_POLL_SLICE_MS,
                              BSP_UART_MODE_POLLING) == 0) {
            if (strstr(line, "+CIFSR:STAIP") != NULL) {
                const char *q1 = strchr(line, '"');
                const char *q2;
                size_t len;

                if (q1 == NULL) return -5;
                q1++;
                q2 = strchr(q1, '"');
                if (q2 == NULL || q2 == q1) return -5;
                len = (size_t)(q2 - q1);
                if (len >= sizeof(candidate)) return -5;
                memcpy(candidate, q1, len);
                candidate[len] = '\0';
                found = 1;
            }
            if (strstr(line, "ERROR") != NULL ||
                strstr(line, "FAIL") != NULL) {
                return -4;
            }
            if (strstr(line, "OK") != NULL) {
                size_t len;
                if (!found) return -5;
                len = strlen(candidate);
                if (len >= buf_len) len = buf_len - 1u;
                memcpy(ip_buf, candidate, len);
                ip_buf[len] = '\0';
                return 0;
            }
        }
        waited += ESP8266_POLL_SLICE_MS;
    }
    return -5;
}
```

Do not change the public success/error contract documented in `ESP8266.h`.

- [ ] **Step 6: Build the STM32 firmware**

```powershell
cmake --build --preset Debug
```

Expected: `Plant_Net.elf` links successfully with no new compiler warnings.

- [ ] **Step 7: Verify the command on hardware**

Flash the Debug firmware, open USART1 logs, and power the ESP8266 while connected to the
Windows hotspot.

Expected AT-level behavior, observed through temporary driver debug logging or an AT UART
trace:

```text
+CIPSTA:ip:"<station-ip>"
+CIPSTA:gateway:"<hotspot-ip>"
+CIPSTA:netmask:"255.255.255.0"
OK
```

The returned gateway must equal the hotspot adapter address shown by `ipconfig`.

- [ ] **Step 8: Commit the driver query**

```powershell
git add STM32/Driver/ESP8266.h STM32/Driver/ESP8266.c
git commit -m "feat(stm32): query esp8266 default gateway"
```

### Task 4: Select gateway then fallback in the STM32 uplink task

**Files:**
- Modify: `STM32/App/Src/app_uplink.c:20-106`

- [ ] **Step 1: Write the candidate-selection helper signature and observe failure**

Add this declaration near the existing static state, then call it from
`app_uplink_ensure()` before any implementation exists:

```c
static int app_uplink_connect_server(void);
```

Replace the direct `esp8266_tcp_connect()` assignment with:

```c
s_tcp_ok = app_uplink_connect_server();
```

Run `cmake --build --preset Debug`.

Expected: FAIL with undefined reference to `app_uplink_connect_server`.

- [ ] **Step 2: Implement candidate connection and Wi-Fi recovery**

Add before `app_uplink_ensure()`:

```c
static int app_uplink_connect_server(void)
{
    char gateway[32] = {0};
    int gateway_ok = (esp8266_get_gateway(gateway, sizeof(gateway)) == 0);

    if (!gateway_ok) {
        LOG_I(TAG, "未取得默认网关，重新加入热点");
        if (esp8266_auto_join() == 0) {
            gateway_ok = (esp8266_get_gateway(gateway, sizeof(gateway)) == 0);
        }
    }

    if (gateway_ok) {
        LOG_I(TAG, "默认网关=%s", gateway);
        if (esp8266_tcp_connect(gateway, PN_DATA_PORT) == 0) {
            LOG_I(TAG, "TCP 已连接 %s:%d（热点网关）",
                  gateway, PN_DATA_PORT);
            return 1;
        }
        LOG_E(TAG, "热点网关连接失败 %s:%d", gateway, PN_DATA_PORT);
    }

    if (!gateway_ok || strcmp(gateway, PN_SERVER_HOST) != 0) {
        if (esp8266_tcp_connect(PN_SERVER_HOST, PN_DATA_PORT) == 0) {
            LOG_I(TAG, "TCP 已连接 %s:%d（配置兜底）",
                  PN_SERVER_HOST, PN_DATA_PORT);
            return 1;
        }
        LOG_E(TAG, "配置兜底连接失败 %s:%d",
              PN_SERVER_HOST, PN_DATA_PORT);
    }
    return 0;
}
```

Update `app_uplink_ensure()` so it no longer prints a second generic success line:

```c
s_tcp_ok = app_uplink_connect_server();
if (!s_tcp_ok) {
    LOG_E(TAG, "服务器连接失败，%u ms 后重试",
          (unsigned)UPLINK_RETRY_COOLDOWN_MS);
}
return s_tcp_ok;
```

- [ ] **Step 3: Build and inspect memory usage**

```powershell
cmake --build --preset Debug
```

Expected: successful link; RAM remains below 75% and Flash remains below 60% of the
STM32F103C8 limits reported by the linker.

- [ ] **Step 4: Test server-start ordering on hardware**

1. Enable the Windows hotspot but leave Flask stopped.
2. Power the STM32/ESP8266 and wait for at least one failed retry log.
3. Start `Flask_TCP/server.py` without rebooting the board.
4. Wait at most 15 seconds.

Expected:

```text
[I][app_uplink] 默认网关=<hotspot-ip>
[I][app_uplink] TCP 已连接 <hotspot-ip>:9000（热点网关）
[D][app_uplink] 已发送 seq=<n>
```

The Flask console must show the device connection and a new sensor row.

- [ ] **Step 5: Test hotspot restart recovery**

1. Keep the board powered and stop the Windows hotspot.
2. Wait until the next send/reconnect failure.
3. Restart the same hotspot.
4. Wait for DHCP and the next retry cycle.

Expected: the board logs a new default gateway and resumes uploads without a reset or
reflash.

- [ ] **Step 6: Commit the STM32 uplink selection**

```powershell
git add STM32/App/Src/app_uplink.c
git commit -m "feat(stm32): discover server from hotspot gateway"
```

### Task 5: Select gateway then fallback in ESP32-CAM

**Files:**
- Modify: `ESP-CAM/CameraWebServer/CameraWebServer.ino:44-78,172-193`

- [ ] **Step 1: Add a bounded Wi-Fi helper declaration and verify the sketch fails**

Add before `pushTask()`:

```cpp
static bool gwConnectServer(WiFiClient& client);
```

Replace the existing direct connection line inside `pushTask()`:

```cpp
if (!c.connect(gw_host, GW_PUSH_PORT)) { vTaskDelay(1000); continue; }
```

with:

```cpp
if (!gwConnectServer(c)) { vTaskDelay(1000); continue; }
```

Use Arduino IDE **Verify**, or run the configured equivalent `arduino-cli compile` command.

Expected: FAIL because `gwConnectServer()` has no definition.

- [ ] **Step 2: Implement bounded Wi-Fi recovery**

Add above `pushTask()`:

```cpp
static bool gwEnsureWifi() {
  if (WiFi.status() == WL_CONNECTED) return true;

  Serial.println("GW| WiFi 已断开，开始重连");
  WiFi.reconnect();
  for (uint8_t i = 0; i < 40 && WiFi.status() != WL_CONNECTED; ++i) {
    vTaskDelay(pdMS_TO_TICKS(250));
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("GW| WiFi 重连超时");
    return false;
  }
  Serial.printf("GW| WiFi 已恢复 IP=%s GW=%s\n",
                WiFi.localIP().toString().c_str(),
                WiFi.gatewayIP().toString().c_str());
  return true;
}
```

- [ ] **Step 3: Implement server candidate selection**

Add below `gwEnsureWifi()`:

```cpp
static bool gwConnectServer(WiFiClient& client) {
  if (!gwEnsureWifi()) return false;

  IPAddress gateway = WiFi.gatewayIP();
  String gatewayText = gateway.toString();
  bool gatewayValid = gateway != IPAddress(0, 0, 0, 0);

  client.stop();
  if (gatewayValid) {
    Serial.printf("GW| 尝试热点网关 %s:%u\n",
                  gatewayText.c_str(), GW_PUSH_PORT);
    if (client.connect(gateway, GW_PUSH_PORT)) {
      Serial.printf("GW| 推流已连 %s:%u（热点网关）\n",
                    gatewayText.c_str(), GW_PUSH_PORT);
      return true;
    }
  }

  if (!gatewayValid || gatewayText != String(gw_host)) {
    client.stop();
    Serial.printf("GW| 尝试配置兜底 %s:%u\n", gw_host, GW_PUSH_PORT);
    if (client.connect(gw_host, GW_PUSH_PORT)) {
      Serial.printf("GW| 推流已连 %s:%u（配置兜底）\n",
                    gw_host, GW_PUSH_PORT);
      return true;
    }
  }
  Serial.println("GW| 服务器连接失败");
  return false;
}
```

Keep `gw_host = PN_SERVER_HOST` as the fallback source. Do not change frame format,
resolution, JPEG quality or push period in this task.

- [ ] **Step 4: Add a bounded initial Wi-Fi connection**

Replace the infinite loop in `setup()` with a 30-second attempt followed by restart:

```cpp
  WiFi.begin(ssid, password);
  WiFi.setSleep(false);

  uint32_t wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 30000u) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nGW| 首次连接热点超时，5 秒后重启");
    delay(5000);
    ESP.restart();
  }
```

This prevents an unrecoverable infinite wait while preserving unattended retry behavior.

- [ ] **Step 5: Verify the ESP32-CAM sketch**

Use Arduino IDE **Verify** for board `AI Thinker ESP32-CAM`, PSRAM enabled, and the existing
`partitions.csv`.

Expected: successful compilation with no undefined symbols or type errors involving
`IPAddress`, `String`, `WiFiClient`, or `pdMS_TO_TICKS`.

- [ ] **Step 6: Test gateway selection on hardware**

1. Start Windows hotspot and Flask.
2. Flash and boot ESP32-CAM.
3. Compare the `GW=` value in serial output with the hotspot adapter address from
   `ipconfig`.
4. Open `/api/live` and confirm frames update.

Expected:

```text
WiFi connected
GW| 尝试热点网关 <hotspot-ip>:9001
GW| 推流已连 <hotspot-ip>:9001（热点网关）
```

- [ ] **Step 7: Test ESP32-CAM recovery**

Restart the hotspot while keeping the camera powered. After the hotspot returns, wait up
to 45 seconds.

Expected: bounded reconnect logs appear and `/api/live` resumes without reflashing.

- [ ] **Step 8: Commit the camera change**

```powershell
git add ESP-CAM/CameraWebServer/CameraWebServer.ino
git commit -m "feat(espcam): discover server from hotspot gateway"
```

### Task 6: Update deployment documentation

**Files:**
- Modify: `README.md:19-43`
- Modify: `docs/deployment-config-map.md:19-55`
- Modify: `config/net_config.h:21-30`

- [ ] **Step 1: Update the configuration comment**

Replace the server-address comment in `config/net_config.h` with:

```c
/* ---- 服务器地址兜底 ----
 * 连接服务器电脑的 Windows 移动热点时，固件优先使用 DHCP 默认网关，
 * 因此热点地址变化不需要重新烧录。本值只在网关获取/连接失败时使用。
 * 普通路由器模式下可填写 Flask 电脑的局域网 IP 或公网 TCP 隧道域名。
 * ------------------------------------------------------------ */
#define PN_SERVER_HOST    "192.168.137.1"
```

Regenerate the synchronized local files with the current first-run configuration
wizard after changing the public template.

```powershell
.\首次配置.bat
```

Expected: `STM32/Core/Inc/net_config.h` and
`ESP-CAM/CameraWebServer/net_config.h` match `config/net_config.h` byte for byte.

- [ ] **Step 2: Update the README architecture and quick-start text**

Add this subsection after the architecture description:

```markdown
### 换网络现场部署

设备连接服务器电脑开启的固定 Windows 移动热点。ESP8266 和 ESP32-CAM
会读取 DHCP 默认网关，并把它作为 Flask 服务器地址，因此电脑切换上游 Wi-Fi
或热点网段变化时无需修改 `PN_SERVER_HOST`、重新编译或烧录。

现场只需：开启约定热点 → 启动 `Flask_TCP/server.py` → 给两块设备上电。
串口日志中的“热点网关”地址应与服务器电脑 `ipconfig` 显示的热点适配器地址一致。
```

- [ ] **Step 3: Update the deployment map**

Change the current architecture and network-scenario table so they explicitly state:

```markdown
- Windows 热点模式：设备运行时读取默认网关，作为服务器首选地址；
- `PN_SERVER_HOST`：网关失败后的兜底，不再是热点模式的唯一地址；
- 普通路由器模式：网关通常是路由器，连接失败后使用 `PN_SERVER_HOST`；
- 验证日志必须包含“默认网关”和“热点网关/配置兜底”来源标签。
```

Remove claims that Windows hotspot is always exactly `192.168.137.1`.

- [ ] **Step 4: Check documentation and synchronized configuration**

```powershell
git diff --check
git diff --exit-code --no-index config/net_config.h STM32/Core/Inc/net_config.h
git diff --exit-code --no-index config/net_config.h ESP-CAM/CameraWebServer/net_config.h
rg -n "恒 192\.168\.137\.1|地址恒为 192\.168\.137\.1" README.md docs config
```

Expected: both `--no-index` comparisons exit 0; `rg` prints no obsolete fixed-address
claim; `git diff --check` prints no errors.

- [ ] **Step 5: Commit the documentation**

```powershell
git add README.md docs/deployment-config-map.md config/net_config.h STM32/Core/Inc/net_config.h ESP-CAM/CameraWebServer/net_config.h
git commit -m "docs: explain hotspot gateway discovery"
```

### Task 7: Full verification and handoff record

**Files:**
- Create: `docs/verification/2026-09-11-hotspot-gateway-discovery.md`

- [ ] **Step 1: Run automated/native checks**

```powershell
cmake --build STM32/build/native-tests
ctest --test-dir STM32/build/native-tests --output-on-failure
cmake --build --preset Debug
git diff --check
```

Expected: parser tests pass, STM32 firmware links, memory remains within device limits,
and no whitespace errors are reported.

- [ ] **Step 2: Verify the ESP32-CAM build**

Use Arduino IDE **Verify** with the same board, PSRAM and partition settings used for the
working demo.

Expected: compilation succeeds. Record Arduino-ESP32 core version, sketch flash use and
dynamic-memory use in the verification document.

- [ ] **Step 3: Execute the现场 matrix**

Record pass/fail and the observed addresses for all cases:

```markdown
| 场景 | ESP8266 | ESP32-CAM | 数据/画面 | 结果 |
|---|---|---|---|---|
| 当前热点正常启动 | 网关地址 | 网关地址 | 均恢复 | PASS/FAIL |
| 先设备后服务器 | 自动重试 | 自动重试 | 均恢复 | PASS/FAIL |
| 热点关闭再开启 | 重新入网 | 重新入网 | 均恢复 | PASS/FAIL |
| 服务器更换上游 Wi-Fi | 无需烧录 | 无需烧录 | 均恢复 | PASS/FAIL |
| 网关与 PN_SERVER_HOST 不同 | 使用网关 | 使用网关 | 均恢复 | PASS/FAIL |
```

Do not mark a row PASS without observing both a new database row and a fresh video frame.

- [ ] **Step 4: Write the verification record**

Create `docs/verification/2026-09-11-hotspot-gateway-discovery.md` containing:

```markdown
# Hotspot Gateway Discovery Verification

- STM32 commit: `<git rev-parse --short HEAD>`
- STM32 build: PASS/FAIL, RAM `<bytes>/<percent>`, Flash `<bytes>/<percent>`
- Native parser tests: PASS/FAIL
- ESP32-CAM build: PASS/FAIL, Arduino-ESP32 `<version>`
- Server hotspot adapter: `<adapter name>`, `<IPv4>`
- Upstream network before/after: `<name/address>` → `<name/address>`
- Hardware matrix: `<completed table from Step 3>`
- Remaining limitation: hotspot SSID/password are still preconfigured; arbitrary router
  credentials require a separate captive-portal feature.
```

Replace every angle-bracket value with observed output before committing; no placeholder
may remain.

- [ ] **Step 5: Confirm only intended files changed**

```powershell
git status --short
git diff --stat HEAD
git log --oneline -7
```

Expected: only the verification record remains uncommitted; recent commits correspond to
Tasks 1-6.

- [ ] **Step 6: Commit the verified handoff record**

```powershell
git add docs/verification/2026-09-11-hotspot-gateway-discovery.md
git commit -m "test: record hotspot discovery verification"
```

Expected: clean `git status --short` and a reproducible record of the tested network
transitions.
