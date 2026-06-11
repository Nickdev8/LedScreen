#include <Arduino.h>
#include <SPI.h>
#include "hardware/gpio.h"
#include "Config.h"
#include "Sd.h"
#include "Leds.h"

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

// Some cards reject ACMDs if CS toggles between CMD55 and the follow-up command.
static uint8_t sdSendACmd(uint8_t acmd, uint32_t arg) {
  sdSendCmd(55, 0, 0x65);
  sdXfer(); sdXfer(); sdXfer();
  sdXfer(0x40 | acmd);
  sdXfer(arg >> 24); sdXfer(arg >> 16); sdXfer(arg >> 8); sdXfer(arg);
  sdXfer(0x01);
  return sdWaitR1();
}

bool sdInit() {
  SPI.end();
  SPI.setRX(kSdMiso); SPI.setTX(kSdMosi); SPI.setSCK(kSdSck);
  SPI.begin();
  // SD DO is open-drain until the card switches fully into SPI mode.
  gpio_pull_up(kSdMiso);
  pinMode(kSdCs, OUTPUT);

  SPI.beginTransaction(SPISettings(400000UL, MSBFIRST, SPI_MODE0));
  sdCsHigh(); delay(2);
  for (uint8_t i = 0; i < 10; i++) sdXfer();
  sdCsLow();

  const auto abort = [&]() -> bool { sdCsHigh(); SPI.endTransaction(); return false; };

  if (sdSendCmd(0, 0, 0x95) != 0x01)     return abort();
  if (sdSendCmd(8, 0x1AA, 0x87) != 0x01) return abort();
  uint8_t r7[4]; for (auto& b : r7) b = sdXfer();
  if (r7[2] != 0x01 || r7[3] != 0xAA)   return abort();

  uint8_t r = 0x01;
  for (uint32_t t = millis(); r == 0x01 && millis() - t < 1000;) {
    sdCsHigh(); sdCsLow();
    r = sdSendACmd(41, 0x40000000);
  }
  if (r != 0x00) return abort();

  if (sdSendCmd(58, 0, 0x01) != 0x00) return abort();
  uint8_t ocr[4]; for (auto& b : ocr) b = sdXfer();
  gSdhc = (ocr[0] & 0x40) != 0;

  if (!gSdhc) {
    sdCsHigh(); sdCsLow();
    if (sdSendCmd(16, 512, 0x01) != 0x00) return abort();
  }

  sdCsHigh();
  SPI.endTransaction();
  return true;
}

bool sdReadSector(uint32_t lba, uint8_t* buf) {
  const uint32_t addr = gSdhc ? lba : lba * 512UL;
  SPI.beginTransaction(SPISettings(kSdReadSpeedHz, MSBFIRST, SPI_MODE0));
  sdCsLow();
  if (sdSendCmd(17, addr, 0x01) != 0x00) { sdCsHigh(); SPI.endTransaction(); return false; }
  bool ok = false;
  for (uint32_t i = 0; i < 50000 && !ok; i++) ok = sdXfer() == 0xFE;
  if (!ok) { sdCsHigh(); SPI.endTransaction(); return false; }
  for (uint16_t i = 0; i < 512; i++) buf[i] = sdXfer();
  sdXfer(); sdXfer();
  sdCsHigh();
  SPI.endTransaction();
  return true;
}

static bool sdStartMultiRead(uint32_t lba) {
  const uint32_t addr = gSdhc ? lba : lba * 512UL;
  SPI.beginTransaction(SPISettings(kSdReadSpeedHz, MSBFIRST, SPI_MODE0));
  sdCsLow();
  if (sdSendCmd(18, addr, 0x01) != 0x00) { sdCsHigh(); SPI.endTransaction(); return false; }
  return true;
}

static bool sdReadBlock(uint8_t* buf) {
  for (uint32_t i = 0; i < 50000; i++) {
    if (sdXfer() == 0xFE) {
      for (uint16_t j = 0; j < 512; j++) buf[j] = sdXfer();
      sdXfer(); sdXfer();  // discard CRC
      return true;
    }
  }
  return false;
}

