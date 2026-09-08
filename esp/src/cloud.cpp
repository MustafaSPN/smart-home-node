// ============================================================
// HOME NODE — ALL-IN-ONE CLOUD CONTROLLER (ESP32-S3)
// ============================================================
// One device does everything, via Firebase RTDB:
//   • Klima:   streams users/{uid}/climate/state  → ELECTRA_AC IR (KY-005)
//   • PC aç:   streams users/{uid}/command/pc_on   → Wake-on-LAN magic packet
//   • Ping:    POSTs to the espPing Cloud Function every 5 min (power outage),
//              authenticated with the Firebase ID token of the signed-in session
//   • Sensör:  DHT11 room temp/humidity → users/{uid}/status
//
// Build & run:
//   pio run -e cloud -t upload && pio device monitor
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_sleep.h>
#include <time.h>
#include <DHT.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRac.h>
#include <Firebase_ESP_Client.h>
#include <addons/RTDBHelper.h>
#include <addons/TokenHelper.h>
#include "config.h"
#include "google_roots.h"

#define AC_PROTOCOL decode_type_t::ELECTRA_AC
#define AC_MODEL    1
#define LED_PIN     2

// ============================================================
// GLOBALS
// ============================================================
IRac ac(IR_SEND_PIN);
DHT  dht(DHT_PIN, DHT11);
WiFiUDP udp;

FirebaseData fbdoStreamAc; // stream: climate/state
FirebaseData fbdoStreamPc; // stream: command/pc_on
FirebaseData fbdoRead;     // reads (state object, MAC)
FirebaseData fbdoWrite;    // writes (status, flag reset)
FirebaseAuth auth;
FirebaseConfig config;

String userUID = "";
bool   firebaseReady = false;

// AC
volatile bool acDirty = false;
bool   firstLoad = true;
bool   acPower = false;
int    acTemp  = 24;
String acMode  = "cool";
String acFan   = "auto";

// PC / WoL
volatile bool wakeRequested = false;
byte   pcMac[6] = {0, 0, 0, 0, 0, 0};
bool   macLoaded = false;

unsigned long lastDht = 0;
unsigned long lastPing = 0;
unsigned long lastWifiCheck = 0;
unsigned long lastClockRetry = 0;

// TLS certificate validation needs a correct wall clock, so the first ping
// waits until NTP has actually set the system time.
bool clockSynced = false;

// Survives soft-reset / deep-sleep (cleared only on cold power-on).
// Counts consecutive auth failures so we can back off and avoid rate-limits.
RTC_DATA_ATTR int authFails = 0;

// ============================================================
// FORWARD DECLARATIONS
// ============================================================
void wifiConnect();
void streamAcCallback(FirebaseStream data);
void streamPcCallback(FirebaseStream data);
void streamTimeout(bool timeout);
void applyStateFromFirebase();
void sendIR();
void handleWake();
void loadMac();
void sendWoL();
void pushRoom();
void sendPing();
bool syncClock(unsigned long timeoutMs);

