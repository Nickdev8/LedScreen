#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include "Config.h"

struct PhysicalPixel {
  uint8_t lane;
  uint16_t index;
};

constexpr uint16_t kLane0LedCount = static_cast<uint16_t>(kLivePanelsPerLane[0]) * kLiveLedsPerPanel;
constexpr uint16_t kLane1LedCount = static_cast<uint16_t>(kLivePanelsPerLane[1]) * kLiveLedsPerPanel;
constexpr uint16_t kLane2LedCount = static_cast<uint16_t>(kLivePanelsPerLane[2]) * kLiveLedsPerPanel;
constexpr uint16_t kLane3LedCount = static_cast<uint16_t>(kLivePanelsPerLane[3]) * kLiveLedsPerPanel;
constexpr uint16_t kLane4LedCount = static_cast<uint16_t>(kLivePanelsPerLane[4]) * kLiveLedsPerPanel;
constexpr uint16_t kLane5LedCount = static_cast<uint16_t>(kLivePanelsPerLane[5]) * kLiveLedsPerPanel;
constexpr uint16_t kLane6LedCount = static_cast<uint16_t>(kLivePanelsPerLane[6]) * kLiveLedsPerPanel;
constexpr uint16_t kLane7LedCount = static_cast<uint16_t>(kLivePanelsPerLane[7]) * kLiveLedsPerPanel;