static void sdStopMultiRead() {
  sdSendCmd(12, 0, 0x01);  // CMD12 stop transmission
  for (uint32_t i = 0; i < 50000; i++) { if (sdXfer() == 0xFF) break; }
  sdCsHigh();
  SPI.endTransaction();
}

static uint32_t gFatLba;
static uint32_t gDataLba;
uint8_t         gSpc;
static uint32_t gRootCluster;

uint32_t fatClusterToLba(uint32_t cluster) {
  return gDataLba + (cluster - 2U) * gSpc;
}

static uint8_t gFatSectorBuf[512];
static bool    gFatReadFailed = false;

uint32_t fatNextCluster(uint32_t cluster) {
  const uint32_t fatSector = gFatLba + cluster / 128U;
  if (!sdReadSector(fatSector, gFatSectorBuf)) {
    gFatReadFailed = true;
    return 0x0FFFFFFF;
  }
  const uint32_t off  = (cluster % 128U) * 4U;
  const uint32_t next = static_cast<uint32_t>(gFatSectorBuf[off])
                      | (static_cast<uint32_t>(gFatSectorBuf[off+1]) << 8)
                      | (static_cast<uint32_t>(gFatSectorBuf[off+2]) << 16)
                      | (static_cast<uint32_t>(gFatSectorBuf[off+3]) << 24);
  return next & 0x0FFFFFFFUL;
}

bool fatMount() {
  uint8_t buf[512];

  if (!sdReadSector(0, buf)) return false;
  uint32_t partLba = 0;
  if (buf[510] == 0x55 && buf[511] == 0xAA) {
    partLba = static_cast<uint32_t>(buf[446+8])
            | (static_cast<uint32_t>(buf[446+9])  <<  8)
            | (static_cast<uint32_t>(buf[446+10]) << 16)
            | (static_cast<uint32_t>(buf[446+11]) << 24);
  }

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
  gRootCluster = static_cast<uint32_t>(buf[44])
               | (static_cast<uint32_t>(buf[45]) << 8)
               | (static_cast<uint32_t>(buf[46]) << 16)
               | (static_cast<uint32_t>(buf[47]) << 24);
  gFatLba  = partLba + reservedSecs;
  gDataLba = gFatLba + static_cast<uint32_t>(numFats) * secsPerFat;

  return true;
}

constexpr uint8_t  kMaxAnimationFiles = 128;
constexpr uint16_t kMaxFilenameBytes  = 261;

struct AnimationFile {
  char     name[kMaxFilenameBytes];
  uint32_t firstCluster;
  uint32_t fileSize;
  bool     startup;
  bool     enabled;
};

static AnimationFile gAnimationFiles[kMaxAnimationFiles];
static uint8_t       gAnimationFileCount = 0;

static char    gLongName[kMaxFilenameBytes];
static bool    gLongNameValid    = false;
static uint8_t gLongNameChecksum = 0;

