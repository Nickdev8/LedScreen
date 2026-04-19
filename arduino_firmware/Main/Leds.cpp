#include <Adafruit_NeoPXL8.h>
#include <Arduino.h>
#include "Config.h"
#include "Leds.h"

// ---------------------------------------------------------------------------
// NeoPXL8 — all 8 lanes fire simultaneously via RP2040 PIO + DMA.
// All lanes must be the same length; pad to the longest (3 panels = 768 LEDs).
// Lanes with only 2 panels (512 LEDs) have 256 unused slots at the end — harmless.
// ---------------------------------------------------------------------------

static_assert(kLiveLaneCount == 8, "NeoPXL8 requires exactly 8 lanes");
static_assert(
  (kLivePanelsPerLane[0] + kLivePanelsPerLane[1] +
   kLivePanelsPerLane[2] + kLivePanelsPerLane[3] +
   kLivePanelsPerLane[4] + kLivePanelsPerLane[5] +
   kLivePanelsPerLane[6] + kLivePanelsPerLane[7]) == kLivePanelCount,
  "kLivePanelsPerLane must sum to kLivePanelCount"
);

constexpr uint16_t kLedsPerLane = 3U * kLiveLedsPerPanel;  // 768 (max panels per lane × 256)

static int8_t gPins[kLiveLaneCount] = {
  (int8_t)kLiveLanePins[0], (int8_t)kLiveLanePins[1],
  (int8_t)kLiveLanePins[2], (int8_t)kLiveLanePins[3],
  (int8_t)kLiveLanePins[4], (int8_t)kLiveLanePins[5],
  (int8_t)kLiveLanePins[6], (int8_t)kLiveLanePins[7],
};

static Adafruit_NeoPXL8 gLeds(kLedsPerLane, gPins, NEO_GRB);

// NeoPXL8 pixel addressing: pixel p on lane l → global index = p * 8 + l
static inline uint32_t px(uint8_t lane, uint16_t index) {
  return static_cast<uint32_t>(index) * kLiveLaneCount + lane;
}

// ---------------------------------------------------------------------------
// Frame buffer (filled by SD / USB layer, consumed by renderFrameBuffer)
// ---------------------------------------------------------------------------

uint8_t gFrameBuffer[kLiveFrameBytes];

// ---------------------------------------------------------------------------
// Pixel mapping
// ---------------------------------------------------------------------------

struct PhysicalPixel { uint8_t lane; uint16_t index; };

static PhysicalPixel mapLogicalToPhysical(uint16_t idx) {
  uint16_t x, y;
  if (kLiveInputColumnMajor) {
    x = idx / kLiveMatrixHeight;
    y = idx % kLiveMatrixHeight;
  } else {
    x = idx % kLiveMatrixWidth;
    y = idx / kLiveMatrixWidth;
  }
  if (kMatrixFlipX) x = kLiveMatrixWidth  - 1U - x;
  if (kMatrixFlipY) y = kLiveMatrixHeight - 1U - y;

  const uint16_t panelsWide = kLiveMatrixWidth / kLivePanelWidth;
  const uint16_t panelIndex = (y / kLivePanelHeight) * panelsWide + (x / kLivePanelWidth);

  uint8_t pil  = static_cast<uint8_t>(panelIndex);
  uint8_t lane = 0;
  while (lane < kLiveLaneCount && pil >= kLivePanelsPerLane[lane])
    pil -= kLivePanelsPerLane[lane++];
  if (lane >= kLiveLaneCount) return {0, 0};

  uint16_t panelX = x % kLivePanelWidth;
  uint16_t panelY = y % kLivePanelHeight;

  if      (kPanelRotationQuarterTurnsCCW == 1) { uint16_t t = panelX; panelX = panelY;               panelY = kLivePanelWidth  - 1U - t; }
  else if (kPanelRotationQuarterTurnsCCW == 2) { panelX = kLivePanelWidth  - 1U - panelX;            panelY = kLivePanelHeight - 1U - panelY; }
  else if (kPanelRotationQuarterTurnsCCW == 3) { uint16_t t = panelY; panelY = panelX;               panelX = kLivePanelHeight - 1U - t; }

  if (kMatrixSerpentine) {
    const bool odd = (panelY & 1U) != 0;
    if (kMatrixReverseOddRows ? odd : !odd) panelX = kLivePanelWidth - 1U - panelX;
  }

  return { lane, static_cast<uint16_t>(pil * kLiveLedsPerPanel + panelY * kLivePanelWidth + panelX) };
}

