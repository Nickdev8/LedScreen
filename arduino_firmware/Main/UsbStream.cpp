#include <Arduino.h>
#include "Config.h"
#include "UsbStream.h"
#include "Leds.h"

static size_t   gFrameIndex     = 0;
static uint32_t gLastActivityMs = 0;

constexpr uint32_t kUsbActiveTimeoutMs = 500;

void serviceUsbStream() {
  // The stream is raw RGB bytes with no delimiter, so a stalled partial frame must be dropped.
  while (Serial.available()) {
    const int b = Serial.read();
    if (b < 0) break;
    gLastActivityMs = millis();
    if (gFrameIndex < kLiveFrameBytes)
      gFrameBuffer[gFrameIndex++] = static_cast<uint8_t>(b);
    if (gFrameIndex >= kLiveFrameBytes) {
      applyCurrentLimit();
      renderFrameBuffer();
      gFrameIndex = 0;
    }
  }
  if (gFrameIndex > 0 && (millis() - gLastActivityMs) > kLiveFrameTimeoutMs)
    gFrameIndex = 0;
}

bool usbStreamActive() {
  return (millis() - gLastActivityMs) <= kUsbActiveTimeoutMs;
}
