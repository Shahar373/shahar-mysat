#include "apps/web.h"
#include "apps/wifi.h"
#include "apps/console.h"
#include "apps/data_logger.h"
#include "apps/camera_app.h"
#include "core/bus.h"
#include "core/events.h"
#include "core/log.h"
#include "core/fdir.h"
#include "core/mission_clock.h"
#include "core/params_store.h"
#include "hal/leds.h"
#include "hal/nano_link.h"
#include "apps/mission.h"
#include "apps/demo.h"
#include "attitude_trigger.h"
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

static WebServer s_server(80);

static void send_json(JsonDocument& doc) {
  String out; serializeJson(doc, out);
  s_server.send(200, "application/json", out);
}

static void handle_telemetry() {
  Telemetry tm; telemetry_get(tm);
  DynamicJsonDocument doc(1536);
  doc["seq"] = tm.seq; doc["uptime_ms"] = tm.uptime_ms;
  char iso[24]; clock_now_iso(iso, sizeof iso); doc["time"] = iso; doc["clock_status"] = clock_status_str();

  JsonObject env = doc.createNestedObject("env"); env["valid"] = tm.env.valid;
  env["temperature_c"] = tm.env.temperature_c; env["humidity_pct"] = tm.env.humidity_pct;
  env["pressure_hpa"] = tm.env.pressure_hpa; env["gas_kohm"] = tm.env.gas_kohm;
  env["iaq"] = tm.env.iaq; env["iaq_accuracy"] = tm.env.iaq_accuracy;

  JsonObject att = doc.createNestedObject("attitude"); att["valid"] = tm.att.valid; att["calibrated"] = tm.att.calibrated;
  att["roll_deg"] = tm.att.roll_deg; att["pitch_deg"] = tm.att.pitch_deg; att["yaw_deg"] = tm.att.yaw_deg;
  att["rate_mag_dps"] = tm.att.rate_mag_dps; att["imu_whoami"] = tm.att.imu_whoami;
  JsonArray rate = att.createNestedArray("rate_dps"); for (float v : tm.att.rate_dps) rate.add(v);

  JsonObject sun = doc.createNestedObject("sun"); sun["valid"] = tm.sun.valid;
  JsonArray raw = sun.createNestedArray("raw"); for (int16_t v : tm.sun.raw) raw.add(v);

  JsonObject pwr = doc.createNestedObject("power"); pwr["valid"] = tm.pwr.valid;
  pwr["batt_v"] = tm.pwr.batt_v; pwr["batt_ma"] = tm.pwr.batt_ma; pwr["panel_v"] = tm.pwr.panel_v;
  pwr["panel_left_ma"] = tm.pwr.panel_left_ma; pwr["panel_right_ma"] = tm.pwr.panel_right_ma;

  doc["logging"]["enabled"] = (bool)g_params.logging_enabled;
  doc["logging"]["period_s"] = g_params.log_period_s;
  doc["logging"]["rows"] = logger_row_count();
  send_json(doc);
}

static void handle_hk() {
  Housekeeping hk; hk_get(hk);
  DynamicJsonDocument doc(1024);
  doc["uptime_s"] = hk.uptime_s; doc["boots"] = hk.boots; doc["reset_reason"] = hk.reset_reason;
  doc["heap_free"] = hk.heap_free; doc["heap_min"] = hk.heap_min; doc["psram_free"] = hk.psram_free;
  doc["cpu_temp_c"] = hk.cpu_temp_c; doc["fs_used"] = hk.fs_used; doc["fs_total"] = hk.fs_total;
  doc["mode"] = hk.mode; doc["callsign"] = g_params.callsign;
  JsonObject ms = doc.createNestedObject("mission");
  ms["phase"] = mission_phase_str();
  ms["countdown_s"] = hk.mission_countdown_s;
  ms["orientation"] = orientation_str(hk.orientation);
  ms["auto"] = mission_auto_enabled();
  ms["upright_ref"] = (bool)g_params.up_ref_valid;
  JsonObject dm = doc.createNestedObject("demo");
  dm["armed"] = (bool)g_params.demo_enabled;
  dm["running"] = demo_running();
  dm["step"] = demo_step_str(demo_step_kind());
  dm["remaining_s"] = demo_remaining_s();
  doc["wifi"]["mode"] = hk.wifi_mode; doc["wifi"]["connected"] = hk.wifi_connected;
  doc["wifi"]["rssi"] = hk.wifi_rssi; doc["wifi"]["ip"] = hk.ip;
  doc["aux"]["present"] = hk.aux_ok;
  if (hk.aux_ok) {
    doc["aux"]["servo_angle"] = hk.aux.servo_angle; doc["aux"]["servo_state"] = hk.aux.servo_state;
    doc["aux"]["hb_age_s"] = hk.aux.hb_age_s; doc["aux"]["boot_flags"] = hk.aux.boot_flags;
  }
  doc["panels_deployed"] = hk.panels_deployed; doc["star_led"] = hk.star_led; doc["camera_ok"] = hk.camera_ok;
  send_json(doc);
}

