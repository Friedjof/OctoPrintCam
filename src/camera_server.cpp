/**
 * camera_server.cpp
 *
 * Camera endpoints on port 81.
 * Kept in a separate translation unit to avoid the HTTP_GET enum collision
 * between ESPAsyncWebServer and esp_http_server / nghttp http_parser.h.
 *
 * A raw TCP server accepts connections and dispatches them based on the
 * request URI:
 *   GET /stream   → MJPEG multipart stream (runs until client disconnects)
 *   GET /snapshot → single JPEG frame
 *
 * Each accepted connection gets its own FreeRTOS task so the accept loop
 * is never blocked. Multiple simultaneous streaming clients and concurrent
 * snapshot requests work without interference.
 */

// esp_http_server.h must NOT share a translation unit with ESPAsyncWebServer.h
#include "esp_camera.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <Arduino.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <cstring>
#include <cstdio>

#define CAMERA_PORT 81

// Mutex that serialises sensor reconfiguration for high-quality snapshots.
// Only snapshot tasks take this; stream tasks run freely without it.
static SemaphoreHandle_t s_cam_mutex = nullptr;

// ── Helpers ───────────────────────────────────────────────────────────────────

static bool sockSend(int sock, const char *buf, size_t len) {
    while (len > 0) {
        int sent = send(sock, buf, len, 0);
        if (sent <= 0) return false;
        buf += sent;
        len -= sent;
    }
    return true;
}

// Read until the end of the HTTP request header block (\r\n\r\n).
// Fills buf with the first chunk so we can parse the request line.
static void readRequestHeaders(int sock, char *buf, size_t bufLen) {
    size_t total = 0;
    while (total < bufLen - 1) {
        int n = recv(sock, buf + total, bufLen - 1 - total, 0);
        if (n <= 0) break;
        total += n;
        buf[total] = '\0';
        if (strstr(buf, "\r\n\r\n")) break;
    }
}

// ── Per-client task ───────────────────────────────────────────────────────────

static void clientTask(void *arg) {
    int sock = (int)(intptr_t)arg;

    char reqBuf[256] = {};
    readRequestHeaders(sock, reqBuf, sizeof(reqBuf));

    bool isStream   = (strstr(reqBuf, "GET /stream")   != nullptr);
    bool isSnapshot = (strstr(reqBuf, "GET /snapshot") != nullptr);
    bool isCapture  = (strstr(reqBuf, "GET /capture")  != nullptr);

    if (isStream) {
        static const char *streamHeader =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: multipart/x-mixed-replace;boundary=frame\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Cache-Control: no-cache\r\n"
            "Connection: close\r\n"
            "\r\n";

        if (!sockSend(sock, streamHeader, strlen(streamHeader))) goto done;

        char partHdr[64];
        while (true) {
            camera_fb_t *fb = esp_camera_fb_get();
            if (!fb) break;

            int hLen = snprintf(partHdr, sizeof(partHdr),
                "\r\n--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                (unsigned)fb->len);

            bool ok = sockSend(sock, partHdr, hLen) &&
                      sockSend(sock, (const char *)fb->buf, fb->len);

            esp_camera_fb_return(fb);
            if (!ok) break;
        }

    } else if (isSnapshot) {
        // High-quality snapshot: keep VGA framesize to avoid destabilising the
        // camera driver, but drop JPEG quality to 4 (near-lossless).
        // The mutex prevents concurrent snapshot tasks from interfering.
        xSemaphoreTake(s_cam_mutex, portMAX_DELAY);

        bool hasPsram = psramFound();
        sensor_t *sensor = esp_camera_sensor_get();
        if (sensor) sensor->set_quality(sensor, 4);

        // Flush all frame buffers so the quality change takes effect.
        int flushed = 0;
        TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(2000);
        while (flushed < 4 && xTaskGetTickCount() < deadline) {
            camera_fb_t *f = esp_camera_fb_get();
            if (f) { esp_camera_fb_return(f); flushed++; }
            else    vTaskDelay(pdMS_TO_TICKS(50));
        }

        camera_fb_t *fb = esp_camera_fb_get();

        // Restore stream quality immediately.
        if (sensor) sensor->set_quality(sensor, hasPsram ? 15 : 20);

        xSemaphoreGive(s_cam_mutex);

        if (fb) {
            char snapHdr[256];
            int hLen = snprintf(snapHdr, sizeof(snapHdr),
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: image/jpeg\r\n"
                "Content-Length: %u\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Content-Disposition: inline; filename=snapshot.jpg\r\n"
                "Connection: close\r\n"
                "\r\n",
                (unsigned)fb->len);
            sockSend(sock, snapHdr, hLen);
            sockSend(sock, (const char *)fb->buf, fb->len);
            esp_camera_fb_return(fb);
        } else {
            static const char *err =
                "HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\n\r\n";
            sockSend(sock, err, strlen(err));
        }

    } else if (isCapture) {
        // Fast single-frame capture for OctoPrint timelapse / FFmpeg.
        // No warm-up discard — sequential timelapse captures are already
        // spaced far enough apart that the sensor is always ready.
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb) {
            char capHdr[256];
            int hLen = snprintf(capHdr, sizeof(capHdr),
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: image/jpeg\r\n"
                "Content-Length: %u\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Connection: close\r\n"
                "\r\n",
                (unsigned)fb->len);
            sockSend(sock, capHdr, hLen);
            sockSend(sock, (const char *)fb->buf, fb->len);
            esp_camera_fb_return(fb);
        } else {
            static const char *err =
                "HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\n\r\n";
            sockSend(sock, err, strlen(err));
        }

    } else {
        static const char *notFound =
            "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n";
        sockSend(sock, notFound, strlen(notFound));
    }

done:
    // Graceful TCP close: send FIN, drain remaining receive buffer, then close.
    // Without this the kernel sends RST which browsers report as "connection reset".
    shutdown(sock, SHUT_WR);
    { char drain[64]; while (recv(sock, drain, sizeof(drain), 0) > 0) {} }
    close(sock);
    vTaskDelete(nullptr);
}

// ── Accept loop ───────────────────────────────────────────────────────────────

static void serverTask(void *) {
    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) {
        Serial.println("[STREAM] socket() failed");
        vTaskDelete(nullptr);
        return;
    }

    int yes = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(CAMERA_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        Serial.println("[STREAM] bind() failed");
        close(server);
        vTaskDelete(nullptr);
        return;
    }

    listen(server, 4);
    Serial.printf("[STREAM] Server started on port %d\n", CAMERA_PORT);

    while (true) {
        int client = accept(server, nullptr, nullptr);
        if (client < 0) continue;

        if (xTaskCreate(clientTask, "cam_client", 4096,
                        (void *)(intptr_t)client, 5, nullptr) != pdPASS) {
            Serial.println("[STREAM] Failed to spawn client task");
            close(client);
        }
    }
}

// ── Public entry point ────────────────────────────────────────────────────────

void startCameraServer() {
    s_cam_mutex = xSemaphoreCreateMutex();
    if (xTaskCreate(serverTask, "cam_server", 4096, nullptr, 5, nullptr) != pdPASS) {
        Serial.println("[STREAM] Failed to start camera server task");
    }
}
