#pragma once
#include <Arduino.h>

// ===========================================================================
// SAFETY LIMITS  ← adjust these to match your power supply
// ===========================================================================
constexpr uint8_t  kGeneralMaxBrightnessPercent = 5;    // global brightness cap (%)
constexpr uint32_t kMaxCurrentMa                = 5000; // adaptive current cap (mA)
constexpr uint32_t kLedMaPerChannel             = 20;   // WS2812 max mA per colour channel

// ===========================================================================
// LED output pins  (schematic: T_OUT0..7 → J_LED0..7)
// Top row    (lane 0-3): outputs 0-3 → GPIOs  0,  1, 26, 28
// Bottom row (lane 4-7): outputs 4-7 → GPIOs 12, 13, 14, 15
// Each lane drives panels daisy-chained: 2, 3, 2, 3  (left → right)
// ===========================================================================
constexpr uint8_t kLiveLaneCount = 8;
constexpr uint8_t kLiveLanePins[kLiveLaneCount]      = { 0,  1, 26, 28, 12, 13, 14, 15};
constexpr uint8_t kLivePanelsPerLane[kLiveLaneCount] = { 2,  3,  2,  3,  2,  3,  2,  3};

// ===========================================================================
// Panel / matrix dimensions  (2 rows × 10 columns = 20 panels)
// ===========================================================================
constexpr uint16_t kLivePanelWidth   = 16;
constexpr uint16_t kLivePanelHeight  = 16;
constexpr uint16_t kLiveLedsPerPanel = 256;    // kLivePanelWidth * kLivePanelHeight
constexpr uint8_t  kLivePanelCount   = 20;

constexpr uint16_t kLiveMatrixWidth  = 160;    // kLivePanelWidth  * 10
constexpr uint16_t kLiveMatrixHeight = 32;     // kLivePanelHeight * 2
constexpr uint16_t kLiveLedCount     = 5120;   // kLiveMatrixWidth * kLiveMatrixHeight
constexpr size_t   kLiveFrameBytes   = 15360;  // kLiveLedCount * 3

// ===========================================================================
// Pixel mapping
// ===========================================================================
constexpr bool    kLiveInputColumnMajor         = false;
constexpr uint8_t kPanelRotationQuarterTurnsCCW = 3;
constexpr bool    kMatrixSerpentine             = true;
constexpr bool    kMatrixReverseOddRows         = true;
constexpr bool    kMatrixFlipX                  = false;
constexpr bool    kMatrixFlipY                  = true;

// ===========================================================================
// SD card SPI pins
// ===========================================================================
constexpr uint8_t kSdCs   = 21;
constexpr uint8_t kSdMosi = 19;
constexpr uint8_t kSdMiso = 20;
constexpr uint8_t kSdSck  = 18;

// ===========================================================================
// Serial / USB streaming (xLights live override)
// ===========================================================================
constexpr uint32_t kLiveSerialBaud     = 500000UL;
constexpr uint32_t kLiveFrameTimeoutMs = 50UL;

// ===========================================================================
// SD retry interval when card is absent or animation file missing
// ===========================================================================
constexpr uint32_t kSdRetryIntervalMs = 3000;
