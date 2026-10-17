#include "esp_camera.h"
#include <WiFi.h>
#include "net_config.h"   /* 统一网络配置（config/net_config.h 的编译副本，同目录） */

//
// WARNING!!! PSRAM IC required for UXGA resolution and high JPEG quality
//            Ensure ESP32 Wrover Module or other board with PSRAM is selected
//            Partial images will be transmitted if image exceeds buffer size
//
//            You must select partition scheme from the board menu that has at least 3MB APP space.
//            Face Recognition is DISABLED for ESP32 and ESP32-S2, because it takes up from 15
//            seconds to process single frame. Face Detection is ENABLED if PSRAM is enabled as well

// ===================
// Select camera model
// ===================
//#define CAMERA_MODEL_WROVER_KIT // Has PSRAM
//#define CAMERA_MODEL_ESP_EYE  // Has PSRAM
//#define CAMERA_MODEL_ESP32S3_EYE // Has PSRAM
//#define CAMERA_MODEL_M5STACK_PSRAM // Has PSRAM
//#define CAMERA_MODEL_M5STACK_V2_PSRAM // M5Camera version B Has PSRAM
//#define CAMERA_MODEL_M5STACK_WIDE // Has PSRAM
//#define CAMERA_MODEL_M5STACK_ESP32CAM // No PSRAM
//#define CAMERA_MODEL_M5STACK_UNITCAM // No PSRAM
#define CAMERA_MODEL_AI_THINKER  // Has PSRAM
//#define CAMERA_MODEL_TTGO_T_JOURNAL // No PSRAM
//#define CAMERA_MODEL_XIAO_ESP32S3 // Has PSRAM
// ** Espressif Internal Boards **
//#define CAMERA_MODEL_ESP32_CAM_BOARD
//#define CAMERA_MODEL_ESP32S2_CAM_BOARD
//#define CAMERA_MODEL_ESP32S3_CAM_LCD

#include "camera_pins.h"

// 已知热点按配置顺序排列；密码只保存在不上传的 net_config.h 中。
struct KnownWifi {
  const char* ssid;
  const char* password;
  const char* server_host;
};
static const KnownWifi knownWifi[] = { PN_WIFI_NETWORKS };

void startCameraServer();
void setupLedFlash(int pin);

/* ================================================================
 * Plant_Net 推流（CAM 纯客户端）：持续把有效帧推给 Flask 收流口，
 * 定时截取/校验/存库全部在 Flask 端做。帧格式：文本头 "L:<len>\n" + JPEG 字节。
 * ================================================================ */
const char* gw_host        = PN_SERVER_HOST;   // 当前热点的 Flask 兜底地址
const uint16_t GW_PUSH_PORT = PN_PUSH_PORT;    // Flask 收流口
#define GW_FRAME_SIZE  FRAMESIZE_SVGA          /* 800x600 */
#define GW_JPEG_Q      12                       /* JPEG 质量，数字越小压缩越少 */
volatile uint32_t gw_push_fps_limit = 10;       // 网页可调整的推流帧率上限
volatile uint32_t gw_push_actual_fps_x10 = 0;  // 最近 10 秒成功推送的平均 FPS ×10
static uint32_t s_pushRetry = 0;

static bool gwFrameValid(camera_fb_t* fb) {
  return fb && fb->len > 1000 && fb->buf[0] == 0xFF && fb->buf[1] == 0xD8;
}

static bool gwIpValid(const IPAddress& ip) {
  return ip[0] != 0 || ip[1] != 0 || ip[2] != 0 || ip[3] != 0;
}

