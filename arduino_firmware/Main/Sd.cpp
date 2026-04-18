#include <Arduino.h>
#include <SPI.h>
#include "hardware/gpio.h"
#include "Config.h"
#include "Sd.h"
#include "Leds.h"   // gFrameBuffer, renderFrameBuffer, applyCurrentLimit

// ===========================================================================
// SD card — bare-metal SPI driver  (ported from SDREADD v1.1.0)
// ===========================================================================

static bool gSdhc = false;

static inline uint8_t sdXfer(uint8_t b = 0xFF) { return SPI.transfer(b); }
static inline void    sdCsLow()  { digitalWrite(kSdCs, LOW); }
static inline void    sdCsHigh() { digitalWrite(kSdCs, HIGH); sdXfer(); }

static uint8_t sdWaitR1() {
  for (uint8_t i = 0; i < 8; i++) {
    uint8_t b = sdXfer();
    if (b != 0xFF) return b;
  }
  return 0xFF;
}

static uint8_t sdSendCmd(uint8_t cmd, uint32_t arg, uint8_t crc) {
  sdXfer();
  sdXfer(0x40 | cmd);
  sdXfer(arg >> 24); sdXfer(arg >> 16); sdXfer(arg >> 8); sdXfer(arg);
  sdXfer(crc);
  return sdWaitR1();
}

// CMD55 + ACMDn — CS must not rise between them
static uint8_t sdSendACmd(uint8_t acmd, uint32_t arg) {
  sdSendCmd(55, 0, 0x65);
  sdXfer(); sdXfer(); sdXfer();   // 24 idle clocks before ACMD frame
  sdXfer(0x40 | acmd);
  sdXfer(arg >> 24); sdXfer(arg >> 16); sdXfer(arg >> 8); sdXfer(arg);
  sdXfer(0x01);
  return sdWaitR1();
}

bool sdInit() {
  SPI.end();
  SPI.setRX(kSdMiso); SPI.setTX(kSdMosi); SPI.setSCK(kSdSck);
  SPI.begin();
  gpio_pull_up(kSdMiso);    // SD DO is open-drain; needs pull-up
  pinMode(kSdCs, OUTPUT);

  SPI.beginTransaction(SPISettings(400000UL, MSBFIRST, SPI_MODE0));
  sdCsHigh(); delay(2);
  for (uint8_t i = 0; i < 10; i++) sdXfer();    // ≥80 idle clocks
  sdCsLow();

  const auto abort = [&]() -> bool { sdCsHigh(); SPI.endTransaction(); return false; };

  if (sdSendCmd(0, 0, 0x95) != 0x01)     return abort();  // reset → SPI mode
  if (sdSendCmd(8, 0x1AA, 0x87) != 0x01) return abort();  // SD v2 + 3.3V check
  uint8_t r7[4]; for (auto& b : r7) b = sdXfer();
  if (r7[2] != 0x01 || r7[3] != 0xAA)   return abort();

  uint8_t r = 0x01;
  for (uint32_t t = millis(); r == 0x01 && millis() - t < 1000;) {
    sdCsHigh(); sdCsLow();
    r = sdSendACmd(41, 0x40000000);   // HCS=1
  }
  if (r != 0x00) return abort();

  if (sdSendCmd(58, 0, 0x01) != 0x00) return abort();     // read OCR
  uint8_t ocr[4]; for (auto& b : ocr) b = sdXfer();
  gSdhc = (ocr[0] & 0x40) != 0;

  if (!gSdhc) {
    sdCsHigh(); sdCsLow();
    if (sdSendCmd(16, 512, 0x01) != 0x00) return abort(); // set block length for SDSC
  }

  sdCsHigh();
  SPI.endTransaction();
  return true;
}