Adafruit_NeoPixel strip0(kLane0LedCount, kLiveLanePins[0], NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel strip1(kLane1LedCount, kLiveLanePins[1], NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel strip2(kLane2LedCount, kLiveLanePins[2], NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel strip3(kLane3LedCount, kLiveLanePins[3], NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel strip4(kLane4LedCount, kLiveLanePins[4], NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel strip5(kLane5LedCount, kLiveLanePins[5], NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel strip6(kLane6LedCount, kLiveLanePins[6], NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel strip7(kLane7LedCount, kLiveLanePins[7], NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel* gStrips[kLiveLaneCount] = {
  &strip0,
  &strip1,
  &strip2,
  &strip3,
  &strip4,
  &strip5,
  &strip6,
  &strip7,
};

uint8_t  gFrameBuffer[kLiveFrameBytes];
size_t   gFrameIndex = 0;
uint32_t gLastSerialActivityMs = 0;

constexpr uint32_t kSerialActiveTimeoutMs = 500;
constexpr uint32_t kBootTestHoldMs = 600;


PhysicalPixel mapLogicalToPhysical(uint16_t logicalIndex) {
  uint16_t x = 0;
  uint16_t y = 0;

  if (kLiveInputColumnMajor) {
    x = logicalIndex / kLiveMatrixHeight;
    y = logicalIndex % kLiveMatrixHeight;
  } else {
    x = logicalIndex % kLiveMatrixWidth;
    y = logicalIndex / kLiveMatrixWidth;
  }

  if (kMatrixFlipX) x = kLiveMatrixWidth  - 1U - x;
  if (kMatrixFlipY) y = kLiveMatrixHeight - 1U - y;

  const uint16_t panelsWide = kLiveMatrixWidth / kLivePanelWidth;
  const uint16_t panelCol   = x / kLivePanelWidth;
  const uint16_t panelRow   = y / kLivePanelHeight;
  const uint16_t panelIndex = panelRow * panelsWide + panelCol;

  uint8_t panelInLane = static_cast<uint8_t>(panelIndex);
  uint8_t lane = 0;
  while (lane < kLiveLaneCount && panelInLane >= kLivePanelsPerLane[lane]) {
    panelInLane -= kLivePanelsPerLane[lane];
    lane++;
  }
  if (lane >= kLiveLaneCount) return {0, 0};

  uint16_t panelX = x % kLivePanelWidth;
  uint16_t panelY = y % kLivePanelHeight;

  if (kPanelRotationQuarterTurnsCCW == 1) {
    const uint16_t oldX = panelX;
    panelX = panelY;
    panelY = kLivePanelWidth - 1U - oldX;
  } else if (kPanelRotationQuarterTurnsCCW == 2) {
    panelX = kLivePanelWidth  - 1U - panelX;
    panelY = kLivePanelHeight - 1U - panelY;
  } else if (kPanelRotationQuarterTurnsCCW == 3) {
    const uint16_t oldY = panelY;
    panelY = panelX;
    panelX = kLivePanelHeight - 1U - oldY;
  }

  if (kMatrixSerpentine) {
    const bool oddRow     = (panelY & 1U) != 0;
    const bool reverseRow = kMatrixReverseOddRows ? oddRow : !oddRow;
    if (reverseRow) panelX = kLivePanelWidth - 1U - panelX;
  }

  const uint16_t pixelInPanel = panelY * kLivePanelWidth + panelX;
  const uint16_t laneIndex    = static_cast<uint16_t>(panelInLane) * kLiveLedsPerPanel + pixelInPanel;
  return {lane, laneIndex};
}


void renderFrameBuffer() {
  for (uint16_t logicalIndex = 0; logicalIndex < kLiveLedCount; logicalIndex++) {
    const size_t bufferIndex = static_cast<size_t>(logicalIndex) * 3U;
    for (uint8_t lane = 0; lane < kLiveLaneCount; lane++) {
      gStrips[lane]->setPixelColor(
        logicalIndex,
        gFrameBuffer[bufferIndex],
        gFrameBuffer[bufferIndex + 1],
        gFrameBuffer[bufferIndex + 2]
      );
    }
  }
  for (uint8_t lane = 0; lane < kLiveLaneCount; lane++) {
    gStrips[lane]->show();
  }
}


void serviceSerialStream() {
  while (Serial.available()) {
    const int incoming = Serial.read();
    if (incoming < 0) break;
    gLastSerialActivityMs = millis();
    if (gFrameIndex < kLiveFrameBytes)
      gFrameBuffer[gFrameIndex++] = static_cast<uint8_t>(incoming);
    if (gFrameIndex >= kLiveFrameBytes) {
      renderFrameBuffer();
      gFrameIndex = 0;
    }
  }
  if (gFrameIndex > 0 && (millis() - gLastSerialActivityMs) > kLiveFrameTimeoutMs)
    gFrameIndex = 0;
}

bool serialPlaybackActive() {
  return (millis() - gLastSerialActivityMs) <= kSerialActiveTimeoutMs;
}


static uint16_t gScannerPixel     = 0;
static uint16_t gScannerPrevPixel = 0xFFFFU;
static uint32_t gNextScanMs       = 0;
static bool     gScannerActive    = false;

constexpr uint32_t kScanStepMs = 10;

static void showSolidColor(uint8_t red, uint8_t green, uint8_t blue) {
  for (uint8_t lane = 0; lane < kLiveLaneCount; lane++) {
    for (uint16_t pixel = 0; pixel < kLiveLedCount; pixel++) {
      gStrips[lane]->setPixelColor(pixel, red, green, blue);
    }
    gStrips[lane]->show();
  }
}

static void runBootTest() {
  showSolidColor(40, 0, 0);
  delay(kBootTestHoldMs);
  showSolidColor(0, 40, 0);
  delay(kBootTestHoldMs);
  showSolidColor(0, 0, 40);
  delay(kBootTestHoldMs);
  showSolidColor(0, 0, 0);
}

static void resetScannerTest() {
  for (uint8_t lane = 0; lane < kLiveLaneCount; lane++) {
    gStrips[lane]->clear();
    gStrips[lane]->show();
  }
  gScannerPixel     = 0;
  gScannerPrevPixel = 0xFFFFU;
  gNextScanMs       = millis();
  gScannerActive    = true;
}

static void updateScannerTest() {
  const uint32_t now = millis();
  if (static_cast<int32_t>(now - gNextScanMs) < 0) return;

  if (gScannerPrevPixel < kLiveLedCount) {
    for (uint8_t lane = 0; lane < kLiveLaneCount; lane++) {
      gStrips[lane]->setPixelColor(gScannerPrevPixel, 0, 0, 0);
    }
  }

  for (uint8_t lane = 0; lane < kLiveLaneCount; lane++) {
    gStrips[lane]->setPixelColor(gScannerPixel, 0, 40, 0);
    gStrips[lane]->show();
  }

  gScannerPrevPixel = gScannerPixel;
  gScannerPixel     = (gScannerPixel + 1) % kLiveLedCount;
  gNextScanMs       = now + kScanStepMs;
}


void setup() {
  Serial.begin(kLiveSerialBaud);
  delay(1000);

  for (uint8_t lane = 0; lane < kLiveLaneCount; lane++) {
    gStrips[lane]->begin();
    gStrips[lane]->clear();
    gStrips[lane]->setBrightness((kGeneralMaxBrightnessPercent * 255U) / 100U);
    gStrips[lane]->show();
  }

  runBootTest();
}

void loop() {
  serviceSerialStream();
  if (serialPlaybackActive()) {
    gScannerActive = false;
    return;
  }
  if (!gScannerActive) resetScannerTest();
  updateScannerTest();
}
