// ============================================================
// HOME NODE — ESP32 Firmware (PlatformIO)
// ============================================================
// Features:
//   1. Logs in with user's Firebase email/password
//   2. Reads MAC address from Firebase users/{uid}/profile/mac_address
//   3. Listens to users/{uid}/command/pc_on
//   4. Sends Wake on LAN magic packet when command received
//   5. Pings Cloud Function every 5 minutes, authenticated with the
//      Firebase ID token of the signed-in session
//   6. Automatic WiFi reconnection + LED status
// ============================================================

#include <Arduino.h>
#include <Firebase_ESP_Client.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <addons/RTDBHelper.h>
#include <addons/TokenHelper.h>

// ============================================================
// CONFIGURATION
// ============================================================
// All WiFi / Firebase / endpoint settings live in config.h, which is
// gitignored. Copy src/config.example.h -> src/config.h and fill it in.
// ============================================================

#include "config.h"
#include "google_roots.h"

// ============================================================
// FORWARD DECLARATIONS
// ============================================================

void wifiConnect();
void loadMacFromFirebase();
void parseMac(String mac);
void sendPing();
bool syncClock(unsigned long timeoutMs);
void streamCallback(FirebaseStream data);
void streamTimeoutCallback(bool timeout);
void sendWoL();

// ============================================================
// GLOBAL VARIABLES
// ============================================================

FirebaseData fbdo;      // Stream ONLY — never use for other operations
FirebaseData fbdoMac;   // Separate object for MAC read
FirebaseData fbdoWrite; // Separate object for command-flag reset writes
FirebaseAuth auth;
FirebaseConfig config;

WiFiUDP udp;

unsigned long lastPingTime = 0;
unsigned long lastWiFiCheck = 0;
unsigned long bootMillis = 0;
unsigned long lastCommandActivity = 0;
bool firebaseReady = false;
volatile bool pcWakeRequested = false; // Set by stream callback, handled in loop()
String userUID = "";                   // Populated after login
String pcMacString = "";               // Loaded from Firebase
byte pcMacBytes[6] = {0, 0, 0, 0, 0, 0};
bool macLoaded = false;
// TLS certificate validation needs a correct wall clock.
bool clockSynced = false;
unsigned long lastClockRetry = 0;

// ============================================================
// SETUP
// ============================================================

void setup() {
  Serial.begin(115200);
  Serial.println("\n🏠 Home Node ESP32 Initializing...");

  bootMillis = millis();
  lastCommandActivity = millis();

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  wifiConnect();

  configTime(0, 0, "pool.ntp.org", "time.google.com");
  clockSynced = syncClock(20000);

  // Configure Firebase
  config.api_key = API_KEY;
  config.database_url = FIREBASE_HOST;

  Serial.println("Logging into Firebase...");
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  config.token_status_callback = tokenStatusCallback;

  Firebase.begin(&config, &auth);
  Firebase.reconnectNetwork(true);

  // Wait for auth to complete and get UID
  Serial.println("Waiting for Firebase auth...");
  unsigned long authStart = millis();
  while (auth.token.uid.empty() && millis() - authStart < 30000) {
    Firebase.ready(); // process token
    delay(300);
  }

  if (!auth.token.uid.empty()) {
    userUID = String(auth.token.uid.c_str());
    Serial.println("✅ Firebase auth OK! UID: " + userUID);

    // Load MAC address from Firebase
    loadMacFromFirebase();

    // Start listening to command/pc_on
    String commandPath = "/users/" + userUID + "/command/pc_on";
    if (Firebase.RTDB.beginStream(&fbdo, commandPath)) {
      Serial.println("✅ Stream started: " + commandPath);
      Firebase.RTDB.setStreamCallback(&fbdo, streamCallback,
                                      streamTimeoutCallback);
    } else {
      Serial.println("❌ Stream error: " + String(fbdo.errorReason().c_str()));
    }

    firebaseReady = true;
    Serial.println("✅ System ready!");
    sendPing();
  } else {
    Serial.println("❌ Firebase auth failed! Check email/password.");
  }
}

// ============================================================
// LOOP
// ============================================================

