#pragma once
#include <Arduino.h>

// kGeneralMaxBrightnessPercent caps output (100 = PSU-limited only, lower to hard-cap brightness).
constexpr uint8_t  kGeneralMaxBrightnessPercent = 25;
constexpr uint32_t kPsuCurrentMa                = 30000;
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

// ── Whole-row corrections ─────────────────────────────────────────────────
// Rotates the entire row 180°: pixel (0,0) becomes pixel (159,15) and vice versa.
// Affects both panel order and content within every panel simultaneously.
constexpr bool    kTopRowRot180                 = false;
constexpr bool    kBotRowRot180                 = true;

// Reverses only the left-to-right ORDER of panels, content within each panel unchanged.
// Use when the entire row is cabled right-to-left instead of left-to-right.
constexpr bool    kTopRowReverseX               = false;
constexpr bool    kBotRowReverseX               = false;

// ── Per-panel content corrections ─────────────────────────────────────────
// Each flag applies independently to every panel in that row.
// Use when panels are physically mounted in a non-standard orientation.
constexpr bool    kTopPanelRot180               = false;  // rotate 180° (= flipX + flipY)
constexpr bool    kBotPanelRot180               = false;

constexpr bool    kTopPanelFlipX                = false;  // mirror each panel left↔right
constexpr bool    kBotPanelFlipX                = false;

constexpr bool    kTopPanelFlipY                = false;  // mirror each panel top↔bottom
constexpr bool    kBotPanelFlipY                = false;

constexpr uint8_t kSdCs   = 21;
constexpr uint8_t kSdMosi = 19;
constexpr uint8_t kSdMiso = 20;
constexpr uint8_t kSdSck  = 18;

constexpr uint32_t kLiveSerialBaud     = 500000UL;
constexpr uint32_t kLiveFrameTimeoutMs = 50UL;

constexpr uint32_t kSdRetryIntervalMs = 3000;
constexpr uint32_t kSdReadSpeedHz     = 25000000UL;
