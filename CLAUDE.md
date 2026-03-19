# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
make              # Build firmware (also builds web UI → C headers first)
make flash        # Flash to auto-detected port
make flash 1      # Flash to /dev/ttyACM1
make monitor      # Serial monitor (115200 baud)
make run          # Build + flash + monitor
make run 1        # Build + flash + monitor on /dev/ttyACM1
make clean        # Remove build artifacts, web dist, generated headers
make list         # List connected ESP32 devices
```

The build system is `make` → internally calls `pio run --environment esp32cam`. There are no tests.

## Architecture

Two HTTP servers run concurrently on the ESP32:

**Port 80 – `ESPAsyncWebServer`** (`src/main.cpp`)
- WebSocket at `/ws` — SUSCam-compatible protocol (text commands + binary JPEG frames)
- REST API: `GET /api/status|position|limits`, `POST /api/move`, `POST /api/move/up|down|left|right|center`, `POST /api/light/on|off|toggle`
- Root `GET /` returns the same JSON as `/api/status`

**Port 81 – native `esp_http_server`** (`src/camera_server.cpp`)
- `GET /stream` — MJPEG live stream (chunked, infinite loop)
- `GET /snapshot` — single JPEG frame

### Critical Header Isolation

`ESPAsyncWebServer.h` and `esp_http_server.h` **must never be included in the same translation unit** — both define `HTTP_GET`, `HTTP_DELETE`, etc. in incompatible enums. This is why the camera server lives in its own `.cpp` file with no ESPAsyncWebServer include.

### Key Files

| File | Purpose |
|---|---|
| `include/config.h` | WiFi credentials, all GPIO pins, servo limits |
| `src/main.cpp` | WiFi, mDNS, camera init, servos, WebSocket, REST API |
| `src/camera_server.cpp` | MJPEG stream + snapshot via native `esp_http_server` |
| `include/camera_server.h` | Public interface: `startCameraServer()` |
| `web/` | Vite-based web UI (currently a template stub) |
| `lib/WebService/web_files.h` | Auto-generated — do not edit manually |
| `tools/web-to-header.py` | Converts `web/dist/` → gzipped C headers |

### Hardware: AI Thinker ESP32-CAM

- Camera pins are fully defined in `config.h`
- Servo pan (X): **GPIO 13**, tilt (Y): **GPIO 12** — these reuse SD-card slot pins, so no SD card can be inserted
- Flash LED: **GPIO 4**
- **GPIO 16** must not be used (PSRAM clock)
- PSRAM present: camera uses VGA (640×480), 3 frame buffers, JPEG quality 15; without PSRAM: QVGA, 1 frame buffer, quality 20

### WebSocket Protocol (SUSCam-compatible)

Text commands: `up`, `down`, `left`, `right`, `center`, `getframe`, `get_pos`, `get_limits`, `client_count`, `light_on`, `light_off`, `{"x":90,"y":45}`

Position feedback is sent as JSON after every move: `{"x":90,"y":45}`. Binary frames (JPEG bytes) are sent in response to `getframe`.

### Web UI Pipeline

`web/` (Vite) → `npm run build` → `web/dist/` → `tools/web-to-header.py` → `lib/WebService/web_files.h`

The generated header is included by firmware to serve the UI from flash. The `make build` target runs this pipeline automatically.

### OctoPrint Integration

- Webcam stream URL: `http://octocam.local:81/stream`
- Snapshot URL: `http://octocam.local:81/snapshot`
- Motor control via REST `POST` calls or WebSocket