void loop() {
  // WiFi check every 30 seconds
  if (millis() - lastWiFiCheck > 30000) {
    lastWiFiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("⚠️ WiFi lost! Reconnecting...");
      digitalWrite(LED_PIN, LOW);
      wifiConnect();
    }
  }

  // Keeps the Firebase ID token refreshed ahead of expiry.
  Firebase.ready();

  if (!clockSynced && millis() - lastClockRetry > 60000) {
    lastClockRetry = millis();
    clockSynced = syncClock(5000);
  }

  // Ping every 5 minutes
  if (firebaseReady && millis() - lastPingTime >= PING_INTERVAL) {
    sendPing();
  }

  // Handle PC wake request flagged by the stream callback (non-blocking)
  if (pcWakeRequested) {
    pcWakeRequested = false;
    lastCommandActivity = millis();
    Serial.println("🖥️ Processing PC wake request...");

    loadMacFromFirebase();
    if (macLoaded) {
      sendWoL();
    } else {
      Serial.println("❌ Cannot send WoL: MAC address not set.");
    }

    // Reset the command flag using a dedicated FirebaseData handle so
    // the active stream (fbdo) is never touched by a write operation.
    if (Firebase.ready()) {
      String commandPath = "/users/" + userUID + "/command/pc_on";
      if (Firebase.RTDB.setBool(&fbdoWrite, commandPath, false)) {
        Serial.println("✅ Command flag reset");
      } else {
        Serial.println("❌ Reset failed: " +
                       String(fbdoWrite.errorReason().c_str()));
      }
    }
  }

  // Scheduled self-restart — recovers from heap fragmentation, expired
  // auth tokens, or silently dead streams. Only restarts when idle to
  // avoid interrupting an in-flight wake command.
  if (millis() - bootMillis >= RESTART_INTERVAL && !pcWakeRequested &&
      millis() - lastCommandActivity > 60000) {
    Serial.println("🔄 Scheduled restart (6h uptime reached)...");
    delay(200);
    ESP.restart();
  }

  // LED: solid = connected, blink = issue
  if (WiFi.status() == WL_CONNECTED && firebaseReady) {
    digitalWrite(LED_PIN, HIGH);
  } else {
    digitalWrite(LED_PIN, (millis() / 500) % 2);
  }

  delay(100);
}

// ============================================================
// WIFI CONNECTION
// ============================================================

void wifiConnect() {
  Serial.print("📶 Connecting to WiFi: ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi connected! IP: " + WiFi.localIP().toString());
    digitalWrite(LED_PIN, HIGH);
  } else {
    Serial.println("\n❌ WiFi failed! Restarting...");
    delay(3000);
    ESP.restart();
  }
}

// ============================================================
// LOAD MAC ADDRESS FROM FIREBASE
// ============================================================

void loadMacFromFirebase() {
  String macPath = "/users/" + userUID + "/profile/mac_address";
  Serial.println("📥 Reading MAC from: " + macPath);

  if (Firebase.RTDB.getString(&fbdoMac, macPath)) {
    String mac = String(fbdoMac.stringData().c_str());
    mac.trim();
    if (mac.length() == 17) { // AA:BB:CC:DD:EE:FF
      parseMac(mac);
      Serial.println("✅ MAC loaded: " + mac);
      macLoaded = true;
    } else {
      Serial.println("⚠️ Invalid MAC format in Firebase: " + mac);
    }
  } else {
    Serial.println(
        "⚠️ MAC not set in Firebase yet. Set it in the app Profile page.");
  }
}

// Parse "AA:BB:CC:DD:EE:FF" into byte array
void parseMac(String mac) {
  for (int i = 0; i < 6; i++) {
    String byteStr = mac.substring(i * 3, i * 3 + 2);
    pcMacBytes[i] = (byte)strtol(byteStr.c_str(), NULL, 16);
  }
}

// ============================================================
// CLOCK — needed before certificates can be validated
// ============================================================

// Returns true once NTP has set a plausible wall clock (past 2021).
bool syncClock(unsigned long timeoutMs) {
  unsigned long t0 = millis();
  while (millis() - t0 < timeoutMs) {
    if (time(nullptr) > 1609459200) { // 2021-01-01
      Serial.println("🕒 Clock synced.");
      return true;
    }
    delay(200);
  }
  Serial.println("⚠️ Clock not synced yet — heartbeat deferred.");
  return false;
}