// ---------------------------------------------------------------------------
// Render — single show() fires all 8 lanes at the same time
// ---------------------------------------------------------------------------

void renderFrameBuffer() {
  for (uint16_t i = 0; i < kLiveLedCount; i++) {
    const PhysicalPixel p   = mapLogicalToPhysical(i);
    const size_t        off = static_cast<size_t>(i) * 3U;
    gLeds.setPixelColor(px(p.lane, p.index),
      gFrameBuffer[off], gFrameBuffer[off + 1], gFrameBuffer[off + 2]);
  }
  gLeds.show();  // all 8 lanes simultaneously — perfect sync
}

// ---------------------------------------------------------------------------
// Current limiting
// ---------------------------------------------------------------------------

void applyCurrentLimit() {
  uint32_t total = 0;
  for (size_t i = 0; i < kLiveFrameBytes; i++) total += gFrameBuffer[i];
  const uint32_t estimatedMa = (total / 255U) * kLedMaPerChannel
                               * kGeneralMaxBrightnessPercent / 100U;
  if (estimatedMa > kMaxCurrentMa) {
    const uint32_t scale = (kMaxCurrentMa * 255U) / estimatedMa;
    for (size_t i = 0; i < kLiveFrameBytes; i++)
      gFrameBuffer[i] = static_cast<uint8_t>((gFrameBuffer[i] * scale) >> 8);
  }
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

void initLeds() {
  if (!gLeds.begin()) {
    Serial.println(F("NeoPXL8 init FAIL — check library install and pin config"));
  }
  gLeds.setBrightness((kGeneralMaxBrightnessPercent * 255U) / 100U);
  gLeds.clear();
  gLeds.show();

  // Boot indicator: first pixel of every panel, green, for 1 s
  for (uint8_t l = 0; l < kLiveLaneCount; l++)
    for (uint8_t p = 0; p < kLivePanelsPerLane[l]; p++)
      gLeds.setPixelColor(px(l, static_cast<uint16_t>(p) * kLiveLedsPerPanel), 0, 40, 0);
  gLeds.show();
  delay(1000);
  gLeds.clear();
  gLeds.show();
}

// ---------------------------------------------------------------------------
// Idle blink — first LED of every panel blinks red when no SD / no animation
// ---------------------------------------------------------------------------

bool     gIdleBlinkActive = false;
static uint32_t gNextBlinkMs = 0;
static bool     gBlinkOn     = false;
constexpr uint32_t kBlinkIntervalMs = 500;

static void setPanelLeds(uint8_t r, uint8_t g, uint8_t b) {
  gLeds.clear();
  for (uint8_t l = 0; l < kLiveLaneCount; l++)
    for (uint8_t p = 0; p < kLivePanelsPerLane[l]; p++)
      gLeds.setPixelColor(px(l, static_cast<uint16_t>(p) * kLiveLedsPerPanel), r, g, b);
  gLeds.show();
}

void resetIdleBlink() {
  gLeds.clear(); gLeds.show();
  gBlinkOn = false; gNextBlinkMs = millis(); gIdleBlinkActive = true;
}

void updateIdleBlink() {
  const uint32_t now = millis();
  if (static_cast<int32_t>(now - gNextBlinkMs) < 0) return;
  gBlinkOn = !gBlinkOn;
  setPanelLeds(gBlinkOn ? 40 : 0, 0, 0);
  gNextBlinkMs = now + kBlinkIntervalMs;
}