stdAc::opmode_t mapMode(const String &m) {
  if (m == "heat") return stdAc::opmode_t::kHeat;
  if (m == "dry")  return stdAc::opmode_t::kDry;
  if (m == "fan")  return stdAc::opmode_t::kFan;
  if (m == "auto") return stdAc::opmode_t::kAuto;
  return stdAc::opmode_t::kCool;
}
stdAc::fanspeed_t mapFan(const String &f) {
  if (f == "low")  return stdAc::fanspeed_t::kLow;
  if (f == "med")  return stdAc::fanspeed_t::kMedium;
  if (f == "high") return stdAc::fanspeed_t::kHigh;
  return stdAc::fanspeed_t::kAuto;
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);
  delay(300);
  Serial.println("\n🏠 Home Node ALL-IN-ONE starting...");

  pinMode(LED_PIN, OUTPUT);
  dht.begin();

  wifiConnect();

  configTime(0, 0, "pool.ntp.org", "time.google.com");
  clockSynced = syncClock(20000);

  config.api_key = FIREBASE_API_KEY;
  config.database_url = FIREBASE_HOST;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  config.token_status_callback = tokenStatusCallback;

  Firebase.begin(&config, &auth);
  Firebase.reconnectNetwork(true);

  Serial.println("🔑 Firebase auth...");
  t0 = millis();
  while (auth.token.uid.empty() && millis() - t0 < 30000) {
    Firebase.ready();
    delay(300);
  }
  if (auth.token.uid.empty()) {
    authFails++;
    Serial.printf("❌ Auth failed (deneme %d). Check USER_EMAIL/USER_PASSWORD.\n",
                  authFails);
    // Back off so we don't hammer Google's auth endpoint (rate-limit guard).
    if (authFails >= 2) {
      authFails = 0; // start fresh after the nap
      Serial.println("😴 Rate-limit koruması: 15 dk derin uykuya geçiliyor...");
      Serial.flush();
      esp_sleep_enable_timer_wakeup((uint64_t)15 * 60 * 1000000ULL);
      esp_deep_sleep_start(); // wakes into a fresh setup()
    }
    Serial.println("↻ 10 sn sonra yeniden denenecek...");
    delay(10000);
    ESP.restart();
  }
  authFails = 0; // success — clear the counter
  userUID = String(auth.token.uid.c_str());
  Serial.println("✅ Auth OK — UID: " + userUID);

  loadMac();

  String acPath = "/users/" + userUID + "/climate/state";
  if (Firebase.RTDB.beginStream(&fbdoStreamAc, acPath))
    Firebase.RTDB.setStreamCallback(&fbdoStreamAc, streamAcCallback, streamTimeout);
  else
    Serial.println("❌ AC stream: " + String(fbdoStreamAc.errorReason().c_str()));

  String pcPath = "/users/" + userUID + "/command/pc_on";
  if (Firebase.RTDB.beginStream(&fbdoStreamPc, pcPath))
    Firebase.RTDB.setStreamCallback(&fbdoStreamPc, streamPcCallback, streamTimeout);
  else
    Serial.println("❌ PC stream: " + String(fbdoStreamPc.errorReason().c_str()));

  firebaseReady = true;
  pushRoom();
  sendPing();
  Serial.println("✅ Ready (klima + PC + ping).");
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  // WiFi watchdog — reconnect if the link drops
  if (millis() - lastWifiCheck > 30000) {
    lastWifiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("⚠️ WiFi lost — reconnecting...");
      wifiConnect();
    }
  }

  // Drives the Firebase client's token lifecycle: it refreshes the ID token
  // shortly before expiry, so getToken() in sendPing() is always current.
  Firebase.ready();

  // If NTP had not answered by the time setup() finished, keep trying — the
  // heartbeat cannot validate the server certificate without a real clock.
  if (!clockSynced && millis() - lastClockRetry > 60000) {
    lastClockRetry = millis();
    clockSynced = syncClock(5000);
  }

  if (acDirty) {
    acDirty = false;
    applyStateFromFirebase();
  }
  if (wakeRequested) {
    wakeRequested = false;
    handleWake();
  }
  if (firebaseReady && millis() - lastDht > 60000) pushRoom();
  if (firebaseReady && millis() - lastPing >= PING_INTERVAL_MS) sendPing();

  digitalWrite(LED_PIN, (WiFi.status() == WL_CONNECTED && firebaseReady)
                            ? HIGH
                            : (millis() / 500) % 2);
  delay(50);
}

// ============================================================
// WIFI
// ============================================================
void wifiConnect() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("📶 WiFi connecting");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.setSleep(false); // avoid modem-sleep latency spikes during IR TX
    Serial.println("\n✅ WiFi OK — IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\n❌ WiFi failed! Restarting...");
    delay(3000);
    ESP.restart();
  }
}

