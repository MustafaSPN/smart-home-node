#pragma once

// ============================================================
// Copy this file to  config.h  and fill in your own values.
// config.h is gitignored so your WiFi / account passwords stay
// off version control.
// ============================================================

// ---- WiFi ----
#define WIFI_SSID     "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// ---- Firebase ----
#define FIREBASE_HOST "YOUR_PROJECT-default-rtdb.europe-west1.firebasedatabase.app"
#define API_KEY       "YOUR_FIREBASE_WEB_API_KEY"

// Firebase Auth account the device signs in with (its UID scopes the RTDB paths)
#define USER_EMAIL    "esp32@example.com"
#define USER_PASSWORD "YOUR_FIREBASE_PASSWORD"

// ---- Cloud Function ping endpoint ----
// The heartbeat authenticates with the Firebase ID token of the session above,
// so no separate API key or shared secret is needed.
#define PING_URL "YOUR_CLOUD_FUNCTION_URL"

// ---- Timing ----
#define PING_INTERVAL   300000UL   // 5 minutes
#define RESTART_INTERVAL 21600000UL // 6 hours

// ---- Board ----
#define LED_PIN 2