bool sdReadSector(uint32_t lba, uint8_t* buf) {
  const uint32_t addr = gSdhc ? lba : lba * 512UL;
  SPI.beginTransaction(SPISettings(8000000UL, MSBFIRST, SPI_MODE0));
  sdCsLow();
  if (sdSendCmd(17, addr, 0x01) != 0x00) { sdCsHigh(); SPI.endTransaction(); return false; }
  bool ok = false;
  for (uint32_t i = 0; i < 50000 && !ok; i++) ok = sdXfer() == 0xFE;
  if (!ok) { sdCsHigh(); SPI.endTransaction(); return false; }
  for (uint16_t i = 0; i < 512; i++) buf[i] = sdXfer();
  sdXfer(); sdXfer();   // discard CRC
  sdCsHigh();
  SPI.endTransaction();
  return true;
}

// ===========================================================================
// FAT32 — minimal reader
// ===========================================================================

static uint32_t gFatLba;
static uint32_t gDataLba;
static uint8_t  gSpc;

uint32_t fatClusterToLba(uint32_t cluster) {
  return gDataLba + (cluster - 2U) * gSpc;
}

static uint8_t gFatSectorBuf[512];

uint32_t fatNextCluster(uint32_t cluster) {
  const uint32_t fatSector = gFatLba + cluster / 128U;
  if (!sdReadSector(fatSector, gFatSectorBuf)) return 0x0FFFFFFF;
  const uint32_t off  = (cluster % 128U) * 4U;
  const uint32_t next = static_cast<uint32_t>(gFatSectorBuf[off])
                      | (static_cast<uint32_t>(gFatSectorBuf[off+1]) << 8)
                      | (static_cast<uint32_t>(gFatSectorBuf[off+2]) << 16)
                      | (static_cast<uint32_t>(gFatSectorBuf[off+3]) << 24);
  return next & 0x0FFFFFFFUL;
}

bool fatMount(uint32_t& outFirstCluster, uint32_t& outFileSize) {
  uint8_t buf[512];

  // MBR → partition 1 start LBA
  if (!sdReadSector(0, buf)) return false;
  uint32_t partLba = 0;
  if (buf[510] == 0x55 && buf[511] == 0xAA) {
    partLba = static_cast<uint32_t>(buf[446+8])
            | (static_cast<uint32_t>(buf[446+9])  <<  8)
            | (static_cast<uint32_t>(buf[446+10]) << 16)
            | (static_cast<uint32_t>(buf[446+11]) << 24);
  }

  // BPB
  if (!sdReadSector(partLba, buf)) return false;
  if (buf[510] != 0x55 || buf[511] != 0xAA) return false;
  const uint16_t bytesPerSec = static_cast<uint16_t>(buf[11]) | (static_cast<uint16_t>(buf[12]) << 8);
  if (bytesPerSec != 512) { Serial.println(F("FAT: sector size != 512")); return false; }
  gSpc = buf[13];
  const uint16_t reservedSecs = static_cast<uint16_t>(buf[14]) | (static_cast<uint16_t>(buf[15]) << 8);
  const uint8_t  numFats      = buf[16];
  const uint32_t secsPerFat   = static_cast<uint32_t>(buf[36])
                              | (static_cast<uint32_t>(buf[37]) << 8)
                              | (static_cast<uint32_t>(buf[38]) << 16)
                              | (static_cast<uint32_t>(buf[39]) << 24);
  const uint32_t rootCluster  = static_cast<uint32_t>(buf[44])
                              | (static_cast<uint32_t>(buf[45]) << 8)
                              | (static_cast<uint32_t>(buf[46]) << 16)
                              | (static_cast<uint32_t>(buf[47]) << 24);
  gFatLba  = partLba + reservedSecs;
  gDataLba = gFatLba + static_cast<uint32_t>(numFats) * secsPerFat;

  // Walk root directory, find first file with extension "LSA"
  uint32_t dirCluster = rootCluster;
  while (dirCluster < 0x0FFFFFF8U) {
    const uint32_t lba = fatClusterToLba(dirCluster);
    for (uint8_t s = 0; s < gSpc; s++) {
      if (!sdReadSector(lba + s, buf)) return false;
      for (uint8_t e = 0; e < 16; e++) {
        uint8_t* en = buf + static_cast<uint16_t>(e) * 32;
        if (en[0] == 0x00) return false;  // end of directory
        if (en[0] == 0xE5) continue;      // deleted
        if (en[11] == 0x0F) continue;     // LFN
        if (en[11] & 0x18) continue;      // volume label / directory
        if (en[8] == 'L' && en[9] == 'S' && en[10] == 'A') {
          const uint32_t hi = static_cast<uint32_t>(en[20]) | (static_cast<uint32_t>(en[21]) << 8);
          const uint32_t lo = static_cast<uint32_t>(en[26]) | (static_cast<uint32_t>(en[27]) << 8);
          outFirstCluster = (hi << 16) | lo;
          outFileSize = static_cast<uint32_t>(en[28])
                      | (static_cast<uint32_t>(en[29]) << 8)
                      | (static_cast<uint32_t>(en[30]) << 16)
                      | (static_cast<uint32_t>(en[31]) << 24);
          return true;
        }
      }
    }
    dirCluster = fatNextCluster(dirCluster);
  }
  return false;
}