// ============================================================
// STREAM CALLBACKS  (flag only — never do Firebase I/O here)
// ============================================================
void streamAcCallback(FirebaseStream data) { acDirty = true; }

void streamPcCallback(FirebaseStream data) {
  if (data.dataType() == "boolean" && data.boolData() == true)
    wakeRequested = true;
}
void streamTimeout(bool timeout) {
  if (timeout) Serial.println("⚠️ Stream timeout, reconnecting...");
}

// ============================================================
// KLIMA
// ============================================================
void applyStateFromFirebase() {
  String path = "/users/" + userUID + "/climate/state";
  if (!Firebase.RTDB.getJSON(&fbdoRead, path)) {
    Serial.println("❌ state read: " + String(fbdoRead.errorReason().c_str()));
    return;
  }
  FirebaseJson &json = fbdoRead.to<FirebaseJson>();
  FirebaseJsonData d;
  if (json.get(d, "power")) acPower = d.to<bool>();
  if (json.get(d, "temp"))  acTemp  = constrain(d.to<int>(), 16, 30);
  if (json.get(d, "mode"))  acMode  = d.to<String>();
  if (json.get(d, "fan"))   acFan   = d.to<String>();
  Serial.printf("📥 klima: power=%d mode=%s temp=%d fan=%s\n", acPower,
                acMode.c_str(), acTemp, acFan.c_str());

  if (firstLoad) { // boot sync, don't blast IR
    firstLoad = false;
    Serial.println("(boot sync — IR gönderilmedi)");
    return;
  }
  sendIR();
}

void sendIR() {
  ac.next.protocol = AC_PROTOCOL;
  ac.next.model    = AC_MODEL;
  ac.next.power    = acPower;
  ac.next.mode     = mapMode(acMode);
  ac.next.degrees  = acTemp;
  ac.next.fanspeed = mapFan(acFan);
  ac.next.swingv   = stdAc::swingv_t::kOff;
  ac.next.swingh   = stdAc::swingh_t::kOff;
  ac.next.light    = true;
  ac.next.beep     = true;
  ac.next.econo    = false;
  ac.next.turbo    = false;
  ac.next.quiet    = false;
  ac.next.filter   = false;
  ac.next.clean    = false;
  ac.next.celsius  = true;
  ac.next.sleep    = -1;
  ac.next.clock    = -1;

  // IR envelope timing is microsecond-sensitive. WiFi/Firebase background
  // tasks can preempt the bit-bang and corrupt the packet (demodulator still
  // blinks, but the AC rejects it). Raise our priority during TX and send a
  // few times so at least one clean packet lands.
  UBaseType_t prio = uxTaskPriorityGet(NULL);
  vTaskPrioritySet(NULL, configMAX_PRIORITIES - 1);
  bool ok = ac.sendAc();
  vTaskPrioritySet(NULL, prio);
  Serial.printf("📤 IR sent (ok=%d)\n", ok);
}

// ============================================================
// PC — WAKE ON LAN
// ============================================================
void handleWake() {
  loadMac(); // refresh in case the user changed it in the app
  if (macLoaded) sendWoL();
  else Serial.println("❌ WoL: MAC not set (app Profile page).");

  // Reset the flag with a DEDICATED object (never the stream one)
  if (Firebase.ready()) {
    String p = "/users/" + userUID + "/command/pc_on";
    if (Firebase.RTDB.setBool(&fbdoWrite, p, false))
      Serial.println("✅ pc_on reset");
    else
      Serial.println("❌ pc_on reset: " + String(fbdoWrite.errorReason().c_str()));
  }
}