static char asciiLower(char c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

static int compareNamesIgnoreCase(const char* a, const char* b) {
  while (*a && *b) {
    const char ca = asciiLower(*a);
    const char cb = asciiLower(*b);
    if (ca != cb) return static_cast<uint8_t>(ca) - static_cast<uint8_t>(cb);
    ++a;
    ++b;
  }
  return static_cast<uint8_t>(*a) - static_cast<uint8_t>(*b);
}

static bool startsWithIgnoreCase(const char* text, const char* prefix) {
  while (*prefix) {
    if (!*text || asciiLower(*text) != asciiLower(*prefix)) return false;
    ++text;
    ++prefix;
  }
  return true;
}

static bool endsWithIgnoreCase(const char* text, const char* suffix) {
  const size_t textLen   = strlen(text);
  const size_t suffixLen = strlen(suffix);
  if (textLen < suffixLen) return false;
  return compareNamesIgnoreCase(text + textLen - suffixLen, suffix) == 0;
}

static uint8_t fatShortNameChecksum(const uint8_t* shortName) {
  uint8_t sum = 0;
  for (uint8_t i = 0; i < 11; i++)
    sum = static_cast<uint8_t>(((sum & 1U) ? 0x80U : 0U) + (sum >> 1) + shortName[i]);
  return sum;
}

static void resetLongName() {
  memset(gLongName, 0, sizeof(gLongName));
  gLongNameValid = false;
  gLongNameChecksum = 0;
}

static void readLongNameEntry(const uint8_t* entry) {
  const uint8_t sequence = entry[0] & 0x1FU;
  if (entry[0] & 0x40U) {
    resetLongName();
    gLongNameValid = true;
    gLongNameChecksum = entry[13];
  }
  if (!gLongNameValid || sequence == 0) {
    resetLongName();
    return;
  }

  static const uint8_t kCharOffsets[13] = {
    1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30
  };
  const uint16_t base = static_cast<uint16_t>(sequence - 1U) * 13U;
  if (base >= kMaxFilenameBytes) {
    resetLongName();
    return;
  }

  for (uint8_t i = 0; i < 13; i++) {
    const uint16_t pos = base + i;
    if (pos >= kMaxFilenameBytes) {
      resetLongName();
      return;
    }
    const uint8_t offset = kCharOffsets[i];
    const uint16_t code = static_cast<uint16_t>(entry[offset])
                        | (static_cast<uint16_t>(entry[offset + 1]) << 8);
    if (code == 0x0000U) {
      gLongName[pos] = '\0';
      break;
    }
    if (code == 0xFFFFU) continue;
    gLongName[pos] = (code >= 0x20U && code <= 0x7EU) ? static_cast<char>(code) : '?';
  }
}

static void formatShortName(const uint8_t* entry, char* out) {
  uint8_t pos = 0;
  for (uint8_t i = 0; i < 8 && entry[i] != ' '; i++)
    out[pos++] = static_cast<char>(entry[i]);
  if (entry[8] != ' ') {
    out[pos++] = '.';
    for (uint8_t i = 8; i < 11 && entry[i] != ' '; i++)
      out[pos++] = static_cast<char>(entry[i]);
  }
  out[pos] = '\0';
}

static void addAnimationFile(const char* name, const uint8_t* entry) {
  if (!endsWithIgnoreCase(name, ".lsa")) return;
  if (gAnimationFileCount >= kMaxAnimationFiles) {
    Serial.println(F("Playlist: file limit reached; remaining .lsa files ignored"));
    return;
  }

  AnimationFile& file = gAnimationFiles[gAnimationFileCount++];
  strncpy(file.name, name, sizeof(file.name) - 1U);
  file.name[sizeof(file.name) - 1U] = '\0';
  const uint32_t hi = static_cast<uint32_t>(entry[20])
                    | (static_cast<uint32_t>(entry[21]) << 8);
  const uint32_t lo = static_cast<uint32_t>(entry[26])
                    | (static_cast<uint32_t>(entry[27]) << 8);
  file.firstCluster = (hi << 16) | lo;
  file.fileSize = static_cast<uint32_t>(entry[28])
                | (static_cast<uint32_t>(entry[29]) << 8)
                | (static_cast<uint32_t>(entry[30]) << 16)
                | (static_cast<uint32_t>(entry[31]) << 24);
  file.startup = startsWithIgnoreCase(file.name, "startup");
  file.enabled = true;
}

static bool scanRootAnimations() {
  uint8_t buf[512];
  gAnimationFileCount = 0;
  resetLongName();

  uint32_t dirCluster = gRootCluster;
  while (dirCluster < 0x0FFFFFF8U) {
    const uint32_t lba = fatClusterToLba(dirCluster);
    for (uint8_t s = 0; s < gSpc; s++) {
      if (!sdReadSector(lba + s, buf)) return false;
      for (uint8_t e = 0; e < 16; e++) {
        uint8_t* en = buf + static_cast<uint16_t>(e) * 32;
        if (en[0] == 0x00) {
          dirCluster = 0x0FFFFFFFU;
          break;
        }
        if (en[0] == 0xE5) {
          resetLongName();
          continue;
        }
        if (en[11] == 0x0F) {
          readLongNameEntry(en);
          continue;
        }

        if (!(en[11] & 0x18U)) {
          char shortName[13];
          formatShortName(en, shortName);
          const bool useLongName = gLongNameValid
                                && gLongName[0] != '\0'
                                && gLongNameChecksum == fatShortNameChecksum(en);
          addAnimationFile(useLongName ? gLongName : shortName, en);
        }
        resetLongName();
      }
      if (dirCluster >= 0x0FFFFFF8U) break;
    }
    if (dirCluster >= 0x0FFFFFF8U) break;
    dirCluster = fatNextCluster(dirCluster);
  }

  for (uint8_t i = 1; i < gAnimationFileCount; i++) {
    AnimationFile key = gAnimationFiles[i];
    uint8_t j = i;
    while (j > 0 && compareNamesIgnoreCase(key.name, gAnimationFiles[j - 1].name) < 0) {
      gAnimationFiles[j] = gAnimationFiles[j - 1];
      --j;
    }
    gAnimationFiles[j] = key;
  }
  return !gFatReadFailed;
}

constexpr size_t kLsaHeaderSize = 16;
constexpr char   kLsaMagic[4]   = { 'L', 'S', 'A', '1' };

static uint32_t gAnimFirstCluster = 0;
static uint32_t gAnimFileSize     = 0;
static uint32_t gAnimDataOffset   = 0;
static uint32_t gAnimFrameCount   = 0;
static uint16_t gAnimFps          = 0;

static uint32_t gReadCluster         = 0;
static uint8_t  gReadSectorInCluster = 0;
static uint16_t gReadByteInSector    = 0;
static uint8_t  gFileSectorBuf[512];
static bool     gSectorLoaded        = false;

static bool streamReadSector(uint32_t lba, uint8_t* dst) {
  if (sdReadSector(lba, dst)) return true;
  gFatReadFailed = true;
  return false;
}

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
      if (!streamReadSector(lba, gFileSectorBuf)) return false;
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
      if (!streamReadSector(fatClusterToLba(gReadCluster) + gReadSectorInCluster, gFileSectorBuf))
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

// Reads exactly kLiveFrameBytes into dst using CMD18 for the bulk, CMD17 for partials.
// Each frame starts at gReadByteInSector==16 within its first sector (16-byte LSA header offset).
// After the call, gReadByteInSector==16 and gSectorLoaded==true for the next frame.
static bool streamReadFrame(uint8_t* dst) {
  static_assert(kLiveFrameBytes == 15360, "Optimized SD frame reader assumes 15360-byte frames");

  // Part 1: 496-byte tail of the current sector.
  // After streamSeekToFirstFrame (or a previous streamReadFrame), gSectorLoaded is always true.
  if (!gSectorLoaded) {
    if (!streamReadSector(fatClusterToLba(gReadCluster) + gReadSectorInCluster, gFileSectorBuf))
      return false;
    gSectorLoaded = true;
  }
  const uint16_t tailBytes = 512U - gReadByteInSector;  // always 496
  memcpy(dst, gFileSectorBuf + gReadByteInSector, tailBytes);
  dst += tailBytes;
  gReadByteInSector = 0;
  gSectorLoaded     = false;
  gReadSectorInCluster++;
  if (gReadSectorInCluster >= gSpc) {
    gReadSectorInCluster = 0;
    gReadCluster = fatNextCluster(gReadCluster);
    if (gReadCluster >= 0x0FFFFFF8U) { Serial.println(F("Anim: FAT chain ended early")); return false; }
  }

  // Part 2: 29 full sectors via CMD18, cluster-by-cluster.
  uint8_t sectorsLeft = 29;
  while (sectorsLeft > 0) {
    const uint8_t inCluster    = gSpc - gReadSectorInCluster;
    const uint8_t blocksThisRun = inCluster < sectorsLeft ? inCluster : sectorsLeft;
    const uint32_t startLba    = fatClusterToLba(gReadCluster) + gReadSectorInCluster;

    if (!sdStartMultiRead(startLba)) {
      gFatReadFailed = true;
      return false;
    }
    for (uint8_t b = 0; b < blocksThisRun; b++) {
      if (!sdReadBlock(dst)) {
        gFatReadFailed = true;
        sdStopMultiRead();
        return false;
      }
      dst += 512;
    }
    sdStopMultiRead();

    gReadSectorInCluster += blocksThisRun;
    sectorsLeft          -= blocksThisRun;
    if (gReadSectorInCluster >= gSpc) {
      gReadSectorInCluster = 0;
      gReadCluster = fatNextCluster(gReadCluster);
      if (gReadCluster >= 0x0FFFFFF8U && sectorsLeft > 0) {
        Serial.println(F("Anim: FAT chain ended early")); return false;
      }
    }
  }

  // Part 3: first 16 bytes of the next sector — cache it for next frame's Part 1.
  if (!streamReadSector(fatClusterToLba(gReadCluster) + gReadSectorInCluster, gFileSectorBuf))
    return false;
  gSectorLoaded     = true;
  memcpy(dst, gFileSectorBuf, 16U);
  gReadByteInSector = 16;
  // gReadSectorInCluster stays: next frame starts at byte 16 of this sector.
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
  const uint64_t expectedSize = static_cast<uint64_t>(kLsaHeaderSize)
                              + static_cast<uint64_t>(frames) * kLiveFrameBytes;
  if (expectedSize > gAnimFileSize) {
    Serial.println(F("LSA: file is truncated")); return false;
  }
  gAnimFps        = fps;
  gAnimFrameCount = frames;
  gAnimDataOffset = kLsaHeaderSize;
  Serial.print(F("LSA: ")); Serial.print(frames);
  Serial.print(F(" frames @ ")); Serial.print(fps); Serial.println(F(" fps"));
  return true;
}

bool     gSdReady       = false;
bool     gAnimReady     = false;
uint32_t gNextSdRetryMs = 0;

static uint32_t gCurrentFrame    = 0;
static uint32_t gNextFrameDueMs  = 0;
static uint32_t gNextProgressMs  = 0;
static bool     gDisplayInFlight = false;

enum class PlaylistPhase : uint8_t {
  Startup,
  Regular,
  Done,
};

static PlaylistPhase gPlaylistPhase = PlaylistPhase::Done;
static int16_t       gCurrentFileIndex = -1;

static bool openAnimation(uint8_t index) {
  AnimationFile& file = gAnimationFiles[index];
  gAnimFirstCluster = file.firstCluster;
  gAnimFileSize     = file.fileSize;
  gFatReadFailed    = false;

  gReadCluster = gAnimFirstCluster;
  gReadSectorInCluster = 0;
  gReadByteInSector = 0;
  gSectorLoaded = false;
  if (!parseLsaHeader() || !streamSeekToFirstFrame()) {
    if (gFatReadFailed) {
      Serial.println(F("Playlist: SD read failed"));
      gSdReady = false;
      gPlaylistPhase = PlaylistPhase::Done;
      return false;
    }
    Serial.print(F("Playlist: skipping invalid file ")); Serial.println(file.name);
    file.enabled = false;
    return false;
  }

  Serial.print(file.startup ? F("Startup: ") : F("Playlist: "));
  Serial.println(file.name);
  gCurrentFileIndex = index;
  gCurrentFrame     = 0;
  gNextFrameDueMs   = millis();
  gNextProgressMs   = millis();
  gAnimReady        = true;
  return true;
}

static int16_t findNextFile(bool startup, int16_t afterIndex, bool wrap) {
  if (gAnimationFileCount == 0) return -1;
  const uint8_t attempts = wrap ? gAnimationFileCount : static_cast<uint8_t>(
    gAnimationFileCount - (afterIndex + 1)
  );
  for (uint8_t step = 1; step <= attempts; step++) {
    const int16_t rawIndex = afterIndex + step;
    const uint8_t index = wrap
      ? static_cast<uint8_t>(rawIndex % gAnimationFileCount)
      : static_cast<uint8_t>(rawIndex);
    const AnimationFile& file = gAnimationFiles[index];
    if (file.enabled && file.startup == startup) return index;
  }
  return -1;
}

static bool startNextAnimation() {
  const uint16_t maxAttempts = static_cast<uint16_t>(gAnimationFileCount) * 2U + 2U;
  for (uint16_t attempts = 0; attempts < maxAttempts; attempts++) {
    int16_t next = -1;
    if (gPlaylistPhase == PlaylistPhase::Startup) {
      next = findNextFile(true, gCurrentFileIndex, false);
      if (next < 0) {
        gPlaylistPhase = PlaylistPhase::Regular;
        gCurrentFileIndex = -1;
        continue;
      }
    } else if (gPlaylistPhase == PlaylistPhase::Regular) {
      next = findNextFile(false, gCurrentFileIndex, true);
      if (next < 0) break;
    } else {
      break;
    }

    if (openAnimation(static_cast<uint8_t>(next))) return true;
    if (!gSdReady) {
      gAnimReady = false;
      return false;
    }
    gCurrentFileIndex = next;
  }

  gPlaylistPhase = PlaylistPhase::Done;
  gAnimReady = false;
  Serial.println(F("Playlist: no playable regular animations"));
  return false;
}

bool initSdPlaylist() {
  gSdReady = gAnimReady = false;
  gDisplayInFlight = false;
  gCurrentFileIndex = -1;
  gPlaylistPhase = PlaylistPhase::Startup;
  gFatReadFailed = false;

  Serial.println(F("--- SD init ---"));
  if (!sdInit()) { Serial.println(F("SD init FAIL")); return false; }
  Serial.println(F("SD init OK"));
  gSdReady = true;

  Serial.println(F("--- FAT mount ---"));
  if (!fatMount()) {
    Serial.println(F("FAT mount FAIL"));
    gSdReady = false;
    return false;
  }
  if (!scanRootAnimations()) {
    Serial.println(F("Playlist: root scan failed"));
    gSdReady = false;
    return false;
  }

  Serial.print(F("Playlist: found ")); Serial.print(gAnimationFileCount);
  Serial.println(F(" root-level .lsa file(s)"));
  startNextAnimation();
  return gAnimReady;
}

void updateAnimationPlayback() {
  if (!gAnimReady) return;

  const uint32_t interval = gAnimFps ? (1000UL / gAnimFps) : 1UL;

  // File boundary: finish the final display before opening the next animation.
  if (gCurrentFrame >= gAnimFrameCount) {
    if (gDisplayInFlight) { waitForDisplay(); gDisplayInFlight = false; }
    if (!startNextAnimation()) return;
  }

  // Bootstrap: no display is running yet; prime the pipeline.
  if (!gDisplayInFlight) {
    if (!streamReadFrame(gFrameBufferBack)) {
      Serial.println(F("Frame read failed — reinit SD"));
      gSdReady = gAnimReady = false;
      return;
    }
    uint8_t* tmp = gFrameBuffer; gFrameBuffer = gFrameBufferBack; gFrameBufferBack = tmp;
    applyCurrentLimit();
    prepareLaneBuffers();
    startDisplay();
    gDisplayInFlight = true;
    gCurrentFrame++;
    gNextFrameDueMs = millis() + interval;
    return;
  }

  // Steady-state: read the next frame into the back buffer while the current
  // DMA display is still running (SPI and PIO/DMA are independent buses).
  if (!streamReadFrame(gFrameBufferBack)) {
    Serial.println(F("Frame read failed — reinit SD"));
    waitForDisplay();
    gSdReady = gAnimReady = gDisplayInFlight = false;
    return;
  }

  // Wait for the previous DMA transfer to finish (~23 ms wall-clock from when
  // startDisplay() was called; the SD read above consumed ~5 ms of that window).
  waitForDisplay();
  gDisplayInFlight = false;

  // Timing gate: hold until the frame is actually due (only active when the
  // animation FPS is low enough that display finishes before the next deadline).
  while (static_cast<int32_t>(gNextFrameDueMs - millis()) > 0) {}

  // Swap, convert, and kick off the next display.
  uint8_t* tmp = gFrameBuffer; gFrameBuffer = gFrameBufferBack; gFrameBufferBack = tmp;
  applyCurrentLimit();
  prepareLaneBuffers();
  startDisplay();
  gDisplayInFlight = true;
  gCurrentFrame++;

  // Increment (not rebase) so timing drift doesn't accumulate.
  gNextFrameDueMs += interval;

  const uint32_t now = millis();
  if (static_cast<int32_t>(now - gNextProgressMs) >= 0) {
    const uint32_t pct = gAnimFrameCount ? (gCurrentFrame * 100UL / gAnimFrameCount) : 0;
    Serial.print(F("frame ")); Serial.print(gCurrentFrame);
    Serial.print(F("/")); Serial.print(gAnimFrameCount);
    Serial.print(F("  ")); Serial.print(pct); Serial.println(F("%"));
    gNextProgressMs = now + 1000UL;
  }
}
