#include <Arduino.h>
#include <SPI.h>
#include "hardware/gpio.h"

namespace {

constexpr const char* kVersion  = "v1.1.0";

constexpr uint8_t  kCs         = 21;
constexpr uint8_t  kMosi       = 19;
constexpr uint8_t  kMiso       = 20;
constexpr uint8_t  kSck        = 18;
constexpr uint32_t kInitHz     = 400000UL;
constexpr uint32_t kDataHz     = 8000000UL;
constexpr uint16_t kSectorSize = 512;

SPIClassRP2040& gSpi  = SPI;
bool            gSdhc = false;

// ── SPI primitives ─────────────────────────────────────────────────────────────

inline uint8_t xfer(uint8_t b = 0xFF) { return gSpi.transfer(b); }
inline void    csLow()  { digitalWrite(kCs, LOW); }
inline void    csHigh() { digitalWrite(kCs, HIGH); xfer(); }

uint8_t waitR1() {
  for (uint8_t i = 0; i < 8; ++i) {
    uint8_t b = xfer();
    if (b != 0xFF) return b;
  }
  return 0xFF;
}

// ── SD commands ────────────────────────────────────────────────────────────────
uint8_t sendCmd(uint8_t cmd, uint32_t arg, uint8_t crc) {
  xfer();  // NCC: ≥8 idle clocks between frames
  xfer(0x40 | cmd);
  xfer(arg >> 24); xfer(arg >> 16); xfer(arg >> 8); xfer(arg);
  xfer(crc);
  return waitR1();
}

uint8_t sendACmd(uint8_t acmd, uint32_t arg) {
  sendCmd(55, 0, 0x65);    // CRC7 for CMD55 arg=0 + stop bit
  xfer(); xfer(); xfer();  // 24 idle clocks gap before ACMD frame
  xfer(0x40 | acmd);
  xfer(arg >> 24); xfer(arg >> 16); xfer(arg >> 8); xfer(arg);
  xfer(0x01);              // stop bit; CRC disabled after CMD0
  return waitR1();
}

// ── Initialisation ─────────────────────────────────────────────────────────────
bool sdInit() {
  gSpi.end();
  gSpi.setRX(kMiso); gSpi.setTX(kMosi); gSpi.setSCK(kSck);
  gSpi.begin();
  gpio_pull_up(kMiso);
  pinMode(kCs, OUTPUT);

  gSpi.beginTransaction(SPISettings(kInitHz, MSBFIRST, SPI_MODE0));
  csHigh(); delay(2);
  for (uint8_t i = 0; i < 10; ++i) xfer();
  csLow();

  const auto abort = [&]() -> bool { csHigh(); gSpi.endTransaction(); return false; };

  if (sendCmd(0, 0, 0x95) != 0x01) return abort();

  if (sendCmd(8, 0x1AA, 0x87) != 0x01) return abort();
  uint8_t r7[4]; for (auto& b : r7) b = xfer();
  if (r7[2] != 0x01 || r7[3] != 0xAA) return abort();

  uint8_t r = 0x01;
  for (uint32_t t = millis(); r == 0x01 && millis() - t < 1000; ) {
    csHigh(); csLow();
    r = sendACmd(41, 0x40000000);
  }
  if (r != 0x00) return abort();

  if (sendCmd(58, 0, 0x01) != 0x00) return abort();
  uint8_t ocr[4]; for (auto& b : ocr) b = xfer();
  gSdhc = (ocr[0] & 0x40) != 0;

  if (!gSdhc) {
    csHigh(); csLow();
    if (sendCmd(16, kSectorSize, 0x01) != 0x00) return abort();
  }

  csHigh();
  gSpi.endTransaction();
  return true;
}

// ── Sector read ────────────────────────────────────────────────────────────────
bool sdRead(uint32_t lba, uint8_t* buf) {
  const uint32_t addr = gSdhc ? lba : lba * kSectorSize;

  gSpi.beginTransaction(SPISettings(kDataHz, MSBFIRST, SPI_MODE0));
  csLow();

  if (sendCmd(17, addr, 0x01) != 0x00) {
    csHigh(); gSpi.endTransaction(); return false;
  }

  bool ok = false;
  for (uint32_t i = 0; i < 50000 && !ok; ++i) ok = xfer() == 0xFE;
  if (!ok) { csHigh(); gSpi.endTransaction(); return false; }

  for (uint16_t i = 0; i < kSectorSize; ++i) buf[i] = xfer();
  xfer(); xfer();

  csHigh();
  gSpi.endTransaction();
  return true;
}

// ── Utilities ──────────────────────────────────────────────────────────────────
void printHex(const uint8_t* d, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    if (d[i] < 0x10) Serial.print('0');
    Serial.print(d[i], HEX);
    if (i + 1 < n) Serial.print(' ');
  }
  Serial.println();
}

}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  Serial.print(F("SDREADD  fw=")); Serial.println(kVersion);

  if (!sdInit()) { Serial.println(F("Init: FAIL")); return; }
  Serial.print(F("Init: PASS  ")); Serial.println(gSdhc ? F("SDHC/SDXC") : F("SDSC"));

  uint8_t buf[kSectorSize];
  if (!sdRead(0, buf)) { Serial.println(F("Sector 0: FAIL")); return; }
  Serial.print(F("Sector 0 [0..31]: ")); printHex(buf, 32);
}

void loop() { delay(1000); }
