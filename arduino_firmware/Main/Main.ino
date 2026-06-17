#include "Config.h"
#include "Leds.h"
#include "Sd.h"

static void tryInitPlayback() {
  initSdPlaylist();
}

void setup() {
  Serial.begin(kLiveSerialBaud);
  delay(1000);

  initLeds();
  tryInitPlayback();
}

void loop() {
  if (!gSdReady) {
    if (millis() >= gNextSdRetryMs) {
      gNextSdRetryMs = millis() + kSdRetryIntervalMs;
      tryInitPlayback();
    }
  }

  if (gAnimReady) {
    gIdleBlinkActive = false;
    updateAnimationPlayback();
    return;
  }

  if (!gIdleBlinkActive) resetIdleBlink();
  updateIdleBlink();
}
