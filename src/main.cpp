/**
 * OctoPrintCam – ESP32-CAM with pan/tilt servo control
 *
 * Port 80  – ESPAsyncWebServer
 *   WebSocket  ws://octocam.local/ws        (SUSCam-compatible protocol)
 *   GET  /api/status                         JSON: position, limits, flash, IP
 *   GET  /api/position                       JSON: {"x":90,"y":45}
 *   GET  /api/limits                         JSON: x/y min/max
 *   POST /api/move          body: {"x":90,"y":45}
 *   POST /api/move/up|down|left|right|center
 *   POST /api/light/on|off|toggle
 *
 * Port 81  – native esp_http_server
 *   GET  /stream                             MJPEG live stream
 *   GET  /snapshot                           Single JPEG frame
 *
 * OctoPrint webcam URL : http://octocam.local:81/stream
 * OctoPrint snapshot   : http://octocam.local:81/snapshot
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <ESPAsyncWebServer.h>
#include "esp_camera.h"
#include "config.h"
#include "camera_server.h"

// ── Globals ───────────────────────────────────────────────────────────────────

AsyncWebServer server(80);
AsyncWebSocket  ws("/ws");

Servo servoX;   // Pan  – horizontal
Servo servoY;   // Tilt – vertical

int posX     = PAN_START;
int posY     = TILT_START;
bool flashOn = false;


// ── Servo helpers ─────────────────────────────────────────────────────────────

void applyServos() {
    servoX.write(posX);
    servoY.write(posY);
}

String jsonPosition() {
    JsonDocument doc;
    doc["x"] = posX;
    doc["y"] = posY;
    String s; serializeJson(doc, s); return s;
}

String jsonLimits() {
    JsonDocument doc;
    doc["x_min"] = PAN_MIN;  doc["x_max"] = PAN_MAX;
    doc["y_min"] = TILT_MIN; doc["y_max"] = TILT_MAX;
    String s; serializeJson(doc, s); return s;
}

String jsonStatus() {
    JsonDocument doc;
    doc["x"]          = posX;
    doc["y"]          = posY;
    doc["x_min"]      = PAN_MIN;  doc["x_max"] = PAN_MAX;
    doc["y_min"]      = TILT_MIN; doc["y_max"] = TILT_MAX;
    doc["flash"]      = flashOn;
    doc["ip"]         = WiFi.localIP().toString();
    doc["stream_url"] = "http://" + WiFi.localIP().toString() + ":81/stream";
    doc["snapshot_url"] = "http://" + WiFi.localIP().toString() + ":81/snapshot";
    doc["ws_url"]     = "ws://" + WiFi.localIP().toString() + "/ws";
    doc["clients"]    = (int)ws.count();
    String s; serializeJson(doc, s); return s;
}

// ── Camera init ───────────────────────────────────────────────────────────────

bool initCamera() {
    camera_config_t cfg;
    cfg.pin_pwdn     = PWDN_GPIO_NUM;
    cfg.pin_reset    = RESET_GPIO_NUM;
    cfg.pin_xclk     = XCLK_GPIO_NUM;
    cfg.pin_sccb_sda = SIOD_GPIO_NUM;
    cfg.pin_sccb_scl = SIOC_GPIO_NUM;
    cfg.pin_d7       = Y9_GPIO_NUM;
    cfg.pin_d6       = Y8_GPIO_NUM;
    cfg.pin_d5       = Y7_GPIO_NUM;
    cfg.pin_d4       = Y6_GPIO_NUM;
    cfg.pin_d3       = Y5_GPIO_NUM;
    cfg.pin_d2       = Y4_GPIO_NUM;
    cfg.pin_d1       = Y3_GPIO_NUM;
    cfg.pin_d0       = Y2_GPIO_NUM;
    cfg.pin_vsync    = VSYNC_GPIO_NUM;
    cfg.pin_href     = HREF_GPIO_NUM;
    cfg.pin_pclk     = PCLK_GPIO_NUM;
    cfg.xclk_freq_hz = 24000000;
    cfg.ledc_timer   = LEDC_TIMER_0;
    cfg.ledc_channel = LEDC_CHANNEL_0;
    cfg.pixel_format = PIXFORMAT_JPEG;
    cfg.grab_mode    = CAMERA_GRAB_LATEST;

    if (psramFound()) {
        cfg.frame_size   = FRAMESIZE_VGA;
        cfg.jpeg_quality = 15;
        cfg.fb_count     = 3;
        cfg.fb_location  = CAMERA_FB_IN_PSRAM;
    } else {
        cfg.frame_size   = FRAMESIZE_QVGA;
        cfg.jpeg_quality = 20;
        cfg.fb_count     = 1;
        cfg.fb_location  = CAMERA_FB_IN_DRAM;
    }

    esp_err_t err = esp_camera_init(&cfg);
    if (err != ESP_OK) {
        Serial.printf("[CAM] Init failed: 0x%x\n", err);
        return false;
    }

    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        s->set_framesize(s, FRAMESIZE_VGA);
        s->set_quality(s, psramFound() ? 15 : 20);
        s->set_gainceiling(s, GAINCEILING_4X);
        s->set_bpc(s, 1);
        s->set_wpc(s, 1);
        s->set_lenc(s, 1);
    }

    Serial.println("[CAM] Init OK");
    return true;
}

// ── WebSocket (SUSCam-compatible) ─────────────────────────────────────────────

void wsSendPos(AsyncWebSocketClient *c) {
    c->text(jsonPosition());
}

void handleWsText(AsyncWebSocketClient *c, const String &msg) {
    if      (msg == "up")     { posY = max(TILT_MIN, posY - SERVO_STEP); applyServos(); wsSendPos(c); }
    else if (msg == "down")   { posY = min(TILT_MAX, posY + SERVO_STEP); applyServos(); wsSendPos(c); }
    else if (msg == "left")   { posX = max(PAN_MIN,  posX - SERVO_STEP); applyServos(); wsSendPos(c); }
    else if (msg == "right")  { posX = min(PAN_MAX,  posX + SERVO_STEP); applyServos(); wsSendPos(c); }
    else if (msg == "center") { posX = PAN_START; posY = TILT_START;     applyServos(); wsSendPos(c); }
    else if (msg == "get_pos")    { wsSendPos(c); }
    else if (msg == "get_limits") { c->text(jsonLimits()); }
    else if (msg == "client_count") {
        JsonDocument doc; doc["clients"] = (int)ws.count();
        String s; serializeJson(doc, s); c->text(s);
    }
    else if (msg == "light_on")  { flashOn = true;  digitalWrite(FLASH_LED_PIN, HIGH); }
    else if (msg == "light_off") { flashOn = false; digitalWrite(FLASH_LED_PIN, LOW);  }
    else if (msg == "getframe") {
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb) { c->binary(fb->buf, fb->len); esp_camera_fb_return(fb); }
    }
    else {
        // JSON position command: {"x": 90, "y": 45}
        JsonDocument doc;
        if (!deserializeJson(doc, msg)) {
            if (!doc["x"].isNull()) posX = constrain((int)doc["x"], PAN_MIN,  PAN_MAX);
            if (!doc["y"].isNull()) posY = constrain((int)doc["y"], TILT_MIN, TILT_MAX);
            applyServos();
            wsSendPos(c);
        }
    }
}

void onWsEvent(AsyncWebSocket *srv, AsyncWebSocketClient *c,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            Serial.printf("[WS] Client #%u connected\n", c->id());
            wsSendPos(c);
            break;
        case WS_EVT_DISCONNECT:
            Serial.printf("[WS] Client #%u disconnected\n", c->id());
            break;
        case WS_EVT_DATA: {
            auto *info = (AwsFrameInfo *)arg;
            if (info->final && info->index == 0 && info->len == len
                && info->opcode == WS_TEXT) {
                handleWsText(c, String((char *)data, len));
            }
            break;
        }
        default: break;
    }
}

// ── REST API helpers ──────────────────────────────────────────────────────────

static void sendJson(AsyncWebServerRequest *req, const String &json, int code = 200) {
    auto *res = req->beginResponse(code, "application/json", json);
    res->addHeader("Access-Control-Allow-Origin", "*");
    req->send(res);
}

// ── setup ─────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    Serial.println("\n[BOOT] OctoPrintCam starting...");

    // Flash LED
    pinMode(FLASH_LED_PIN, OUTPUT);
    digitalWrite(FLASH_LED_PIN, LOW);

    // Camera
    if (!initCamera()) {
        Serial.println("[ERROR] Camera init failed – halting");
        while (true) delay(1000);
    }

    // Servos
    servoX.attach(SERVO_PAN_PIN,  500, 2400);
    servoY.attach(SERVO_TILT_PIN, 500, 2400);
    applyServos();
    Serial.println("[SERVO] Attached – pan=" + String(posX) + " tilt=" + String(posY));

    // WiFi
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("[WIFI] Connecting");
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    WiFi.setSleep(false);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    Serial.printf("\n[WIFI] %s\n", WiFi.localIP().toString().c_str());

    // mDNS
    if (MDNS.begin(MDNS_NAME)) {
        MDNS.addService("http", "tcp", 80);
        MDNS.addService("http", "tcp", 81);
        Serial.printf("[mDNS] http://%s.local\n", MDNS_NAME);
    }

    // WebSocket
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);

    // ── API routes ────────────────────────────────────────────────────────────

    server.on("/api/status",   HTTP_GET, [](AsyncWebServerRequest *req) { sendJson(req, jsonStatus()); });
    server.on("/api/position", HTTP_GET, [](AsyncWebServerRequest *req) { sendJson(req, jsonPosition()); });
    server.on("/api/limits",   HTTP_GET, [](AsyncWebServerRequest *req) { sendJson(req, jsonLimits()); });

    // POST /api/move  body: {"x":90,"y":45}
    server.on("/api/move", HTTP_POST,
        [](AsyncWebServerRequest *req) {},
        nullptr,
        [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t, size_t) {
            JsonDocument doc;
            if (!deserializeJson(doc, data, len)) {
                if (!doc["x"].isNull()) posX = constrain((int)doc["x"], PAN_MIN,  PAN_MAX);
                if (!doc["y"].isNull()) posY = constrain((int)doc["y"], TILT_MIN, TILT_MAX);
                applyServos();
                ws.textAll(jsonPosition());
            }
            sendJson(req, jsonPosition());
        }
    );

    server.on("/api/move/up",     HTTP_POST, [](AsyncWebServerRequest *req) {
        posY = max(TILT_MIN, posY - SERVO_STEP); applyServos(); ws.textAll(jsonPosition());
        sendJson(req, jsonPosition());
    });
    server.on("/api/move/down",   HTTP_POST, [](AsyncWebServerRequest *req) {
        posY = min(TILT_MAX, posY + SERVO_STEP); applyServos(); ws.textAll(jsonPosition());
        sendJson(req, jsonPosition());
    });
    server.on("/api/move/left",   HTTP_POST, [](AsyncWebServerRequest *req) {
        posX = max(PAN_MIN, posX - SERVO_STEP);  applyServos(); ws.textAll(jsonPosition());
        sendJson(req, jsonPosition());
    });
    server.on("/api/move/right",  HTTP_POST, [](AsyncWebServerRequest *req) {
        posX = min(PAN_MAX, posX + SERVO_STEP);  applyServos(); ws.textAll(jsonPosition());
        sendJson(req, jsonPosition());
    });
    server.on("/api/move/center", HTTP_POST, [](AsyncWebServerRequest *req) {
        posX = PAN_START; posY = TILT_START; applyServos(); ws.textAll(jsonPosition());
        sendJson(req, jsonPosition());
    });

    server.on("/api/light/on",  HTTP_POST, [](AsyncWebServerRequest *req) {
        flashOn = true;  digitalWrite(FLASH_LED_PIN, HIGH);
        sendJson(req, "{\"flash\":true}");
    });
    server.on("/api/light/off", HTTP_POST, [](AsyncWebServerRequest *req) {
        flashOn = false; digitalWrite(FLASH_LED_PIN, LOW);
        sendJson(req, "{\"flash\":false}");
    });
    server.on("/api/light/toggle", HTTP_POST, [](AsyncWebServerRequest *req) {
        flashOn = !flashOn; digitalWrite(FLASH_LED_PIN, flashOn ? HIGH : LOW);
        JsonDocument doc; doc["flash"] = flashOn;
        String s; serializeJson(doc, s); sendJson(req, s);
    });

    // Root → status
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) { sendJson(req, jsonStatus()); });

    // CORS preflight
    server.onNotFound([](AsyncWebServerRequest *req) {
        if (req->method() == HTTP_OPTIONS) {
            auto *res = req->beginResponse(200);
            res->addHeader("Access-Control-Allow-Origin",  "*");
            res->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
            res->addHeader("Access-Control-Allow-Headers", "Content-Type");
            req->send(res);
        } else {
            req->send(404, "text/plain", "Not Found");
        }
    });

    server.begin();
    Serial.println("[HTTP] API server started on port 80");

    startCameraServer();

    Serial.println("\n[BOOT] Ready!");
    Serial.printf("  Webcam stream  : http://%s.local:81/stream\n",   MDNS_NAME);
    Serial.printf("  Snapshot       : http://%s.local:81/snapshot\n", MDNS_NAME);
    Serial.printf("  API status     : http://%s.local/api/status\n",  MDNS_NAME);
    Serial.printf("  WebSocket      : ws://%s.local/ws\n",            MDNS_NAME);
}

void loop() {
    ws.cleanupClients();
    delay(10);
}