// ===========================================================================
// LSA animation streaming
// ===========================================================================

constexpr size_t kLsaHeaderSize = 16;
constexpr char   kLsaMagic[4]   = { 'L', 'S', 'A', '1' };

static uint32_t gAnimFirstCluster = 0;
static uint32_t gAnimDataOffset   = 0;
static uint32_t gAnimFrameCount   = 0;
static uint16_t gAnimFps          = 0;

static uint32_t gReadCluster         = 0;
static uint8_t  gReadSectorInCluster = 0;
static uint16_t gReadByteInSector    = 0;
static uint8_t  gFileSectorBuf[512];
static bool     gSectorLoaded        = false;

static bool streamAdvanceSector() {
  gReadByteInSector = 0;
  gSectorLoaded     = false;
  gReadSectorInCluster++;
  if (gReadSectorInCluster >= gSpc) {
    gReadSectorInCluster = 0;
    gReadCluster = fatNextCluster(gReadCluster);
    if (gReadCluster >= 0x0FFFFFF8U) {
      Serial.println(F("Anim: unexpected end of FAT chain"));
      return false;
    }
  }
  return true;
}

static bool streamRead(uint8_t* dst, size_t len) {
  while (len > 0) {
    if (!gSectorLoaded) {
      const uint32_t lba = fatClusterToLba(gReadCluster) + gReadSectorInCluster;
      if (!sdReadSector(lba, gFileSectorBuf)) return false;
      gSectorLoaded = true;
    }
    const uint16_t avail = 512U - gReadByteInSector;
    const uint16_t take  = static_cast<uint16_t>(len < avail ? len : avail);
    memcpy(dst, gFileSectorBuf + gReadByteInSector, take);
    dst               += take;
    len               -= take;
    gReadByteInSector += take;
    if (gReadByteInSector >= 512) {
      if (!streamAdvanceSector()) return false;
    }
  }
  return true;
}

static bool streamSeekToFirstFrame() {
  gReadCluster         = gAnimFirstCluster;
  gReadSectorInCluster = 0;
  gReadByteInSector    = 0;
  gSectorLoaded        = false;
  uint32_t skip = gAnimDataOffset;
  while (skip > 0) {
    if (!gSectorLoaded) {
      if (!sdReadSector(fatClusterToLba(gReadCluster) + gReadSectorInCluster, gFileSectorBuf))
        return false;
      gSectorLoaded = true;
    }
    const uint16_t avail = 512U - gReadByteInSector;
    const uint16_t take  = static_cast<uint16_t>(skip < avail ? skip : avail);
    gReadByteInSector += take;
    skip              -= take;
    if (gReadByteInSector >= 512) {
      if (!streamAdvanceSector()) return false;
    }
  }
  return true;
}

