// Camera payload. Deliberately kept simple in phase 0 -- the owner wants the camera to be a
// secondary payload, not the centerpiece. Fixes carried over from the stock firmware:
//   - esp_camera_fb_get() had an unbounded "while(!fb)" spin; here it is timeout-bounded.
//   - a whole JPEG was base64-encoded into one heap String; here the caller streams the file
//     in chunks (see apps/web.cpp) instead of building one huge buffer.
#pragma once
#include <Arduino.h>

bool camera_init();
bool camera_present();
// Captures a frame, saves it to LittleFS (ring of MYSAT_MAX_PHOTOS), returns the new photo id or
// -1 on failure/timeout. Attaches whatever attitude/sun/power context the caller passes in.
int  camera_capture(const char* iso_timestamp, float roll, float pitch, float yaw, bool sun_in_fov);
String camera_index_json();
bool camera_photo_path(int id, String& outPath, String& outTimestamp);
