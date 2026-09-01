#include "apps/camera_app.h"
#include "core/log.h"
#include "core/events.h"
#include "esp_camera.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

// AI-Thinker ESP32-CAM pin map (same board the stock firmware targets)
#define PWDN_GPIO_NUM  32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM   0
#define SIOD_GPIO_NUM  26
#define SIOC_GPIO_NUM  27
#define Y9_GPIO_NUM    35
#define Y8_GPIO_NUM    34
#define Y7_GPIO_NUM    39
#define Y6_GPIO_NUM    36
#define Y5_GPIO_NUM    21
#define Y4_GPIO_NUM    19
#define Y3_GPIO_NUM    18
#define Y2_GPIO_NUM     5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM  23
#define PCLK_GPIO_NUM  22

#define MYSAT_MAX_PHOTOS 8
#define CAPTURE_TIMEOUT_MS 1500
static const char* INDEX_FILE = "/photo_index.json";

static bool s_present = false;
static int s_next_id = 0;

struct PhotoRec { int id; String ts; String path; };
static PhotoRec s_photos[MYSAT_MAX_PHOTOS];
static int s_count = 0;

static void index_load() {
  s_count = 0;
  if (!LittleFS.exists(INDEX_FILE)) return;
  File f = LittleFS.open(INDEX_FILE, "r");
  if (!f) return;
  DynamicJsonDocument doc(2048);
  if (deserializeJson(doc, f) == DeserializationError::Ok) {
    for (JsonObject o : doc["photos"].as<JsonArray>()) {
      if (s_count >= MYSAT_MAX_PHOTOS) break;
      s_photos[s_count].id = o["id"]; s_photos[s_count].ts = o["ts"].as<const char*>();
      s_photos[s_count].path = o["path"].as<const char*>();
      s_count++;
      if (s_photos[s_count - 1].id >= s_next_id) s_next_id = s_photos[s_count - 1].id + 1;
    }
  }
  f.close();
}
static void index_save() {
  DynamicJsonDocument doc(2048);
  JsonArray arr = doc.createNestedArray("photos");
  for (int i = 0; i < s_count; i++) {
    JsonObject o = arr.createNestedObject();
    o["id"] = s_photos[i].id; o["ts"] = s_photos[i].ts; o["path"] = s_photos[i].path;
  }
  File f = LittleFS.open(INDEX_FILE, "w");
  if (f) { serializeJson(doc, f); f.close(); }
}

bool camera_init() {
  camera_config_t c{};
  c.ledc_channel = LEDC_CHANNEL_0; c.ledc_timer = LEDC_TIMER_0;
  c.pin_d0 = Y2_GPIO_NUM; c.pin_d1 = Y3_GPIO_NUM; c.pin_d2 = Y4_GPIO_NUM; c.pin_d3 = Y5_GPIO_NUM;
  c.pin_d4 = Y6_GPIO_NUM; c.pin_d5 = Y7_GPIO_NUM; c.pin_d6 = Y8_GPIO_NUM; c.pin_d7 = Y9_GPIO_NUM;
  c.pin_xclk = XCLK_GPIO_NUM; c.pin_pclk = PCLK_GPIO_NUM; c.pin_vsync = VSYNC_GPIO_NUM; c.pin_href = HREF_GPIO_NUM;
  c.pin_sscb_sda = SIOD_GPIO_NUM; c.pin_sscb_scl = SIOC_GPIO_NUM;
  c.pin_pwdn = PWDN_GPIO_NUM; c.pin_reset = RESET_GPIO_NUM;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_JPEG;
  c.frame_size = FRAMESIZE_SVGA;      // 800x600: a lighter payload than the stock XGA, camera is secondary here
  c.jpeg_quality = 15;
  c.fb_count = 1;
  c.grab_mode = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&c);
  if (err != ESP_OK) { LOGE("CAMERA", "init failed: 0x%x", err); s_present = false; return false; }
  s_present = true;
  index_load();
  LOGI("CAMERA", "ready, %d photo(s) in ring", s_count);
  return true;
}
bool camera_present() { return s_present; }

static camera_fb_t* capture_with_timeout() {
  uint32_t start = millis();
  camera_fb_t* fb = nullptr;
  while (millis() - start < CAPTURE_TIMEOUT_MS) {
    fb = esp_camera_fb_get();
    if (fb) return fb;
    delay(10);
  }
  return nullptr;
}

int camera_capture(const char* iso_timestamp, float roll, float pitch, float yaw, bool sun_in_fov) {
  if (!s_present) return -1;
  camera_fb_t* fb = capture_with_timeout();
  if (!fb) { LOGE("CAMERA", "capture timed out"); events_post(EV_PHOTO, -1, "capture timeout"); return -1; }

  char path[32]; snprintf(path, sizeof path, "/photo_%d.jpg", s_next_id % MYSAT_MAX_PHOTOS);
  File f = LittleFS.open(path, "w");
  if (!f) { esp_camera_fb_return(fb); return -1; }
  f.write(fb->buf, fb->len);
  f.close();
  size_t len = fb->len;
  esp_camera_fb_return(fb);

  // metadata sidecar: this is the L1 product idea from the payload council report, kept minimal here
  char metaPath[32]; snprintf(metaPath, sizeof metaPath, "/photo_%d.json", s_next_id % MYSAT_MAX_PHOTOS);
  File mf = LittleFS.open(metaPath, "w");
  if (mf) {
    mf.printf("{\"id\":%d,\"ts\":\"%s\",\"bytes\":%u,\"roll\":%.1f,\"pitch\":%.1f,\"yaw\":%.1f,\"sun_in_fov\":%s}\n",
              s_next_id, iso_timestamp, (unsigned)len, roll, pitch, yaw, sun_in_fov ? "true" : "false");
    mf.close();
  }

  int slot = s_next_id % MYSAT_MAX_PHOTOS;
  bool found = false;
  for (int i = 0; i < s_count; i++) if (s_photos[i].id % MYSAT_MAX_PHOTOS == slot) { s_photos[i] = {s_next_id, iso_timestamp, path}; found = true; break; }
  if (!found && s_count < MYSAT_MAX_PHOTOS) s_photos[s_count++] = {s_next_id, iso_timestamp, path};

  index_save();
  events_post(EV_PHOTO, s_next_id, "photo #%d saved, %u bytes", s_next_id, (unsigned)len);
  int id = s_next_id;
  s_next_id++;
  return id;
}

String camera_index_json() {
  String j = "[";
  for (int i = 0; i < s_count; i++) {
    if (i) j += ",";
    j += "{\"id\":" + String(s_photos[i].id) + ",\"ts\":\"" + s_photos[i].ts + "\"}";
  }
  j += "]";
  return j;
}

bool camera_photo_path(int id, String& outPath, String& outTimestamp) {
  for (int i = 0; i < s_count; i++) if (s_photos[i].id == id) { outPath = s_photos[i].path; outTimestamp = s_photos[i].ts; return true; }
  return false;
}
