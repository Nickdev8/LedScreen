#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include "Config.h"
#include "Leds.h"

// ---------------------------------------------------------------------------
// NeoPixel strips
// ---------------------------------------------------------------------------

static_assert(kLiveLaneCount == 8, "Update strip declarations for non-8-lane configs");
static_assert(
  (kLivePanelsPerLane[0] + kLivePanelsPerLane[1] +
   kLivePanelsPerLane[2] + kLivePanelsPerLane[3] +
   kLivePanelsPerLane[4] + kLivePanelsPerLane[5] +
   kLivePanelsPerLane[6] + kLivePanelsPerLane[7]) == kLivePanelCount,
  "kLivePanelsPerLane must sum to kLivePanelCount"
);

constexpr uint16_t kLane0Leds = static_cast<uint16_t>(kLivePanelsPerLane[0]) * kLiveLedsPerPanel;
constexpr uint16_t kLane1Leds = static_cast<uint16_t>(kLivePanelsPerLane[1]) * kLiveLedsPerPanel;
constexpr uint16_t kLane2Leds = static_cast<uint16_t>(kLivePanelsPerLane[2]) * kLiveLedsPerPanel;
constexpr uint16_t kLane3Leds = static_cast<uint16_t>(kLivePanelsPerLane[3]) * kLiveLedsPerPanel;
constexpr uint16_t kLane4Leds = static_cast<uint16_t>(kLivePanelsPerLane[4]) * kLiveLedsPerPanel;
constexpr uint16_t kLane5Leds = static_cast<uint16_t>(kLivePanelsPerLane[5]) * kLiveLedsPerPanel;
constexpr uint16_t kLane6Leds = static_cast<uint16_t>(kLivePanelsPerLane[6]) * kLiveLedsPerPanel;
constexpr uint16_t kLane7Leds = static_cast<uint16_t>(kLivePanelsPerLane[7]) * kLiveLedsPerPanel;

static Adafruit_NeoPixel strip0(kLane0Leds, kLiveLanePins[0], NEO_GRB + NEO_KHZ800);
static Adafruit_NeoPixel strip1(kLane1Leds, kLiveLanePins[1], NEO_GRB + NEO_KHZ800);
static Adafruit_NeoPixel strip2(kLane2Leds, kLiveLanePins[2], NEO_GRB + NEO_KHZ800);
static Adafruit_NeoPixel strip3(kLane3Leds, kLiveLanePins[3], NEO_GRB + NEO_KHZ800);
static Adafruit_NeoPixel strip4(kLane4Leds, kLiveLanePins[4], NEO_GRB + NEO_KHZ800);
static Adafruit_NeoPixel strip5(kLane5Leds, kLiveLanePins[5], NEO_GRB + NEO_KHZ800);
static Adafruit_NeoPixel strip6(kLane6Leds, kLiveLanePins[6], NEO_GRB + NEO_KHZ800);
static Adafruit_NeoPixel strip7(kLane7Leds, kLiveLanePins[7], NEO_GRB + NEO_KHZ800);
static Adafruit_NeoPixel* gStrips[kLiveLaneCount] = {
  &strip0, &strip1, &strip2, &strip3,
  &strip4, &strip5, &strip6, &strip7,
};

// ---------------------------------------------------------------------------
// Frame buffer (written by SD/animation layer, read by render)
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

  uint16_t px = x % kLivePanelWidth;
  uint16_t py = y % kLivePanelHeight;

  if      (kPanelRotationQuarterTurnsCCW == 1) { uint16_t t = px; px = py;               py = kLivePanelWidth  - 1U - t; }
  else if (kPanelRotationQuarterTurnsCCW == 2) { px = kLivePanelWidth  - 1U - px;        py = kLivePanelHeight - 1U - py; }
  else if (kPanelRotationQuarterTurnsCCW == 3) { uint16_t t = py; py = px;               px = kLivePanelHeight - 1U - t; }

  if (kMatrixSerpentine) {
    const bool odd = (py & 1U) != 0;
    if (kMatrixReverseOddRows ? odd : !odd) px = kLivePanelWidth - 1U - px;
  }

  return { lane, static_cast<uint16_t>(pil * kLiveLedsPerPanel + py * kLivePanelWidth + px) };
}

// ---------------------------------------------------------------------------
// Render + current limiting
// ---------------------------------------------------------------------------

void renderFrameBuffer() {
  for (uint16_t i = 0; i < kLiveLedCount; i++) {
    const PhysicalPixel p   = mapLogicalToPhysical(i);
    const size_t        off = static_cast<size_t>(i) * 3U;
    gStrips[p.lane]->setPixelColor(p.index,
      gFrameBuffer[off], gFrameBuffer[off + 1], gFrameBuffer[off + 2]);
  }
  for (uint8_t l = 0; l < kLiveLaneCount; l++) gStrips[l]->show();
}

void applyCurrentLimit() {
  uint32_t total = 0;
  for (size_t i = 0; i < kLiveFrameBytes; i++) total += gFrameBuffer[i];
  // Estimate actual mA: raw sum → normalize to per-channel max → apply hardware brightness
  // Without the brightness factor the limiter over-estimates by 100/kGeneralMaxBrightnessPercent
  // and kills all content except the sparsest pixels.
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
  for (uint8_t l = 0; l < kLiveLaneCount; l++) {
    gStrips[l]->begin();
    gStrips[l]->clear();
    gStrips[l]->setBrightness((kGeneralMaxBrightnessPercent * 255U) / 100U);
    gStrips[l]->show();
  }
  // Boot indicator: one green pixel per panel for 1 s
  for (uint8_t l = 0; l < kLiveLaneCount; l++) {
    for (uint8_t p = 0; p < kLivePanelsPerLane[l]; p++)
      gStrips[l]->setPixelColor(static_cast<uint16_t>(p) * kLiveLedsPerPanel, 0, 40, 0);
    gStrips[l]->show();
  }
  delay(1000);
  for (uint8_t l = 0; l < kLiveLaneCount; l++) { gStrips[l]->clear(); gStrips[l]->show(); }
}

// ---------------------------------------------------------------------------
// Idle blink  (first LED of every panel blinks red when no SD / no animation)
// ---------------------------------------------------------------------------

bool     gIdleBlinkActive = false;
static uint32_t gNextBlinkMs  = 0;
static bool     gBlinkOn      = false;
constexpr uint32_t kBlinkIntervalMs = 500;

static void setPanelLeds(uint8_t r, uint8_t g, uint8_t b) {
  for (uint8_t l = 0; l < kLiveLaneCount; l++) {
    for (uint8_t p = 0; p < kLivePanelsPerLane[l]; p++)
      gStrips[l]->setPixelColor(static_cast<uint16_t>(p) * kLiveLedsPerPanel, r, g, b);
    gStrips[l]->show();
  }
}

void resetIdleBlink() {
  for (uint8_t l = 0; l < kLiveLaneCount; l++) { gStrips[l]->clear(); gStrips[l]->show(); }
  gBlinkOn = false;
  gNextBlinkMs = millis();
  gIdleBlinkActive = true;
}

void updateIdleBlink() {
  const uint32_t now = millis();
  if (static_cast<int32_t>(now - gNextBlinkMs) < 0) return;
  gBlinkOn = !gBlinkOn;
  setPanelLeds(gBlinkOn ? 40 : 0, 0, 0);
  gNextBlinkMs = now + kBlinkIntervalMs;
}