void loadMac() {
  String path = "/users/" + userUID + "/profile/mac_address";
  if (Firebase.RTDB.getString(&fbdoRead, path)) {
    String mac = String(fbdoRead.stringData().c_str());
    mac.trim();
    if (mac.length() == 17) {
      for (int i = 0; i < 6; i++)
        pcMac[i] = (byte)strtol(mac.substring(i * 3, i * 3 + 2).c_str(), NULL, 16);
      macLoaded = true;
      Serial.println("✅ MAC: " + mac);
    } else {
      Serial.println("⚠️ Invalid MAC: " + mac);
    }
  } else {
    Serial.println("⚠️ MAC not set yet.");
  }
}

void sendWoL() {
  Serial.println("🖥️ Sending Wake-on-LAN...");
  byte packet[102];
  for (int i = 0; i < 6; i++) packet[i] = 0xFF;
  for (int i = 0; i < 16; i++)
    for (int j = 0; j < 6; j++) packet[6 + i * 6 + j] = pcMac[j];

  IPAddress broadcast(255, 255, 255, 255);
  udp.begin(9);
  udp.beginPacket(broadcast, 9); udp.write(packet, 102); udp.endPacket();
  udp.beginPacket(broadcast, 7); udp.write(packet, 102); udp.endPacket();
  udp.stop();
  Serial.println("✅ Magic packet sent!");
}

// ============================================================
// SENSOR + PING
// ============================================================
void pushRoom() {
  lastDht = millis();
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  String base = "/users/" + userUID + "/status/";
  if (!isnan(t)) Firebase.RTDB.setFloat(&fbdoWrite, base + "room_temp", t);
  if (!isnan(h)) Firebase.RTDB.setFloat(&fbdoWrite, base + "room_hum", h);
  time_t now = time(nullptr);
  if (now > 100000)
    Firebase.RTDB.setDouble(&fbdoWrite, base + "ac_last_ping", (double)now * 1000.0);
  Serial.printf("🌡️  room: %.1f°C  %.0f%%\n", t, h);
}

// Waits for NTP to set the system clock. Returns true once the time looks
// real (anything past 2021 means we are no longer at the 1970 epoch).
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

// Re-arm the heartbeat for ~30 s instead of burning the whole 5-minute
// interval, so a transient block (no clock, no token yet) is retried soon.
static void deferPing(const char *why) {
  Serial.printf("⏳ Ping deferred: %s\n", why);
  lastPing = millis() - PING_INTERVAL_MS + 30000UL;
}

void sendPing() {
  lastPing = millis();
  if (WiFi.status() != WL_CONNECTED || userUID.isEmpty()) {
    deferPing("no WiFi / not signed in");
    return;
  }

  // No clock means no way to check certificate validity dates.
  if (!clockSynced) {
    deferPing("waiting for NTP");
    return;
  }

  // Firebase.ready() refreshes the ID token when it is close to expiry, so a
  // ready session always yields a currently-valid token.
  if (!Firebase.ready()) {
    deferPing("Firebase token not ready");
    return;
  }
  const char *idToken = Firebase.getToken();
  if (idToken == nullptr || strlen(idToken) == 0) {
    deferPing("no ID token available");
    return;
  }

  Serial.println("📡 Ping...");
  WiFiClientSecure client;
  // Validate the server certificate against Google's roots (see
  // google_roots.h). This enables MBEDTLS_SSL_VERIFY_REQUIRED and hostname
  // checking — setInsecure() is deliberately not used.
  client.setCACert(GOOGLE_ROOT_CAS);

  HTTPClient http;
  http.begin(client, PING_URL);
  http.setTimeout(30000);
  http.addHeader("Content-Type", "application/json");
  // The function derives the UID from this token; no UID is sent by the client.
  http.addHeader("Authorization", String("Bearer ") + idToken);

  int code = http.POST("{}");
  if (code == 401) {
    // Token rejected — drop it so the client re-authenticates on the next pass.
    Serial.println("🔑 Ping unauthorized — forcing token refresh.");
    Firebase.refreshToken(&config);
  }
  Serial.printf(code > 0 ? "✅ Ping HTTP %d\n" : "❌ Ping err %d\n", code);
  http.end();
}
