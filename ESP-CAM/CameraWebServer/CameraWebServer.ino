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

// ===========================
// WiFi 凭据（来自统一 net_config.h）
// ===========================
const char* ssid = PN_WIFI_SSID;
const char* password = PN_WIFI_PASS;

void startCameraServer();
void setupLedFlash(int pin);

/* ================================================================
 * Plant_Net 推流（CAM 纯客户端）：持续把有效帧推给 Flask 收流口，
 * 定时截取/校验/存库全部在 Flask 端做。帧格式：文本头 "L:<len>\n" + JPEG 字节。
 * ================================================================ */
const char* gw_host        = PN_SERVER_HOST;   // 默认网关失败后的 Flask 兜底地址
const uint16_t GW_PUSH_PORT = PN_PUSH_PORT;    // Flask 收流口
const uint32_t GW_PUSH_PERIOD_MS = 100;        // ~10fps VGA（数据已走 ESP8266，带宽充足）
#define GW_FRAME_SIZE  FRAMESIZE_VGA           /* 640x480：XCLK15 下稳定甜点（SVGA 帧率崩，已回退） */
#define GW_JPEG_Q      6                        /* JPEG 质量，数字越小越清晰（6~10 常用区间） */
static uint32_t s_pushRetry = 0;

static bool gwFrameValid(camera_fb_t* fb) {
  return fb && fb->len > 1000 && fb->buf[0] == 0xFF && fb->buf[1] == 0xD8;
}

static bool gwIpValid(const IPAddress& ip) {
  return ip[0] != 0 || ip[1] != 0 || ip[2] != 0 || ip[3] != 0;
}

/* WiFi 断开后做一次有界重连，让推流任务不会永久卡在等待里。 */
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

  String localText = WiFi.localIP().toString();
  String gatewayText = WiFi.gatewayIP().toString();
  Serial.printf("GW| WiFi 已恢复 IP=%s GW=%s\n",
                localText.c_str(), gatewayText.c_str());
  return true;
}

/* Windows 热点模式下默认网关就是 Flask 电脑；普通路由器模式下
 * 网关连接失败后再使用 PN_SERVER_HOST。 */
static bool gwConnectServer(WiFiClient& client) {
  if (!gwEnsureWifi()) return false;

  IPAddress gateway = WiFi.gatewayIP();
  String gatewayText = gateway.toString();
  bool gatewayValid = gwIpValid(gateway);

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

  /* 地址相同则不重复连接，避免多等一次 TCP 超时。 */
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

static void pushTask(void*) {
  WiFiClient c;
  for (;;) {
    if (!c.connected()) {
      if (millis() - s_pushRetry < 3000) { vTaskDelay(300); continue; }
      s_pushRetry = millis();
      if (!gwConnectServer(c)) { vTaskDelay(1000); continue; }
    }
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) {
      if (gwFrameValid(fb)) {
        c.print("L:"); c.print(fb->len); c.print("\n");
        c.write(fb->buf, fb->len);
      }
      esp_camera_fb_return(fb);
    }
    vTaskDelay(GW_PUSH_PERIOD_MS);
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
  config.frame_size = FRAMESIZE_UXGA;
  config.pixel_format = PIXFORMAT_JPEG;  // for streaming
  //config.pixel_format = PIXFORMAT_RGB565; // for face detection/recognition
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  // if PSRAM IC present, init with UXGA resolution and higher JPEG quality
  //                      for larger pre-allocated frame buffer.
  if (config.pixel_format == PIXFORMAT_JPEG) {
    if (psramFound()) {
      config.jpeg_quality = 10;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
      // Limit the frame size when PSRAM is not available
      config.frame_size = FRAMESIZE_SVGA;
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
  // drop down frame size（VGA：清晰与带宽的甜点；SVGA 在此模块 XCLK15 下有时序风险）
  if (config.pixel_format == PIXFORMAT_JPEG) {
    s->set_framesize(s, GW_FRAME_SIZE);
  }
  s->set_quality(s, GW_JPEG_Q);   // 质量越小越清晰

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
    return;  // 防止极端情况下重启调用返回后继续启动推流
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.printf("GW| IP=%s 默认网关=%s\n",
                WiFi.localIP().toString().c_str(),
                WiFi.gatewayIP().toString().c_str());

  startCameraServer();

  /* XCLK 运行时降频：初始化 20MHz 保险起步 → 等 httpd 起来后切 15MHz（同页面 /xclk） */
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