// ============================================================
// CLOUD FUNCTION PING — Firebase ID token in the Authorization header
// ============================================================

// Re-arm the heartbeat for ~30 s rather than burning the full interval.
static void deferPing(const char *why) {
  Serial.printf("⏳ Ping deferred: %s\n", why);
  lastPingTime = millis() - PING_INTERVAL + 30000UL;
}

void sendPing() {
  if (WiFi.status() != WL_CONNECTED || userUID.isEmpty()) {
    deferPing("no WiFi / not signed in");
    return;
  }
  if (!clockSynced) {
    deferPing("waiting for NTP");
    return;
  }
  // Firebase.ready() refreshes the ID token shortly before it expires.
  if (!Firebase.ready()) {
    deferPing("Firebase token not ready");
    return;
  }
  const char *idToken = Firebase.getToken();
  if (idToken == nullptr || strlen(idToken) == 0) {
    deferPing("no ID token available");
    return;
  }

  Serial.println("📡 Sending ping...");

  WiFiClientSecure client;
  // Validate the server certificate against Google's roots (google_roots.h).
  client.setCACert(GOOGLE_ROOT_CAS);

  HTTPClient http;
  http.begin(client, PING_URL);
  http.setTimeout(30000);
  http.addHeader("Content-Type", "application/json");
  // The function derives the UID from this token; no UID is sent by the client.
  http.addHeader("Authorization", String("Bearer ") + idToken);

  int httpCode = http.POST("{}");
  if (httpCode == 401) {
    Serial.println("🔑 Ping unauthorized — forcing token refresh.");
    Firebase.refreshToken(&config);
  }
  if (httpCode > 0) {
    Serial.printf("✅ Ping OK! HTTP %d\n", httpCode);
  } else {
    Serial.printf("❌ Ping error: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
  lastPingTime = millis();
}

// ============================================================
// FIREBASE STREAM CALLBACK
// ============================================================

void streamCallback(FirebaseStream data) {
  Serial.println("🔔 Stream event received!");
  Serial.println("Path: " + String(data.dataPath().c_str()));

  // IMPORTANT: Do NOT perform Firebase operations (or blocking work) here.
  // Using the stream's FirebaseData object for other calls corrupts the
  // stream. Just flag the request; loop() handles it.
  if (data.dataType() == "boolean" && data.boolData() == true) {
    Serial.println("🖥️ Turn On PC command received! (queued)");
    pcWakeRequested = true;
  }
}

void streamTimeoutCallback(bool timeout) {
  if (timeout) {
    Serial.println("⚠️ Stream timeout!");
  }

  // If the stream did not auto-resume, tear it down and start a new one.
  if (!fbdo.httpConnected() && !userUID.isEmpty()) {
    Serial.println("🔁 Re-establishing command stream...");
    Firebase.RTDB.endStream(&fbdo);
    String commandPath = "/users/" + userUID + "/command/pc_on";
    if (Firebase.RTDB.beginStream(&fbdo, commandPath)) {
      Serial.println("✅ Stream re-established: " + commandPath);
    } else {
      Serial.println("❌ Stream reconnect error: " +
                     String(fbdo.errorReason().c_str()));
    }
  }
}

// ============================================================
// WAKE ON LAN — Send Magic Packet
// ============================================================

void sendWoL() {
  Serial.println("🖥️ Sending Wake on LAN magic packet...");
  Serial.print("Target MAC: ");
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X", pcMacBytes[i]);
    if (i < 5)
      Serial.print(":");
  }
  Serial.println();

  // Magic Packet: 6x 0xFF + MAC repeated 16 times = 102 bytes
  byte magicPacket[102];
  for (int i = 0; i < 6; i++)
    magicPacket[i] = 0xFF;
  for (int i = 0; i < 16; i++) {
    for (int j = 0; j < 6; j++) {
      magicPacket[6 + (i * 6) + j] = pcMacBytes[j];
    }
  }

  IPAddress broadcastIP(255, 255, 255, 255);
  udp.begin(9);

  udp.beginPacket(broadcastIP, 9);
  udp.write(magicPacket, 102);
  udp.endPacket();

  udp.beginPacket(broadcastIP, 7);
  udp.write(magicPacket, 102);
  udp.endPacket();

  udp.stop();
  Serial.println("✅ Magic packet sent!");
}
