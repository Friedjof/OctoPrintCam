# OctoPrintCam

ESP32-CAM firmware with pan/tilt servo control, MJPEG streaming, WebSocket control, and a REST API. Designed as a drop-in webcam for OctoPrint.

## Hardware

- **Board:** AI Thinker ESP32-CAM
- **Camera:** OV2640 (onboard)
- **Servo pan (X-axis):** GPIO 13
- **Servo tilt (Y-axis):** GPIO 12
- **Flash LED:** GPIO 4

The servo pins reuse the SD card slot signals — no SD card can be inserted while servos are connected.

GPIO 16 must remain unused (PSRAM clock line).

## Features

- MJPEG live stream on port 81
- Single JPEG snapshot on port 81
- WebSocket control (SUSCam-compatible protocol) on port 80
- REST API for position, limits, light, and status on port 80
- mDNS hostname: `octocam.local`
- Pan/tilt servo control via WebSocket commands or REST
- Automatic sensor tuning after camera init (BPC, WPC, lens correction)

## Configuration

Edit `include/config.h` before building:

```c
#define WIFI_SSID     "your-ssid"
#define WIFI_PASSWORD "your-password"
#define MDNS_NAME     "octocam"
```

Servo limits and GPIO assignments are also defined in `config.h`. Defaults: pan 0–180°, tilt 0–90°, step 5° per command.

## Build and Flash

Requires [PlatformIO](https://platformio.org/) and Python 3.

```bash
make          # Build firmware (also builds web UI and generates C headers)
make flash    # Flash to auto-detected port
make flash 1  # Flash to /dev/ttyACM1
make monitor  # Serial monitor at 115200 baud
make run      # Build + flash + monitor
make run 1    # Build + flash + monitor on /dev/ttyACM1
make clean    # Remove build artifacts, web dist, generated headers
make list     # List connected ESP32 devices
```

After changing `sdkconfig.board` or `platformio.ini`, always run `make clean` before `make` to ensure the new settings are picked up.

## Endpoints

### Port 81 — Camera server (native esp_http_server)

| Method | Path        | Description              |
|--------|-------------|--------------------------|
| GET    | `/stream`   | MJPEG live stream        |
| GET    | `/snapshot` | Single JPEG frame        |

### Port 80 — API server (ESPAsyncWebServer)

| Method | Path                              | Description                        |
|--------|-----------------------------------|------------------------------------|
| GET    | `/`                               | Same as `/api/status`              |
| GET    | `/api/status`                     | Full status JSON                   |
| GET    | `/api/position`                   | Current servo position             |
| GET    | `/api/limits`                     | Servo min/max limits               |
| POST   | `/api/move`                       | Move to position `{"x":90,"y":45}` |
| POST   | `/api/move/up\|down\|left\|right\|center` | Step move                  |
| POST   | `/api/light/on\|off\|toggle`      | Flash LED control                  |
| WS     | `/ws`                             | WebSocket (SUSCam-compatible)      |

### WebSocket Commands

Text commands: `up`, `down`, `left`, `right`, `center`, `getframe`, `get_pos`, `get_limits`, `client_count`, `light_on`, `light_off`, `{"x":90,"y":45}`

After every move the device sends back the current position as JSON: `{"x":90,"y":45}`. In response to `getframe` a binary JPEG frame is sent.

## OctoPrint Integration

Set these URLs in OctoPrint under **Settings > Webcam & Timelapse**:

| Field        | Value                             |
|--------------|-----------------------------------|
| Stream URL   | `http://octocam.local:81/stream`  |
| Snapshot URL | `http://octocam.local:81/snapshot`|

## Reverse Proxy Setup (Nginx Proxy Manager)

When placing the camera behind a reverse proxy, two issues need to be addressed:

**1. Request header size**

Browsers send cookies and other headers along with every request. The ESP32's HTTP server has a limited header buffer. The project ships a `sdkconfig.board` file that raises this limit to 2048 bytes, but even that may not be enough when large session cookies are present. The reliable fix is to strip cookies in nginx before forwarding to the ESP32.

**2. URI prefix stripping**

The ESP32 only knows `/stream` and `/snapshot`. If nginx exposes them under a subpath like `/cam/`, the prefix must be stripped before the request reaches the ESP32.

### Nginx Proxy Manager — Advanced tab

Add the following to the existing proxy host for your OctoPrint domain (e.g. `octoprint.example.com`). Replace `192.168.x.x` with the ESP32's local IP address.

```nginx
location /cam/ {
    proxy_pass http://192.168.x.x:81/;
    proxy_http_version 1.1;
    proxy_set_header Host $host;
    proxy_set_header X-Real-IP $remote_addr;
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
    proxy_set_header X-Forwarded-Proto $scheme;
    proxy_set_header Cookie "";
    proxy_set_header Authorization "";
    proxy_buffering off;
    proxy_read_timeout 3600s;
}
```

Key points:
- The trailing slash in `proxy_pass` strips the `/cam` prefix — `/cam/stream` becomes `/stream` on the ESP32.
- `proxy_set_header Cookie ""` and `Authorization ""` prevent large browser headers from reaching the ESP32 and triggering the "Header fields are too long" error.
- `proxy_buffering off` is required for MJPEG streaming — without it nginx buffers the response and the stream never reaches the client.
- `proxy_read_timeout 3600s` keeps the stream connection alive without timeout.

Then configure OctoPrint with the proxy URLs:

| Field        | Value                                         |
|--------------|-----------------------------------------------|
| Stream URL   | `http://octoprint.example.com/cam/stream`     |
| Snapshot URL | `http://octoprint.example.com/cam/snapshot`   |

## Project Structure

```
include/
  config.h              WiFi credentials, GPIO pins, servo limits
src/
  main.cpp              WiFi, mDNS, camera init, servos, WebSocket, REST API
  camera_server.cpp     MJPEG stream and snapshot via native esp_http_server
include/
  camera_server.h       Public interface: startCameraServer()
web/                    Vite-based web UI
tools/
  web-to-header.py      Converts web/dist/ to gzipped C headers
lib/WebService/
  web_files.h           Auto-generated — do not edit manually
sdkconfig.board         ESP-IDF Kconfig overrides (header buffer size)
platformio.ini          PlatformIO build configuration
```

## Architecture Notes

Two HTTP servers run concurrently:

- **Port 80** uses `ESPAsyncWebServer` for WebSocket and REST.
- **Port 81** uses the native `esp_http_server` for camera streaming.

These two server libraries define `HTTP_GET` and related constants in incompatible enums. They must never be included in the same translation unit. This is why the camera server lives in its own `.cpp` file with no `ESPAsyncWebServer` include.

With PSRAM the camera runs at VGA (640×480) with 3 frame buffers and JPEG quality 15. Without PSRAM it falls back to QVGA, 1 frame buffer, and quality 20.
