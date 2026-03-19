#pragma once

// Starts the native esp_http_server on port 81.
// Provides:  GET /stream   (MJPEG)
//            GET /snapshot (single JPEG)
void startCameraServer();
