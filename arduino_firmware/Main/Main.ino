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
  // USB has highest priority — renders immediately when data arrives
  serviceUsbStream();
  if (usbStreamActive()) {
    gIdleBlinkActive = false;
    return;
  }

  // No USB data — run SD animation (or idle blink if SD unavailable)
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
