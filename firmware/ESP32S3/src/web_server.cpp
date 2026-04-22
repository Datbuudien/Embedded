/**
 * web_server.cpp — AsyncWebServer trên SoftAP
 * Route: GET / (dashboard HTML), GET /status (JSON), POST /control
 * Manual Override: timeout 1800s, countdown, auto-reset về AUTO
 */

#include "web_server.h"
#include "config.h"
#include "protocol.h"
#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

// ---- Private State -----------------------------------------
static AsyncWebServer s_server(WEB_SERVER_PORT);

static SensorFrame s_current_frame = {};
static uint8_t     s_current_cmd   = CMD_OFF;
static float       s_ann_prob      = 0.0f;

static bool     s_manual_override  = false;
static uint8_t  s_manual_cmd       = CMD_OFF;
static uint32_t s_override_start_ms = 0;

// ---- HTML Dashboard ----------------------------------------
static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="vi">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Hệ thống Tưới tiêu Thông minh</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body { font-family: 'Segoe UI', sans-serif; background: #0f1923; color: #e0e6ef; min-height: 100vh; padding: 20px; }
    h1 { text-align: center; color: #4fc3f7; margin-bottom: 24px; font-size: 1.5rem; }
    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(160px, 1fr)); gap: 16px; margin-bottom: 24px; }
    .card { background: #1a2635; border-radius: 12px; padding: 20px; text-align: center; border: 1px solid #2a3a4a; }
    .card .label { font-size: 0.75rem; color: #7a9bb5; margin-bottom: 8px; text-transform: uppercase; letter-spacing: 1px; }
    .card .value { font-size: 1.8rem; font-weight: 700; color: #4fc3f7; }
    .card .unit  { font-size: 0.85rem; color: #7a9bb5; }
    .status-bar { background: #1a2635; border-radius: 12px; padding: 16px 24px; margin-bottom: 24px; display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: 12px; border: 1px solid #2a3a4a; }
    .status-bar .mode { font-weight: 700; }
    .mode.auto { color: #4caf50; }
    .mode.manual { color: #ff9800; }
    .pump-on  { color: #4caf50; font-weight: 700; }
    .pump-off { color: #ef5350; font-weight: 700; }
    .rain-yes { color: #29b6f6; font-weight: 700; }
    .controls { display: flex; gap: 12px; flex-wrap: wrap; justify-content: center; }
    button { padding: 12px 28px; border-radius: 8px; border: none; cursor: pointer; font-size: 1rem; font-weight: 600; transition: all 0.2s; }
    #btnOn  { background: #4caf50; color: #fff; }
    #btnOff { background: #ef5350; color: #fff; }
    #btnAuto{ background: #1565c0; color: #fff; }
    button:hover { opacity: 0.85; transform: translateY(-1px); }
    #countdown { font-size: 0.85rem; color: #ff9800; margin-top: 8px; text-align: center; min-height: 20px; }
    .ann-bar { background: #1a2635; border-radius: 12px; padding: 16px 24px; margin-bottom: 24px; border: 1px solid #2a3a4a; }
    .ann-bar .label { font-size: 0.75rem; color: #7a9bb5; margin-bottom: 8px; text-transform: uppercase; }
    .progress-bg { background: #2a3a4a; border-radius: 8px; height: 18px; overflow: hidden; }
    .progress-fill { height: 100%; border-radius: 8px; background: linear-gradient(90deg, #1565c0, #4fc3f7); transition: width 0.5s; }
  </style>
</head>
<body>
  <h1>🌱 Hệ thống Tưới tiêu Thông minh</h1>

  <div id="status-bar" class="status-bar">
    <div>Chế độ: <span id="modeLabel" class="mode">--</span></div>
    <div>Bơm: <span id="pumpLabel">--</span></div>
    <div>Mưa: <span id="rainLabel">--</span></div>
    <div id="timeLabel">--</div>
  </div>

  <div class="grid">
    <div class="card"><div class="label">Độ ẩm đất</div><div class="value" id="soil">--</div><div class="unit">%</div></div>
    <div class="card"><div class="label">Nhiệt độ</div><div class="value" id="temp">--</div><div class="unit">°C</div></div>
    <div class="card"><div class="label">Độ ẩm KK</div><div class="value" id="hum">--</div><div class="unit">%RH</div></div>
    <div class="card"><div class="label">Mức mưa AO</div><div class="value" id="rain">--</div><div class="unit">/ 4095</div></div>
  </div>

  <div class="ann-bar">
    <div class="label">AI Confidence (P tưới)</div>
    <div class="progress-bg"><div class="progress-fill" id="annBar" style="width:0%"></div></div>
    <div style="text-align:right;font-size:0.85rem;margin-top:4px;color:#4fc3f7"><span id="annProb">0</span>%</div>
  </div>

  <div class="controls">
    <button id="btnOn"  onclick="control('on')">💧 Bật bơm</button>
    <button id="btnOff" onclick="control('off')">⏹ Tắt bơm</button>
    <button id="btnAuto" onclick="control('auto')">🤖 Về AUTO</button>
  </div>
  <div id="countdown"></div>

  <script>
    let overrideRemaining = 0;
    let countdownTimer = null;

    async function fetchStatus() {
      try {
        const r = await fetch('/status');
        const d = await r.json();
        document.getElementById('soil').textContent   = d.soil_pct.toFixed(1);
        document.getElementById('temp').textContent   = d.temperature.toFixed(1);
        document.getElementById('hum').textContent    = d.humidity_air.toFixed(1);
        document.getElementById('rain').textContent   = d.rain_raw;
        document.getElementById('rainLabel').innerHTML = d.rain_digital
          ? '<span class="rain-yes">🌧 Đang mưa</span>' : 'Khô';
        document.getElementById('pumpLabel').innerHTML = d.pump_state
          ? '<span class="pump-on">BẬT</span>' : '<span class="pump-off">TẮT</span>';
        const modeEl = document.getElementById('modeLabel');
        modeEl.textContent = d.manual_override ? 'MANUAL' : 'AUTO';
        modeEl.className   = 'mode ' + (d.manual_override ? 'manual' : 'auto');
        document.getElementById('timeLabel').textContent = d.time || '';
        const prob = Math.round(d.ann_prob * 100);
        document.getElementById('annProb').textContent = prob;
        document.getElementById('annBar').style.width  = prob + '%';
        if (d.manual_override && d.override_remaining > 0) {
          overrideRemaining = d.override_remaining;
          startCountdown();
        } else {
          overrideRemaining = 0;
          document.getElementById('countdown').textContent = '';
        }
      } catch(e) { console.warn('Fetch error:', e); }
    }

    function startCountdown() {
      if (countdownTimer) clearInterval(countdownTimer);
      countdownTimer = setInterval(() => {
        if (overrideRemaining <= 0) {
          clearInterval(countdownTimer);
          document.getElementById('countdown').textContent = '';
          return;
        }
        const m = Math.floor(overrideRemaining / 60);
        const s = overrideRemaining % 60;
        document.getElementById('countdown').textContent =
          `Manual mode: còn ${m} phút ${s} giây (tự động về AUTO)`;
        overrideRemaining--;
      }, 1000);
    }

    async function control(cmd) {
      await fetch('/control', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ cmd })
      });
      fetchStatus();
    }

    fetchStatus();
    setInterval(fetchStatus, 2000);
  </script>
</body>
</html>
)rawliteral";

// ---- Implementation ----------------------------------------

void web_server_init() {
    // GET / — Dashboard HTML
    s_server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "text/html", String(INDEX_HTML));
    });

    // GET /status — JSON sensor + system state
    s_server.on("/status", HTTP_GET, [](AsyncWebServerRequest *req) {
        JsonDocument doc;
        doc["soil_pct"]         = s_current_frame.soil_pct;
        doc["temperature"]      = s_current_frame.temperature;
        doc["humidity_air"]     = s_current_frame.humidity_air;
        doc["rain_raw"]         = s_current_frame.rain_raw;
        doc["rain_digital"]     = s_current_frame.rain_digital;
        doc["pump_state"]       = s_current_frame.pump_state;
        doc["manual_override"]  = s_manual_override;
        doc["ann_prob"]         = s_ann_prob;
        uint32_t remaining = web_server_get_override_remaining();
        doc["override_remaining"] = remaining;
        // Không có LVGL clock ở đây — placeholder
        doc["time"] = "";

        char buf[256];
        size_t len = serializeJson(doc, buf, sizeof(buf));
        req->send(200, "application/json", buf);
    });

    // POST /control — {"cmd": "on"|"off"|"auto"}
    s_server.on("/control", HTTP_POST,
        [](AsyncWebServerRequest *req) {},
        nullptr,
        [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, (char*)data, len);
            if (err) {
                req->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                return;
            }
            const char* cmd = doc["cmd"] | "";
            if (strcmp(cmd, "on") == 0) {
                s_manual_override     = true;
                s_manual_cmd          = CMD_ON;
                s_override_start_ms   = millis();
                Serial.println("[WEB] Manual Override: BẬT bơm (30 phút)");
                req->send(200, "application/json", "{\"status\":\"ok\",\"cmd\":\"on\"}");
            } else if (strcmp(cmd, "off") == 0) {
                s_manual_override     = true;
                s_manual_cmd          = CMD_OFF;
                s_override_start_ms   = millis();
                Serial.println("[WEB] Manual Override: TẮT bơm (30 phút)");
                req->send(200, "application/json", "{\"status\":\"ok\",\"cmd\":\"off\"}");
            } else if (strcmp(cmd, "auto") == 0) {
                s_manual_override = false;
                Serial.println("[WEB] Manual Override hủy — về AUTO mode");
                req->send(200, "application/json", "{\"status\":\"ok\",\"cmd\":\"auto\"}");
            } else {
                req->send(400, "application/json", "{\"error\":\"Unknown cmd\"}");
            }
        }
    );

    s_server.begin();
    Serial.printf("[WEB] Server bắt đầu — http://%s/\n", WiFi.softAPIP().toString().c_str());
}

void web_server_update(const SensorFrame& frame, uint8_t pump_cmd, float ann_prob) {
    memcpy(&s_current_frame, &frame, sizeof(SensorFrame));
    s_current_cmd = pump_cmd;
    s_ann_prob    = ann_prob;

    // KI-008: Manual Override tự hủy sau 1800s — không disable được
    if (s_manual_override) {
        uint32_t elapsed_sec = (millis() - s_override_start_ms) / 1000;
        if (elapsed_sec >= MANUAL_OVERRIDE_TIMEOUT_SEC) {
            s_manual_override = false;
            Serial.println("[WEB] Manual Override HẾT HẠN (30 phút) — tự động về AUTO");
        }
    }
}

bool web_server_is_manual_override() {
    return s_manual_override;
}

uint8_t web_server_get_manual_cmd() {
    return s_manual_cmd;
}

uint32_t web_server_get_override_remaining() {
    if (!s_manual_override) return 0;
    uint32_t elapsed_sec = (millis() - s_override_start_ms) / 1000;
    if (elapsed_sec >= MANUAL_OVERRIDE_TIMEOUT_SEC) return 0;
    return MANUAL_OVERRIDE_TIMEOUT_SEC - elapsed_sec;
}
