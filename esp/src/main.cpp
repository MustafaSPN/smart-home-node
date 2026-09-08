// ============================================================
// AIR CONDITIONER — LAN WEB CONTROLLER
// ============================================================
// Serves a web UI (temperature, mode, fan speed, power off) and
// transmits the corresponding IR command via the KY-005 transmitter.
// Also reports room temperature/humidity from the DHT11.
//
// Build & run:
//   pio run -e control -t upload && pio device monitor
//   → open http://<ip-in-serial>/   or   http://homenode.local/
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <DHT.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRac.h>
#include "config.h"

// ============================================================
// AC PROTOCOL  ← SET THIS after running the `decode` environment
// ============================================================
// Detected from the AC remote via the `decode` environment.
#define AC_PROTOCOL decode_type_t::ELECTRA_AC
#define AC_MODEL    1   // Some protocols need a model number; 1 is a safe default

// ============================================================
// GLOBALS
// ============================================================
IRac ac(IR_SEND_PIN);
DHT dht(DHT_PIN, DHT11);
WebServer server(80);

// Desired AC state (what the UI shows / what we transmit)
bool   acPower = false;
int    acTemp  = 24;      // 16..30 °C
String acMode  = "cool";  // cool | heat | dry | fan | auto
String acFan   = "auto";  // auto | low | med | high

// Latest DHT reading
float roomTemp = NAN;
float roomHum  = NAN;
unsigned long lastDht = 0;

// ============================================================
// ENUM MAPPING
// ============================================================
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
// TRANSMIT CURRENT STATE VIA IR
// ============================================================
void sendState() {
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

  bool ok = ac.sendAc();
  Serial.printf("📤 IR sent: power=%d mode=%s temp=%d fan=%s (ok=%d)\n",
                acPower, acMode.c_str(), acTemp, acFan.c_str(), ok);
}

// ============================================================
// WEB HANDLERS
// ============================================================
String stateJson() {
  String s = "{";
  s += "\"power\":" + String(acPower ? "true" : "false");
  s += ",\"temp\":" + String(acTemp);
  s += ",\"mode\":\"" + acMode + "\"";
  s += ",\"fan\":\"" + acFan + "\"";
  s += ",\"roomTemp\":" + (isnan(roomTemp) ? String("null") : String(roomTemp, 1));
  s += ",\"roomHum\":"  + (isnan(roomHum)  ? String("null") : String(roomHum, 0));
  s += "}";
  return s;
}

void handleState() {
  server.send(200, "application/json", stateJson());
}

void handleCmd() {
  if (server.hasArg("power")) acPower = (server.arg("power") == "on");
  if (server.hasArg("temp"))  acTemp  = constrain(server.arg("temp").toInt(), 16, 30);
  if (server.hasArg("mode"))  { acMode = server.arg("mode"); acPower = true; }
  if (server.hasArg("fan"))   acFan   = server.arg("fan");

  sendState();
  handleState();
}

