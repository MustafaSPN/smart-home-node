// ============================================================
// AIR CONDITIONER — IR PROTOCOL DECODER
// ============================================================
// Point the AC remote at the KY-022 receiver and press a button.
// The serial monitor prints the detected protocol + decoded AC state.
//
// Build & run:
//   pio run -e decode -t upload && pio device monitor
//
// What to look for in the output:
//   "Protocol  : COOLIX"  (or GREE / MIDEA / TCL112AC ...)  ← note this name
//   "AC State  : ..."     ← confirms it's a recognised AC protocol
//
// Copy the "Protocol" name into main.cpp -> AC_PROTOCOL, then flash `control`.
// ============================================================

#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRac.h>
#include <IRutils.h>
#include "config.h"

const uint16_t kCaptureBufferSize = 1024; // AC packets are long
const uint8_t kTimeout = 50;              // ms of no signal = end of message
const uint16_t kMinUnknownSize = 12;

IRrecv irrecv(IR_RECV_PIN, kCaptureBufferSize, kTimeout, true);
decode_results results;

void setup() {
  Serial.begin(115200);
  // ESP32-S3 native USB: wait until the host reconnects, else early prints
  // are lost after a reset.
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 4000) {
    delay(10);
  }
  delay(400);
  Serial.println("\n📡 IR Decoder ready.");
  Serial.println("Point the remote at the KY-022 and press a button...\n");
  irrecv.setUnknownThreshold(kMinUnknownSize);
  irrecv.enableIRIn();
}

void loop() {
  // Raw pin watcher — bypasses the IR library. The receiver output idles HIGH
  // and pulses LOW on 38kHz IR. We count what fraction of the time it's LOW:
  //   ~0%      = idle, good (spikes only when a remote button is pressed)
  //   near 100% = output stuck LOW = miswired/defective receiver
  static unsigned long lowCount = 0, sampleCount = 0;
  sampleCount++;
  if (digitalRead(IR_RECV_PIN) == LOW) {
    lowCount++;
  }

  // Heartbeat — proves the board + serial are alive, and reports raw activity
  static unsigned long lastBeat = 0;
  if (millis() - lastBeat > 3000) {
    lastBeat = millis();
    int pct = sampleCount ? (int)(lowCount * 100 / sampleCount) : 0;
    Serial.printf("… dinliyorum · son 3sn LOW oranı: %%%d\n", pct);
    lowCount = 0;
    sampleCount = 0;
  }

  if (irrecv.decode(&results)) {
    // Human-readable summary (protocol, bits, value)
    Serial.println(resultToHumanReadableBasic(&results));

    // If it's a known AC protocol, decode the full climate state
    String acState = IRAcUtils::resultAcToString(&results);
    if (acState.length()) {
      Serial.println("AC State  : " + acState);
    } else {
      Serial.println("(Not decoded as a known AC protocol — raw dump below)");
    }

    // Raw timing array — useful as a fallback if the protocol is UNKNOWN
    Serial.println(resultToSourceCode(&results));
    Serial.println("--------------------------------------------------\n");
    yield();
  }
}
