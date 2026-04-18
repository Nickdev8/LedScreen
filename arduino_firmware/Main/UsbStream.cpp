// USB serial streaming — receives raw RGB frames from xLights over USB CDC.
// Protocol: host sends kLiveFrameBytes (7680) bytes per frame, back-to-back.
// No framing or header — just raw channel data at kLiveSerialBaud.
// Priority: when active, SD animation is suppressed (checked via usbStreamActive()).

#include <Arduino.h>
#include "Config.h"
#include "UsbStream.h"
#include "Leds.h"

static size_t   gFrameIndex     = 0;
static uint32_t gLastActivityMs = 0;

constexpr uint32_t kUsbActiveTimeoutMs = 500;

void serviceUsbStream() {
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
  // Reset partial frame if no data arrives within the frame timeout
  if (gFrameIndex > 0 && (millis() - gLastActivityMs) > kLiveFrameTimeoutMs)
    gFrameIndex = 0;
}

bool usbStreamActive() {
  return (millis() - gLastActivityMs) <= kUsbActiveTimeoutMs;
}