const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="tr">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1">
<title>Home Node Climate</title>
<style>
  :root { color-scheme: dark; }
  * { box-sizing: border-box; -webkit-tap-highlight-color: transparent; }
  body { margin:0; font-family:-apple-system,system-ui,sans-serif;
         background:#0b0f17; color:#e8ecf3; display:flex; justify-content:center; }
  .app { width:100%; max-width:420px; padding:24px 18px 40px; }
  h1 { font-size:20px; font-weight:700; margin:8px 0 2px; }
  .sub { color:#8a93a6; font-size:13px; margin-bottom:20px; }
  .card { background:#141a26; border:1px solid #223; border-radius:20px;
          padding:20px; margin-bottom:16px; }
  .room { display:flex; gap:18px; }
  .room div { flex:1; text-align:center; }
  .room .big { font-size:26px; font-weight:700; }
  .room .lbl { font-size:11px; color:#8a93a6; text-transform:uppercase; letter-spacing:.5px; }
  .temp { text-align:center; }
  .temp .val { font-size:64px; font-weight:800; line-height:1; }
  .temp .val span { font-size:24px; color:#8a93a6; }
  .stepper { display:flex; gap:14px; justify-content:center; margin-top:14px; }
  .stepper button { width:64px; height:64px; border-radius:50%; font-size:30px; }
  .grid { display:grid; grid-template-columns:repeat(4,1fr); gap:8px; }
  .lbl-row { font-size:12px; color:#8a93a6; margin:0 0 8px; text-transform:uppercase; letter-spacing:.5px; }
  button { border:none; background:#1e2636; color:#e8ecf3; border-radius:14px;
           padding:12px 0; font-size:15px; font-weight:600; cursor:pointer;
           transition:.15s; }
  button:active { transform:scale(.94); }
  button.on { background:#2f6df6; color:#fff; }
  .off { width:100%; background:#3a1620; color:#ff6b81; padding:16px;
         font-size:16px; margin-top:4px; }
  .off.on { background:#ff3b5c; color:#fff; }
  .pill { display:inline-block; width:9px; height:9px; border-radius:50%;
          background:#ff5252; margin-right:6px; vertical-align:middle; }
  .pill.act { background:#39d98a; }
</style>
</head>
<body>
<div class="app">
  <h1><span id="pill" class="pill"></span>Climate</h1>
  <div class="sub" id="status">Bağlanıyor…</div>

  <div class="card room">
    <div><div class="big" id="rT">–</div><div class="lbl">Oda °C</div></div>
    <div><div class="big" id="rH">–</div><div class="lbl">Nem %</div></div>
  </div>

  <div class="card temp">
    <div class="val"><span id="setT">24</span><span>°C</span></div>
    <div class="stepper">
      <button onclick="step(-1)">−</button>
      <button onclick="step(1)">+</button>
    </div>
  </div>

  <div class="card">
    <p class="lbl-row">Mod</p>
    <div class="grid" id="modes">
      <button data-mode="cool">Soğutma</button>
      <button data-mode="heat">Isıtma</button>
      <button data-mode="dry">Nem Al</button>
      <button data-mode="fan">Fan</button>
    </div>
    <p class="lbl-row" style="margin-top:16px">Fan Hızı</p>
    <div class="grid" id="fans">
      <button data-fan="auto">Oto</button>
      <button data-fan="low">Düşük</button>
      <button data-fan="med">Orta</button>
      <button data-fan="high">Yüksek</button>
    </div>
  </div>

  <button class="off" id="powerBtn" onclick="togglePower()">⏻ Kapat</button>
</div>

<script>
let st = { power:false, temp:24, mode:'cool', fan:'auto' };

function render() {
  document.getElementById('setT').textContent = st.temp;
  document.getElementById('pill').className = 'pill' + (st.power ? ' act' : '');
  document.getElementById('status').textContent =
    st.power ? (st.mode + ' • ' + st.fan + ' • ' + st.temp + '°C') : 'Kapalı';
  document.querySelectorAll('#modes button').forEach(b =>
    b.classList.toggle('on', st.power && b.dataset.mode === st.mode));
  document.querySelectorAll('#fans button').forEach(b =>
    b.classList.toggle('on', b.dataset.fan === st.fan));
  const pb = document.getElementById('powerBtn');
  pb.classList.toggle('on', st.power);
  pb.textContent = st.power ? '⏻ Kapat' : '⏻ Kapalı';
}

function apply(data) {
  document.getElementById('rT').textContent = data.roomTemp ?? '–';
  document.getElementById('rH').textContent = data.roomHum ?? '–';
  st = { power:data.power, temp:data.temp, mode:data.mode, fan:data.fan };
  render();
}

function cmd(params) {
  fetch('/cmd?' + params).then(r => r.json()).then(apply).catch(()=>{});
}
function step(d)     { cmd('temp=' + (st.temp + d)); }
function togglePower(){ cmd('power=' + (st.power ? 'off' : 'on')); }

document.querySelectorAll('#modes button').forEach(b =>
  b.onclick = () => cmd('mode=' + b.dataset.mode));
document.querySelectorAll('#fans button').forEach(b =>
  b.onclick = () => cmd('fan=' + b.dataset.fan));

function poll(){ fetch('/state').then(r=>r.json()).then(apply).catch(()=>{}); }
poll(); setInterval(poll, 5000);
</script>
</body>
</html>
)HTML";

void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

// ============================================================
// SETUP / LOOP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n❄️  AC Controller starting...");

  dht.begin();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("📶 WiFi connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.print(".");
  }
  Serial.println("\n✅ WiFi OK — IP: " + WiFi.localIP().toString());

  if (MDNS.begin("homenode")) {
    Serial.println("🌐 http://homenode.local/");
  }

  server.on("/", handleRoot);
  server.on("/state", handleState);
  server.on("/cmd", handleCmd);
  server.begin();
  Serial.println("✅ Web server ready.");
}

void loop() {
  server.handleClient();

  // Refresh DHT every 3 s (DHT11 is slow; don't poll faster)
  if (millis() - lastDht > 3000) {
    lastDht = millis();
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t)) roomTemp = t;
    if (!isnan(h)) roomHum = h;
  }
}