/* 扫描一轮热点，按配置顺序尝试可见网络。失败后调用方稍后再扫描。 */
static bool gwConnectKnownWifiOnce() {
  WiFi.disconnect();
  int found = WiFi.scanNetworks();
  if (found < 0) {
    Serial.printf("GW| 热点扫描失败：%d\n", found);
    return false;
  }
  Serial.printf("GW| 扫描到 %d 个热点\n", found);

  bool visible[sizeof(knownWifi) / sizeof(knownWifi[0])] = {};
  for (size_t i = 0; i < sizeof(knownWifi) / sizeof(knownWifi[0]); ++i) {
    for (int j = 0; j < found; ++j) {
      if (WiFi.SSID(j) == knownWifi[i].ssid) {
        visible[i] = true;
        break;
      }
    }
  }
  WiFi.scanDelete();

  for (size_t i = 0; i < sizeof(knownWifi) / sizeof(knownWifi[0]); ++i) {
    if (!visible[i]) continue;

    Serial.printf("GW| 尝试连接已知热点：%s\n", knownWifi[i].ssid);
    WiFi.begin(knownWifi[i].ssid, knownWifi[i].password);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 12000u) {
      delay(200);
    }
    if (WiFi.status() == WL_CONNECTED) {
      gw_host = knownWifi[i].server_host;
      Serial.printf("GW| 当前热点：%s，IP=%s\n", knownWifi[i].ssid,
                    WiFi.localIP().toString().c_str());
      return true;
    }
    Serial.printf("GW| 热点连接失败或超时：%s\n", knownWifi[i].ssid);
    WiFi.disconnect();
    delay(100);
  }

  return false;
}

/* WiFi 断开后重新扫描，让推流任务不会永久卡在原热点。 */
static bool gwEnsureWifi() {
  if (WiFi.status() == WL_CONNECTED) return true;

  Serial.println("GW| WiFi 已断开，重新扫描已知热点");
  if (!gwConnectKnownWifiOnce()) {
    Serial.println("GW| 本轮没有可连接的热点");
    return false;
  }

  String localText = WiFi.localIP().toString();
  String gatewayText = WiFi.gatewayIP().toString();
  Serial.printf("GW| WiFi 已恢复 IP=%s GW=%s\n",
                localText.c_str(), gatewayText.c_str());
  return true;
}

/* 优先使用当前热点配置的 Flask 地址；Windows 热点的网关作为兜底。 */
static bool gwConnectServer(WiFiClient& client) {
  if (!gwEnsureWifi()) return false;

  IPAddress gateway = WiFi.gatewayIP();
  String gatewayText = gateway.toString();
  bool gatewayValid = gwIpValid(gateway);

  client.stop();
  if (gw_host && gw_host[0]) {
    Serial.printf("GW| 尝试配置服务器 %s:%u\n", gw_host, GW_PUSH_PORT);
    if (client.connect(gw_host, GW_PUSH_PORT)) {
      Serial.printf("GW| 推流已连 %s:%u（配置地址）\n", gw_host, GW_PUSH_PORT);
      return true;
    }
  }

  /* 地址相同则不重复连接，避免多等一次 TCP 超时。 */
  if (gatewayValid && (!gw_host || gatewayText != String(gw_host))) {
    client.stop();
    Serial.printf("GW| 尝试热点网关 %s:%u\n", gatewayText.c_str(), GW_PUSH_PORT);
    if (client.connect(gateway, GW_PUSH_PORT)) {
      Serial.printf("GW| 推流已连 %s:%u（热点网关）\n",
                    gatewayText.c_str(), GW_PUSH_PORT);
      return true;
    }
  }

  Serial.println("GW| 服务器连接失败");
  return false;
}

