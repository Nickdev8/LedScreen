#include "Config.h"
#include "Leds.h"
#include "Sd.h"
#include "UsbStream.h"

void setup() {
  Serial.begin(kLiveSerialBaud);
  delay(1000);

  initLeds();
  initSdAndAnimation();
}

void loop() {
  // Live USB traffic owns the LEDs until it goes idle.
  serviceUsbStream();
  if (usbStreamActive()) {
    gIdleBlinkActive = false;
    return;
  }

  if (!gSdReady || !gAnimReady) {
    if (millis() >= gNextSdRetryMs) {
      gNextSdRetryMs = millis() + kSdRetryIntervalMs;
      initSdAndAnimation();
    }
    if (!gAnimReady) {
      if (!gIdleBlinkActive) resetIdleBlink();
      updateIdleBlink();
    }
    return;
  }

  gIdleBlinkActive = false;
  updateAnimationPlayback();
}
