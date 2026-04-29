#include "Config.h"
#include "Leds.h"
#include "Sd.h"
#include "Gif.h"
#include "UsbStream.h"

static void tryInitPlayback() {
  initSdAndAnimation();
  if (!gAnimReady && gSdReady) {
    uint32_t gifCluster, gifSize;
    if (fatFindFile("GIF", gifCluster, gifSize))
      initGifPlayback(gifCluster, gifSize);
  }
}

void setup() {
  Serial.begin(kLiveSerialBaud);
  delay(1000);

  initLeds();
  tryInitPlayback();
}

void loop() {
  // Live USB traffic owns the LEDs until it goes idle.
  serviceUsbStream();
  if (usbStreamActive()) {
    gIdleBlinkActive = false;
    return;
  }

  const bool playing = gAnimReady || gGifReady;

  if (!gSdReady || !playing) {
    if (millis() >= gNextSdRetryMs) {
      gNextSdRetryMs = millis() + kSdRetryIntervalMs;
      tryInitPlayback();
    }
    if (!playing) {
      if (!gIdleBlinkActive) resetIdleBlink();
      updateIdleBlink();
    }
    return;
  }

  gIdleBlinkActive = false;
  if (gAnimReady) updateAnimationPlayback();
  else            updateGifPlayback();
}
