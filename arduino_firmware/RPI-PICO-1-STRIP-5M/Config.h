#pragma once

#include <Arduino.h>

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

// Mirrored single-strip variant using all LED connectors/outputs.
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

// Assumes a 5 m strip at 60 LEDs/m. Adjust if your strip density differs.
constexpr uint16_t kSingleStripLedCount = 300;
constexpr uint8_t kLivePanelsPerLane[kLiveLaneCount] = {1, 1, 1, 1, 1, 1, 1, 1};

constexpr uint16_t kLivePanelWidth = kSingleStripLedCount;
constexpr uint16_t kLivePanelHeight = 1;
constexpr uint16_t kLiveLedsPerPanel = kLivePanelWidth * kLivePanelHeight;  // 300
constexpr uint8_t kLivePanelCount = 1;

constexpr uint16_t kLiveMatrixWidth = kLivePanelWidth;                       // 300
constexpr uint16_t kLiveMatrixHeight = kLivePanelHeight;                     // 1
constexpr uint16_t kLiveLedCount = kLiveMatrixWidth * kLiveMatrixHeight;     // 300
constexpr size_t kLiveFrameBytes = static_cast<size_t>(kLiveLedCount) * 3U;  // 900

constexpr uint32_t kLiveSerialBaud = 500000UL;
constexpr uint32_t kLiveFrameTimeoutMs = 50UL;





// Brightness/power safety.
constexpr uint8_t kGeneralMaxBrightnessPercent = 20;
constexpr uint32_t kMaxCurrentMa = 2000UL;
constexpr uint32_t kLedMaPerChannel = 20UL;



constexpr bool kLiveInputColumnMajor = false;
constexpr uint8_t kPanelRotationQuarterTurnsCCW = 0;
constexpr bool kMatrixSerpentine = false;
constexpr bool kMatrixReverseOddRows = false;
constexpr bool kMatrixFlipX = false;
constexpr bool kMatrixFlipY = false;
