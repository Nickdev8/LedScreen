#pragma once
#include <Arduino.h>

// Keep these limits aligned with the installed PSU and wiring budget.
constexpr uint8_t  kGeneralMaxBrightnessPercent = 5;
constexpr uint32_t kMaxCurrentMa                = 5000;
constexpr uint32_t kLedMaPerChannel             = 20;

// Lane order follows the PCB routing, not the visual left-to-right panel order.
constexpr uint8_t kLiveLaneCount = 8;
constexpr uint8_t kLiveLanePins[kLiveLaneCount]      = {12, 13, 14, 15,  0,  1, 26, 28};
constexpr uint8_t kLivePanelsPerLane[kLiveLaneCount] = { 2,  3,  2,  3,  2,  3,  2,  3};

constexpr uint16_t kLivePanelWidth   = 16;
constexpr uint16_t kLivePanelHeight  = 16;
constexpr uint16_t kLiveLedsPerPanel = 256;
constexpr uint8_t  kLivePanelCount   = 20;

constexpr uint16_t kLiveMatrixWidth  = 160;
constexpr uint16_t kLiveMatrixHeight = 32;
constexpr uint16_t kLiveLedCount     = 5120;
constexpr size_t   kLiveFrameBytes   = 15360;

constexpr bool    kLiveInputColumnMajor         = false;
constexpr uint8_t kPanelRotationQuarterTurnsCCW = 3;
constexpr bool    kMatrixSerpentine             = true;
constexpr bool    kMatrixReverseOddRows         = true;
constexpr bool    kMatrixFlipX                  = false;
constexpr bool    kMatrixFlipY                  = true;

constexpr uint8_t kSdCs   = 21;
constexpr uint8_t kSdMosi = 19;
constexpr uint8_t kSdMiso = 20;
constexpr uint8_t kSdSck  = 18;

constexpr uint32_t kLiveSerialBaud     = 500000UL;
constexpr uint32_t kLiveFrameTimeoutMs = 50UL;

constexpr uint32_t kSdRetryIntervalMs = 3000;