static void pushTask(void*) {
  WiFiClient c;
  uint32_t fpsStart = millis();
  uint32_t pushedFrames = 0;
  for (;;) {
    if (!c.connected()) {
      gw_push_actual_fps_x10 = 0;
      if (millis() - s_pushRetry < 3000) { vTaskDelay(300); continue; }
      s_pushRetry = millis();
      if (!gwConnectServer(c)) { vTaskDelay(1000); continue; }
      fpsStart = millis();
      pushedFrames = 0;
    }
    uint32_t frameStart = millis();
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) {
      if (gwFrameValid(fb)) {
        c.print("L:"); c.print(fb->len); c.print("\n");
        if (c.write(fb->buf, fb->len) == fb->len) {
          ++pushedFrames;
        } else {
          Serial.println("GW| JPEG 发送不完整，重新连接服务器");
          c.stop();
          gw_push_actual_fps_x10 = 0;
        }
      }
      esp_camera_fb_return(fb);
    }
    uint32_t elapsed = millis() - fpsStart;
    if (elapsed >= 10000u) {
      gw_push_actual_fps_x10 = (pushedFrames * 10000u + elapsed / 2u) / elapsed;
      Serial.printf("GW| 最近 %lu ms 推流 %lu 帧，平均 %.1f FPS\n",
                    (unsigned long)elapsed, (unsigned long)pushedFrames,
                    pushedFrames * 1000.0f / elapsed);
      fpsStart = millis();
      pushedFrames = 0;
    }
    // 把采集与网络发送耗时也计入周期，设置值表示帧率上限。
    uint32_t periodMs = 1000u / gw_push_fps_limit;
    uint32_t frameElapsed = millis() - frameStart;
    if (frameElapsed < periodMs) {
      vTaskDelay(pdMS_TO_TICKS(periodMs - frameElapsed));
    } else {
      vTaskDelay(1);
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = GW_FRAME_SIZE;
  config.pixel_format = PIXFORMAT_JPEG;  // for streaming
  //config.pixel_format = PIXFORMAT_RGB565; // for face detection/recognition
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = GW_JPEG_Q;
  config.fb_count = 1;

  // 按实际推流分辨率分配帧缓冲，避免为 UXGA 预留多余内存。
  if (config.pixel_format == PIXFORMAT_JPEG) {
    if (psramFound()) {
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
      config.fb_location = CAMERA_FB_IN_DRAM;
    }
  } else {
    // Best option for face detection/recognition
    config.frame_size = FRAMESIZE_240X240;
#if CONFIG_IDF_TARGET_ESP32S3
    config.fb_count = 2;
#endif
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t* s = esp_camera_sensor_get();
  // initial sensors are flipped vertically and colors are a bit saturated
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);        // flip it back
    s->set_brightness(s, 1);   // up the brightness just a bit
    s->set_saturation(s, -2);  // lower the saturation
  }
  // 长时间监控画质参数。方向由安装方式决定，保持原来的翻转设置。
  s->set_brightness(s, 0);
  s->set_contrast(s, 0);
  s->set_saturation(s, 0);
  if (s->set_sharpness) {
    int rc = s->set_sharpness(s, 1);
    if (rc != 0) Serial.printf("GW| 锐度 +1 设置失败 (rc=%d)\n", rc);
  } else {
    Serial.println("GW| 当前传感器不支持设置锐度");
  }
  // 降噪“自动”没有对应的统一驱动档位，保留传感器默认值。
  s->set_ae_level(s, 0);
  s->set_special_effect(s, 0);
  s->set_whitebal(s, 1);       // 自动白平衡
  s->set_awb_gain(s, 1);       // 高级自动白平衡
  s->set_wb_mode(s, 0);        // 不指定手动白平衡模式
  s->set_exposure_ctrl(s, 1);  // 自动曝光
  s->set_aec2(s, 0);           // 夜间模式关闭
  s->set_gain_ctrl(s, 1);      // 自动增益
  s->set_raw_gma(s, 1);
  s->set_lenc(s, 1);
  s->set_bpc(s, 1);
  s->set_wpc(s, 1);
  s->set_colorbar(s, 0);

#if defined(CAMERA_MODEL_M5STACK_WIDE) || defined(CAMERA_MODEL_M5STACK_ESP32CAM)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

#if defined(CAMERA_MODEL_ESP32S3_EYE)
  s->set_vflip(s, 1);
#endif

// Setup LED FLash if LED pin is defined in camera_pins.h
#if defined(LED_GPIO_NUM)
  setupLedFlash(LED_GPIO_NUM);
#endif

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setSleep(false);
  while (!gwConnectKnownWifiOnce()) {
    Serial.println("GW| 没有可连接的已知热点，5 秒后重扫");
    delay(5000);
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.printf("GW| IP=%s 默认网关=%s\n",
                WiFi.localIP().toString().c_str(),
                WiFi.gatewayIP().toString().c_str());

  startCameraServer();

  /* XCLK 运行时降频：初始化仍以 20MHz 起步，推流前切到 15MHz。 */
  delay(1000);
  {
    int rc = s->set_xclk(s, LEDC_TIMER_0, 15);
    Serial.printf("GW| XCLK 20->15MHz (rc=%d)\n", rc);
  }

  /* Plant_Net 推流任务：CAM 纯客户端，持续推帧给 Flask(9001) */
  xTaskCreate(pushTask, "gw_push", 8192, NULL, 1, NULL);
  Serial.println("GW| 推流任务已启动");

  Serial.print("Camera Ready! Use 'http://");
  Serial.print(WiFi.localIP());
  Serial.println("' to connect");
}

void loop() {
  // Do nothing. Everything is done in another task by the web server
  delay(10000);
}