static void handle_cmd() {
  if (!s_server.hasArg("plain") && !s_server.hasArg("cmd")) { s_server.send(400, "text/plain", "missing command"); return; }
  String line = s_server.hasArg("cmd") ? s_server.arg("cmd") : s_server.arg("plain");
  String out;
  class StrPrint : public Print { public: String* s; size_t write(uint8_t c) override { *s += (char)c; return 1; } } sp; sp.s = &out;
  console_lock();
  bool known = console_execute(line, sp);
  console_unlock();
  events_post(EV_CMD, known, "web cmd: %s", line.c_str());
  if (!known) out += "? unrecognized command\n";
  s_server.send(200, "text/plain", out);
}

static void handle_photos() { s_server.send(200, "application/json", camera_index_json()); }
static void handle_photo() {
  if (!s_server.hasArg("id")) { s_server.send(400, "text/plain", "missing id"); return; }
  String path, ts;
  if (!camera_photo_path(s_server.arg("id").toInt(), path, ts)) { s_server.send(404, "text/plain", "not found"); return; }
  File f = LittleFS.open(path, "r");
  if (!f) { s_server.send(500, "text/plain", "read failed"); return; }
  s_server.sendHeader("X-Photo-Timestamp", ts);
  s_server.streamFile(f, "image/jpeg");   // served raw, unlike the stock firmware's base64-in-JSON
  f.close();
}
static void handle_photo_capture() {
  Telemetry tm; telemetry_get(tm);
  char iso[24]; clock_now_iso(iso, sizeof iso);
  bool sunFov = tm.sun.valid && tm.sun.raw[3] > 300;
  int id = camera_capture(iso, tm.att.roll_deg, tm.att.pitch_deg, tm.att.yaw_deg, sunFov);
  DynamicJsonDocument doc(128); doc["id"] = id; doc["ok"] = id >= 0;
  send_json(doc);
}

static void handle_logs() { s_server.send(200, "application/json", logger_list_json()); }
static void handle_log_download() {
  if (!s_server.hasArg("file")) { s_server.send(400, "text/plain", "missing file"); return; }
  String name = s_server.arg("file");
  s_server.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
  class StrmPrint : public Print { public: WiFiClient* c; size_t write(const uint8_t* b, size_t n) override { return c->write(b, n); } size_t write(uint8_t b) override { return c->write(&b, 1); } } sp;
  WiFiClient client = s_server.client(); sp.c = &client;
  s_server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  s_server.send(200, "text/csv", "");
  if (!logger_download(name.c_str(), sp)) s_server.sendContent("error: file not found\n");
}

static void handle_events() {
  class StrmPrint : public Print { public: WiFiClient* c; size_t write(const uint8_t* b, size_t n) override { return c->write(b, n); } size_t write(uint8_t b) override { return c->write(&b, 1); } } sp;
  WiFiClient client = s_server.client(); sp.c = &client;
  s_server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  s_server.send(200, "text/plain", "");
  events_dump(sp, 300);
}

