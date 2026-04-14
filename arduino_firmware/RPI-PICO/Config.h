#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Active live settings (used by current RPI-PICO.ino)
// ---------------------------------------------------------------------------
// LED data output wiring from schematic:
// T_OUT0..7 (U1 A0..A7) -> LED0..7_DATA -> J_LED0..7 pin 2
constexpr uint8_t kLedOutputCount = 8;
constexpr uint8_t kLedGpioByOutput[kLedOutputCount] = {
  0,   // GPOIO 0 output 0: T_OUT0 / LED0_DATA / J_LED0
  1,   // GPOIO 1 output 1: T_OUT1 / LED1_DATA / J_LED1
  26,  // GPOIO 26 output 2: T_OUT2 / LED2_DATA / J_LED2
  28,  // GPOIO 28 output 3: T_OUT3 / LED3_DATA / J_LED3
  12,  // GPOIO 12 output 4: T_OUT4 / LED4_DATA / J_LED4
  13,  // GPOIO 13 output 5: T_OUT5 / LED5_DATA / J_LED5
  14,  // GPOIO 14 output 6: T_OUT6 / LED6_DATA / J_LED6
  15   // GPOIO 15 output 7: T_OUT7 / LED7_DATA / J_LED7
};

// Twenty panels total in a 2 high x 10 wide wall:
// - top row:    outputs 0..3 drive 2,3,2,3 panels left-to-right
// - bottom row: outputs 4..7 drive 2,3,2,3 panels left-to-right
constexpr uint8_t kLiveLaneCount = 8;
constexpr uint8_t kLiveLaneOutputs[kLiveLaneCount] = {0, 1, 2, 3, 4, 5, 6, 7};
constexpr uint8_t kLiveLanePins[kLiveLaneCount] = {
  kLedGpioByOutput[kLiveLaneOutputs[0]],
  kLedGpioByOutput[kLiveLaneOutputs[1]],
  kLedGpioByOutput[kLiveLaneOutputs[2]],
  kLedGpioByOutput[kLiveLaneOutputs[3]],
  kLedGpioByOutput[kLiveLaneOutputs[4]],
  kLedGpioByOutput[kLiveLaneOutputs[5]],
  kLedGpioByOutput[kLiveLaneOutputs[6]],
  kLedGpioByOutput[kLiveLaneOutputs[7]],
};
constexpr uint8_t kLivePanelsPerLane[kLiveLaneCount] = {2, 3, 2, 3, 2, 3, 2, 3};

constexpr uint16_t kLivePanelWidth = 16;
constexpr uint16_t kLivePanelHeight = 16;
constexpr uint16_t kLiveLedsPerPanel = kLivePanelWidth * kLivePanelHeight;  // 256
constexpr uint8_t kLivePanelCount = 20;

constexpr uint16_t kLiveMatrixWidth = kLivePanelWidth * 10;                 // 160
constexpr uint16_t kLiveMatrixHeight = kLivePanelHeight * 2;                // 32
constexpr uint16_t kLiveLedCount = kLiveMatrixWidth * kLiveMatrixHeight;    // 5120
constexpr size_t kLiveFrameBytes = static_cast<size_t>(kLiveLedCount) * 3U; // 15360

constexpr uint32_t kLiveSerialBaud = 500000UL;
constexpr uint32_t kLiveFrameTimeoutMs = 50UL;





// Brightness/power safety.
// Hard cap: never exceed this global brightness percentage.
constexpr uint8_t kGeneralMaxBrightnessPercent = 10;
// Adaptive cap: reduce brightness further to keep estimated current under this.
constexpr uint32_t kMaxCurrentMa = 30000UL;
constexpr uint32_t kLedMaPerChannel = 20UL;  // WS2812 worst-case per color channel





// Logical XY -> physical strip mapping.
// xLights Generic Serial matrix usually sends by string first.
// For 16x160 (16 strings, 160 pixels each), enable column-major decode.
constexpr bool kLiveInputColumnMajor = false;
// Rotate each panel before serpentine/index mapping: 0,1,2,3 quarter-turns CCW.
constexpr uint8_t kPanelRotationQuarterTurnsCCW = 3;
constexpr bool kMatrixSerpentine = true;
constexpr bool kMatrixReverseOddRows = true;
constexpr bool kMatrixFlipX = false;
constexpr bool kMatrixFlipY = true;

// ---------------------------------------------------------------------------
// Legacy/reference config (SD + older firmware modes)
// ---------------------------------------------------------------------------
constexpr uint16_t kPanelWidth = 16;    // LEDs per row in one panel
constexpr uint16_t kPanelHeight = 16;   // LEDs per column in one panel
constexpr uint8_t kPanelsHigh = 2;      // panel rows in the full wall
constexpr uint8_t kPanelsWide = 10;     // panel columns in the full wall

// Derived counts.
constexpr uint16_t kLedsPerPanel = kPanelWidth * kPanelHeight;  // 256
constexpr uint16_t kPanelCount = static_cast<uint16_t>(kPanelsHigh) * kPanelsWide;  // 20
constexpr uint16_t kLedCount = kLedsPerPanel * kPanelCount;  // 5120 total LEDs
constexpr size_t kFrameBytes = static_cast<size_t>(kLedCount) * 3U;  // RGB bytes

// Current firmware drives one output lane. Pick which board connector to use.
constexpr uint8_t kActiveLedOutput = 0;  // 0..7 (default: J_LED0)
constexpr uint8_t kLedPin = kLedGpioByOutput[kActiveLedOutput];

// ---------------------------------------------------------------------------
// SD wiring
// ---------------------------------------------------------------------------
constexpr uint8_t kSdCs = 21;
constexpr uint8_t kSdMosi = 19;
constexpr uint8_t kSdMiso = 20;
constexpr uint8_t kSdSck = 18;

// ---------------------------------------------------------------------------
// Runtime timings
// ---------------------------------------------------------------------------
constexpr uint32_t kSerialBaud = 115200UL;
constexpr uint32_t kSerialWaitMs = 3000UL;
constexpr uint32_t kSdRetryMs = 5000UL;
constexpr uint32_t kStatusPrintMs = 3000UL;

// Playlist and animation defaults.
constexpr const char* kPlaylistPath = "/playlist.txt";
constexpr uint16_t kDefaultFrameDelayMs = 33;
constexpr size_t kMaxPlaylistEntries = 32;
constexpr size_t kMaxPathLen = 64;