static bool parseLsaHeader() {
  uint8_t h[kLsaHeaderSize];
  if (!streamRead(h, sizeof(h))) return false;
  if (memcmp(h, kLsaMagic, sizeof(kLsaMagic)) != 0) {
    Serial.println(F("LSA: bad magic")); return false;
  }
  const uint16_t ledCount = static_cast<uint16_t>(h[4]) | (static_cast<uint16_t>(h[5]) << 8);
  const uint16_t fps      = static_cast<uint16_t>(h[6]) | (static_cast<uint16_t>(h[7]) << 8);
  const uint32_t frames   = static_cast<uint32_t>(h[8])
                          | (static_cast<uint32_t>(h[9])  << 8)
                          | (static_cast<uint32_t>(h[10]) << 16)
                          | (static_cast<uint32_t>(h[11]) << 24);
  if (ledCount != kLiveLedCount || fps == 0 || frames == 0) {
    Serial.println(F("LSA: header mismatch")); return false;
  }
  gAnimFps        = fps;
  gAnimFrameCount = frames;
  gAnimDataOffset = kLsaHeaderSize;
  Serial.print(F("LSA: ")); Serial.print(frames);
  Serial.print(F(" frames @ ")); Serial.print(fps); Serial.println(F(" fps (looping)"));
  return true;
}

// ===========================================================================
// Public: SD + animation init and playback
// ===========================================================================

bool     gSdReady       = false;
bool     gAnimReady     = false;
uint32_t gNextSdRetryMs = 0;

static uint32_t gCurrentFrame    = 0;
static uint32_t gNextFrameDueMs  = 0;
static uint32_t gLoopCount       = 0;
static uint32_t gNextProgressMs  = 0;

bool initSdAndAnimation() {
  gSdReady = gAnimReady = false;

  Serial.println(F("--- SD init ---"));
  if (!sdInit()) { Serial.println(F("SD init FAIL")); return false; }
  Serial.println(F("SD init OK"));
  gSdReady = true;

  Serial.println(F("--- FAT mount ---"));
  uint32_t fileSize = 0;
  if (!fatMount(gAnimFirstCluster, fileSize)) {
    Serial.println(F("No .lsa file found on SD root")); return false;
  }
  Serial.print(F("Found .lsa  cluster=")); Serial.print(gAnimFirstCluster);
  Serial.print(F("  size=")); Serial.println(fileSize);

  gReadCluster = gAnimFirstCluster;
  gReadSectorInCluster = 0;
  gReadByteInSector = 0;
  gSectorLoaded = false;
  if (!parseLsaHeader()) return false;
  if (!streamSeekToFirstFrame()) return false;

  gCurrentFrame   = 0;
  gLoopCount      = 0;
  gNextFrameDueMs = millis();
  gNextProgressMs = millis();
  gAnimReady      = true;
  return true;
}

void updateAnimationPlayback() {
  if (!gAnimReady) return;
  const uint32_t now = millis();
  if (static_cast<int32_t>(now - gNextFrameDueMs) < 0) return;

  if (gCurrentFrame >= gAnimFrameCount) {
    if (!streamSeekToFirstFrame()) {
      Serial.println(F("Seek failed — reinit SD"));
      gSdReady = gAnimReady = false;
      return;
    }
    gCurrentFrame   = 0;
    gNextFrameDueMs = now;
    gLoopCount++;
    Serial.print(F(">>> LOOP #")); Serial.println(gLoopCount);
  }

  if (!streamRead(gFrameBuffer, kLiveFrameBytes)) {
    Serial.println(F("Frame read failed — reinit SD"));
    gSdReady = gAnimReady = false;
    return;
  }

  applyCurrentLimit();
  renderFrameBuffer();
  gCurrentFrame++;

  // Progress report once per second
  if (static_cast<int32_t>(now - gNextProgressMs) >= 0) {
    const uint32_t pct = gAnimFrameCount ? (gCurrentFrame * 100UL / gAnimFrameCount) : 0;
    Serial.print(F("frame ")); Serial.print(gCurrentFrame);
    Serial.print(F("/")); Serial.print(gAnimFrameCount);
    Serial.print(F("  ")); Serial.print(pct); Serial.print(F("%"));
    Serial.print(F("  loop#")); Serial.println(gLoopCount);
    gNextProgressMs = now + 1000UL;
  }

  uint32_t interval = 1000UL / gAnimFps;
  if (!interval) interval = 1;
  gNextFrameDueMs = now + interval;
}