static const char FALLBACK_HTML[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>MySat</title><style>
body{font:15px system-ui;background:#0b1220;color:#e7ecf5;margin:0;padding:16px}
h1{font-size:1.3rem} .row{display:flex;flex-wrap:wrap;gap:10px;margin:10px 0}
.card{background:#131d31;border:1px solid #26334d;border-radius:8px;padding:10px 14px;min-width:160px}
.card b{display:block;color:#98a5bc;font-size:.75rem;font-weight:400}
button{background:#1a2640;color:#e7ecf5;border:1px solid #26334d;border-radius:6px;padding:6px 12px;cursor:pointer}
input,textarea{background:#0b1220;color:#e7ecf5;border:1px solid #26334d;border-radius:6px;padding:6px}
pre{white-space:pre-wrap;background:#131d31;border:1px solid #26334d;border-radius:8px;padding:10px;max-height:240px;overflow:auto}
small{color:#98a5bc}
</style></head><body>
<h1>MySat &mdash; standalone console</h1>
<small>data/index.html was not found on LittleFS, showing the built-in fallback page.</small>
<div class="row" id="cards"></div>
<div class="row">
<button onclick="cmd('solar deploy')">Deploy panels</button>
<button onclick="cmd('solar retract')">Retract panels</button>
<button onclick="cmd('led toggle')">Toggle LED</button>
<button onclick="cmd('demo run')">Run demo show</button>
<button onclick="fetch('/api/photo/capture').then(()=>log('photo captured'))">Take photo</button>
</div>
<div class="row" style="width:100%">
<input id="cmdline" style="flex:1" placeholder="command, try: help" onkeydown="if(event.key=='Enter')send()">
<button onclick="send()">Send</button>
</div>
<pre id="out"></pre>
<script>
function log(s){document.getElementById('out').textContent += s + "\n";}
function cmd(c){fetch('/api/cmd',{method:'POST',body:c}).then(r=>r.text()).then(log);}
function send(){const el=document.getElementById('cmdline');cmd(el.value);el.value='';}
function poll(){fetch('/api/telemetry').then(r=>r.json()).then(t=>{
  const c=document.getElementById('cards'); c.innerHTML='';
  const add=(l,v)=>{c.innerHTML+='<div class="card"><b>'+l+'</b>'+v+'</div>';};
  add('Time', t.time);
  if(t.env.valid) add('Environment', t.env.temperature_c.toFixed(1)+'&deg;C  '+t.env.pressure_hpa.toFixed(0)+'hPa  IAQ '+t.env.iaq.toFixed(0));
  if(t.attitude.valid) add('Attitude', 'R'+t.attitude.roll_deg.toFixed(0)+' P'+t.attitude.pitch_deg.toFixed(0)+' Y'+t.attitude.yaw_deg.toFixed(0)+'&deg;');
  if(t.power.valid) add('Power', t.power.batt_v.toFixed(2)+'V  '+t.power.batt_ma.toFixed(0)+'mA');
  if(t.sun.valid) add('Sun', t.sun.raw.join(' / '));
}).catch(()=>{});}
setInterval(poll,2000); poll();
</script></body></html>)HTML";

static void handle_root() {
  if (LittleFS.exists("/index.html")) { File f = LittleFS.open("/index.html", "r"); s_server.streamFile(f, "text/html"); f.close(); return; }
  s_server.send_P(200, "text/html", FALLBACK_HTML);
}

static void handle_static() {
  String path = s_server.uri();
  if (!LittleFS.exists(path)) { s_server.send(404, "text/plain", "not found"); return; }
  String ct = "text/plain";
  if (path.endsWith(".html")) ct = "text/html"; else if (path.endsWith(".css")) ct = "text/css";
  else if (path.endsWith(".js")) ct = "application/javascript"; else if (path.endsWith(".json")) ct = "application/json";
  else if (path.endsWith(".png")) ct = "image/png"; else if (path.endsWith(".svg")) ct = "image/svg+xml";
  File f = LittleFS.open(path, "r");
  s_server.streamFile(f, ct);
  f.close();
}

static void web_task(void*) {
  fdir_wdt_subscribe();
  s_server.on("/", HTTP_GET, handle_root);
  s_server.on("/api/telemetry", HTTP_GET, handle_telemetry);
  s_server.on("/api/hk", HTTP_GET, handle_hk);
  s_server.on("/api/cmd", HTTP_POST, handle_cmd);
  s_server.on("/api/photos", HTTP_GET, handle_photos);
  s_server.on("/api/photo", HTTP_GET, handle_photo);
  s_server.on("/api/photo/capture", HTTP_GET, handle_photo_capture);
  s_server.on("/api/logs", HTTP_GET, handle_logs);
  s_server.on("/api/logs/download", HTTP_GET, handle_log_download);
  s_server.on("/api/events", HTTP_GET, handle_events);
  s_server.onNotFound(handle_static);
  s_server.begin();
  LOGI("WEB", "HTTP server started");

  for (;;) {
    s_server.handleClient();
    fdir_wdt_feed();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void web_task_start() { xTaskCreatePinnedToCore(web_task, "web", 8192, nullptr, 2, nullptr, 0); }
