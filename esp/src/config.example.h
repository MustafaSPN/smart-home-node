#pragma once

// ============================================================
// Copy this file to  config.h  and fill in your own values.
// config.h is gitignored so your WiFi / account passwords stay
// off version control.
// ============================================================

// ============================================================
// WIFI
// ============================================================
#define WIFI_SSID     "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// ============================================================
// FIREBASE (only used by the `cloud` environment)
// Same project as the HomeNode iOS app.
// ============================================================
#define FIREBASE_HOST    "YOUR_PROJECT-default-rtdb.europe-west1.firebasedatabase.app"
#define FIREBASE_API_KEY "YOUR_FIREBASE_WEB_API_KEY"

// Log in with a Firebase Auth account (so data lands under users/{uid}).
// Create a dedicated account for the device, e.g. esp32@example.com.
#define USER_EMAIL    "esp32@example.com"
#define USER_PASSWORD "YOUR_FIREBASE_PASSWORD"

// ---- PC Wake-on-LAN + power-outage ping (cloud env) ----
// The PC MAC address is read from users/{uid}/profile/mac_address (set in app).
// The heartbeat authenticates with the Firebase ID token of the session above,
// so no separate API key or shared secret is needed.
#define PING_URL "YOUR_CLOUD_FUNCTION_URL"
#define PING_INTERVAL_MS 300000UL // 5 minutes

// ============================================================
// PINS (ESP32-S3 N16R8 — safe GPIOs)
// ============================================================
#define IR_RECV_PIN 15 // KY-022 IR receiver  (signal OUT)
#define IR_SEND_PIN 4  // KY-005 IR transmitter (signal S / DAT)
#define DHT_PIN     5  // DHT11 data pin
