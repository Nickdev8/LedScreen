// =============================================================================
// FirmwareUpdate.h — SD card firmware updater for RP2040
// =============================================================================
//
// HOW IT WORKS
// ------------
// On boot, if an SD card is mounted and a file named /firmware.uf2 exists in
// the root, this code flashes it page by page directly into the Pico's flash
// memory and reboots into the new firmware.
//
// After a successful flash the file is renamed to /firmware.done so the Pico
// does not re-flash on every subsequent boot.
//
// HOW TO USE
// ----------
// 1. In Arduino IDE choose: Sketch -> Export Compiled Binary
//    This writes a .uf2 file next to the .ino file.
// 2. Copy that .uf2 file to the SD card root and rename it to firmware.uf2
// 3. Insert the SD card and power on (or reset) the Pico.
// 4. The Pico prints progress over serial, renames the file, and reboots.
//
// WARNING — DO NOT MODIFY THIS FILE unless you know exactly what you are doing.
// -------------------------------------------------------------------------------
// The flash_range_erase / flash_range_program calls below run with interrupts
// disabled and write directly to the Pico's internal flash. A bug here can
// brick the board. The board can always be recovered by holding BOOTSEL on
// power-on and reflashing over USB, but avoid unnecessary edits.
// =============================================================================

#pragma once

#include <SdFat.h>
#include "hardware/flash.h"
#include "hardware/sync.h"

// UF2 format constants (do not change — defined by the UF2 specification)
static constexpr uint32_t kUf2Magic0       = 0x0A324655UL;  // "UF2\n"
static constexpr uint32_t kUf2Magic1       = 0x9E5D5157UL;
static constexpr uint32_t kUf2MagicEnd     = 0x0AB16F30UL;
static constexpr uint32_t kUf2FlagNoFlash  = 0x00001000UL;  // file-container block, skip
static constexpr uint32_t kUf2FlagFamilyId = 0x00002000UL;  // familyID field present
static constexpr uint32_t kRp2040FamilyId  = 0xe48bff56UL;  // RP2040 family identifier
static constexpr uint32_t kRp2040FlashBase = 0x10000000UL;  // start of RP2040 flash in address space

static constexpr const char* kUpdateFile = "/firmware.uf2";
static constexpr const char* kUpdateDone = "/firmware.done";

// Read a little-endian uint32 from a byte buffer.
static uint32_t uf2U32(const uint8_t* p) {
  return (uint32_t)p[0]
       | ((uint32_t)p[1] << 8)
       | ((uint32_t)p[2] << 16)
       | ((uint32_t)p[3] << 24);
}

// Call once after SD is mounted (e.g. in setup()).
// If /firmware.uf2 is present it flashes it and reboots — never returns.
// If the file is absent or invalid it returns immediately.
void checkForFirmwareUpdate(SdFat32& fs) {
  File32 f;
  if (!f.open(kUpdateFile, O_RDONLY)) return;  // no update file, nothing to do

  const uint32_t fileSize = f.fileSize();
  if (fileSize < 512 || (fileSize % 512) != 0) {
    Serial.println("[update] firmware.uf2 has bad size, skipping");
    f.close();
    return;
  }

  Serial.println("[update] Found /firmware.uf2 — flashing...");
  Serial.flush();

  uint8_t  buf[512];
  uint32_t written    = 0;
  uint32_t prevSector = 0xFFFFFFFFUL;  // tracks the last erased sector
  bool     ok         = true;

  while (ok && f.read(buf, 512) == 512) {

    // -------------------------------------------------------------------------
    // Validate the three UF2 magic numbers that bracket every 512-byte block.
    // A mismatch means the file is corrupt or not a UF2 — abort immediately
    // rather than risk writing garbage to flash.
    // -------------------------------------------------------------------------
    if (uf2U32(&buf[0])   != kUf2Magic0  ||
        uf2U32(&buf[4])   != kUf2Magic1  ||
        uf2U32(&buf[508]) != kUf2MagicEnd) {
      Serial.println("[update] Bad UF2 magic — aborting");
      ok = false;
      break;
    }

    const uint32_t flags        = uf2U32(&buf[8]);
    const uint32_t targetAddr   = uf2U32(&buf[12]);
    const uint32_t payloadSize  = uf2U32(&buf[16]);
    const uint32_t familyOrSize = uf2U32(&buf[28]);

    // Skip file-container blocks (they carry source file data, not flash data).
    if (flags & kUf2FlagNoFlash) continue;

    // If a family ID is present, only accept RP2040 blocks.
    if ((flags & kUf2FlagFamilyId) && familyOrSize != kRp2040FamilyId) continue;

    // Every RP2040 UF2 data block carries exactly 256 bytes (one flash page).
    if (payloadSize != FLASH_PAGE_SIZE) continue;

    // Ignore blocks targeting addresses outside flash.
    if (targetAddr < kRp2040FlashBase) continue;

    const uint32_t flashOffset = targetAddr - kRp2040FlashBase;
    const uint32_t sector      = flashOffset & ~((uint32_t)(FLASH_SECTOR_SIZE - 1));

    // -------------------------------------------------------------------------
    // Erase the sector the first time we need to write into it.
    // flash_range_erase requires interrupts disabled; the RP2040 ROM routine
    // temporarily disables XIP so no code runs from flash during the erase.
    // -------------------------------------------------------------------------
    if (sector != prevSector) {
      uint32_t irq = save_and_disable_interrupts();
      flash_range_erase(sector, FLASH_SECTOR_SIZE);
      restore_interrupts(irq);
      prevSector = sector;
    }

    // -------------------------------------------------------------------------
    // Program one 256-byte page. Payload data starts at byte 32 of the block.
    // Same interrupt/XIP precautions as erase above.
    // -------------------------------------------------------------------------
    uint32_t irq = save_and_disable_interrupts();
    flash_range_program(flashOffset, &buf[32], FLASH_PAGE_SIZE);
    restore_interrupts(irq);

    ++written;
    if (written % 128 == 0) {
      Serial.print("[update] ");
      Serial.print(written);
      Serial.println(" pages written...");
      Serial.flush();
    }
  }

  f.close();

  if (!ok) {
    Serial.println("[update] Aborted — flash contents may be incomplete.");
    return;
  }

  // Rename the file so the Pico does not re-flash on the next boot.
  fs.rename(kUpdateFile, kUpdateDone);

  Serial.print("[update] Complete — ");
  Serial.print(written);
  Serial.println(" pages written. Rebooting...");
  Serial.flush();
  delay(200);

  // Trigger a watchdog reset to boot into the new firmware.
  watchdog_enable(1, 1);
  while (true) {}  // wait for watchdog to fire — never returns
}
