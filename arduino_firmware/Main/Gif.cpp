#include <Arduino.h>
#include "Config.h"
#include "Gif.h"
#include "Sd.h"
#include "Leds.h"

// ── Seekable SD file reader ──────────────────────────────────────────────────

struct GifReader {
    uint32_t firstCluster;
    uint32_t fileSize;
    uint32_t curCluster;
    uint8_t  curSec;          // sector within current cluster
    uint16_t curByte;         // byte within current sector (0-511)
    uint32_t filePos;
    uint8_t  buf[512];
    bool     loaded;
};

static GifReader gFile;

static bool gifLoadSector() {
    const uint32_t lba = fatClusterToLba(gFile.curCluster) + gFile.curSec;
    if (!sdReadSector(lba, gFile.buf)) return false;
    gFile.loaded = true;
    return true;
}

static bool gifAdvanceSector() {
    gFile.curByte = 0;
    gFile.loaded  = false;
    if (++gFile.curSec >= gSpc) {
        gFile.curSec = 0;
        gFile.curCluster = fatNextCluster(gFile.curCluster);
        if (gFile.curCluster >= 0x0FFFFFF8U) return false;
    }
    return true;
}

static bool gifRead(uint8_t* dst, uint32_t len) {
    while (len > 0) {
        if (!gFile.loaded && !gifLoadSector()) return false;
        const uint16_t avail = 512U - gFile.curByte;
        const uint16_t take  = (uint16_t)(len < avail ? len : avail);
        if (dst) { memcpy(dst, gFile.buf + gFile.curByte, take); dst += take; }
        gFile.curByte += take;
        gFile.filePos += take;
        len -= take;
        if (gFile.curByte >= 512 && !gifAdvanceSector()) return len == 0;
    }
    return true;
}

static bool gifSkip(uint32_t len) { return gifRead(nullptr, len); }

static bool gifSeek(uint32_t pos) {
    if (pos < gFile.filePos) {
        // Backward: restart from beginning of file
        gFile.curCluster = gFile.firstCluster;
        gFile.curSec     = 0;
        gFile.curByte    = 0;
        gFile.filePos    = 0;
        gFile.loaded     = false;
    }
    return (pos == gFile.filePos) || gifSkip(pos - gFile.filePos);
}

static uint8_t  gifRB()  { uint8_t b = 0; gifRead(&b, 1); return b; }
static uint16_t gifRU16() { uint8_t lo = gifRB(), hi = gifRB(); return (uint16_t)lo | ((uint16_t)hi << 8); }

static void gifSkipSubBlocks() {
    for (uint8_t len; (len = gifRB()) != 0;) gifSkip(len);
}

// ── LZW decoder ─────────────────────────────────────────────────────────────

static uint16_t lzwPrefix[4096];
static uint8_t  lzwValue[4096];
static uint8_t  lzwStack[4096];

static uint8_t  gSubBuf[256];
static uint8_t  gSubLen, gSubPos;
static uint32_t gBitBuf;
static int      gBitCnt;

static void lzwResetBits() { gSubLen = 0; gSubPos = 0; gBitBuf = 0; gBitCnt = 0; }

static int lzwGetCode(int codeSize) {
    while (gBitCnt < codeSize) {
        if (gSubPos >= gSubLen) {
            gSubLen = gifRB();
            if (gSubLen == 0) return -1;
            gifRead(gSubBuf, gSubLen);
            gSubPos = 0;
        }
        gBitBuf |= (uint32_t)gSubBuf[gSubPos++] << gBitCnt;
        gBitCnt += 8;
    }
    const int code = (int)(gBitBuf & ((1u << codeSize) - 1));
    gBitBuf >>= codeSize; gBitCnt -= codeSize;
    return code;
}

// Decode LZW-compressed image data into pixels[0..pixelCount-1] (palette indices).
static bool lzwDecode(uint8_t minCodeSize, uint8_t* pixels, uint32_t pixelCount) {
    const int clearCode = 1 << minCodeSize;
    const int eoiCode   = clearCode + 1;

    for (int i = 0; i < clearCode; i++) { lzwPrefix[i] = 0xFFFF; lzwValue[i] = (uint8_t)i; }

    lzwResetBits();
    int codeSize = minCodeSize + 1;
    int nextCode = eoiCode + 1;
    int maxCode  = 1 << codeSize;
    int prevCode = -1;
    uint8_t firstOfPrev = 0;
    uint32_t pixOut = 0;

    // First code (typically clearCode)
    int code = lzwGetCode(codeSize);
    if (code == clearCode) { codeSize = minCodeSize+1; nextCode = eoiCode+1; maxCode = 1<<codeSize; prevCode = -1; code = lzwGetCode(codeSize); }

    while (code != eoiCode && code != -1 && pixOut < pixelCount) {
        if (code == clearCode) {
            codeSize = minCodeSize + 1; nextCode = eoiCode + 1; maxCode = 1 << codeSize; prevCode = -1;
            code = lzwGetCode(codeSize); continue;
        }

        int sp = 0;
        const bool isNew = (code == nextCode);
        if (isNew) { lzwStack[sp++] = firstOfPrev; }  // first pixel of prev string added at bottom

        int c = isNew ? prevCode : code;
        if (c < 0 || c >= 4096) return false;
        while (lzwPrefix[c] != 0xFFFF) {
            if (sp >= 4096) return false;
            lzwStack[sp++] = lzwValue[c];
            c = lzwPrefix[c];
        }
        lzwStack[sp++] = lzwValue[c];

        const uint8_t first = lzwStack[sp - 1];  // first pixel of output string

        for (int i = sp - 1; i >= 0 && pixOut < pixelCount; i--)
            pixels[pixOut++] = lzwStack[i];

        if (nextCode < 4096 && prevCode >= 0) {
            lzwPrefix[nextCode] = (uint16_t)prevCode;
            lzwValue[nextCode]  = first;
            if (++nextCode == maxCode && codeSize < 12) { codeSize++; maxCode <<= 1; }
        }

        prevCode    = code;
        firstOfPrev = first;
        code = lzwGetCode(codeSize);
    }

    // Drain remaining sub-blocks
    if (gSubPos < gSubLen) gifSkip(gSubLen - gSubPos);
    gifSkipSubBlocks();

    return pixOut > 0;
}

// ── GIF state ────────────────────────────────────────────────────────────────

static uint8_t  gGlobalPalette[256 * 3];
static uint8_t  gLocalPalette[256 * 3];
static bool     gHasGlobalPalette;
static uint8_t  gGifBgIndex;
static uint32_t gFirstFramePos;   // file offset of first block after GCT

static uint8_t  gGifPixels[kLiveLedCount];  // 5120-byte palette-index buffer

bool gGifReady = false;

static uint32_t gNextGifFrameMs = 0;

// ── GIF block parser ─────────────────────────────────────────────────────────

// Parse and render one image frame from the current file position.
// Returns frame delay in centiseconds (0 = as-fast-as-possible).
// Returns -1 on error or end of file.
static int gifDecodeFrame(bool transparent, uint8_t transpIdx, uint8_t disposal) {
    // Image descriptor
    const uint16_t left   = gifRU16();
    const uint16_t top    = gifRU16();
    const uint16_t width  = gifRU16();
    const uint16_t height = gifRU16();
    const uint8_t  flags  = gifRB();

    const bool     hasLocal = (flags & 0x80) != 0;
    const bool     interlaced = (flags & 0x40) != 0;
    const uint16_t localCtSize = hasLocal ? (3U << ((flags & 0x07) + 1)) : 0;

    uint8_t* palette = gHasGlobalPalette ? gGlobalPalette : nullptr;
    if (hasLocal) {
        gifRead(gLocalPalette, localCtSize);
        palette = gLocalPalette;
    } else if (localCtSize) {
        gifSkip(localCtSize);
    }

    if (!palette) { gifSkipSubBlocks(); return 0; }  // no palette = skip frame

    // Handle disposal of previous frame
    if (disposal == 2) {
        // Restore to background color
        const uint8_t* bg = (gGifBgIndex < 256) ? palette + gGifBgIndex * 3 : nullptr;
        const uint8_t br = bg ? bg[0] : 0, bgg = bg ? bg[1] : 0, bb = bg ? bg[2] : 0;
        for (uint16_t row = top; row < top + height && row < kLiveMatrixHeight; row++) {
            for (uint16_t col = left; col < left + width && col < kLiveMatrixWidth; col++) {
                size_t off = ((size_t)row * kLiveMatrixWidth + col) * 3;
                gFrameBuffer[off] = br; gFrameBuffer[off+1] = bgg; gFrameBuffer[off+2] = bb;
            }
        }
    }

    // Decode LZW image data
    const uint8_t minCodeSize = gifRB();
    const uint32_t pixCount = (uint32_t)width * height;
    const uint32_t decodeCount = pixCount < kLiveLedCount ? pixCount : kLiveLedCount;

    if (interlaced) {
        // Minimal interlace support: decode all pixels then deinterlace
        // For simplicity we just decode and skip interlaced frames
        lzwResetBits();
        gifSkipSubBlocks();
        return 0;
    }

    if (!lzwDecode(minCodeSize, gGifPixels, decodeCount)) return -1;

    // Blit decoded pixels into gFrameBuffer (RGB conversion)
    for (uint16_t row = 0; row < height && (top + row) < kLiveMatrixHeight; row++) {
        for (uint16_t col = 0; col < width && (left + col) < kLiveMatrixWidth; col++) {
            const uint32_t pixIdx = (uint32_t)row * width + col;
            if (pixIdx >= decodeCount) break;
            const uint8_t palIdx = gGifPixels[pixIdx];
            if (transparent && palIdx == transpIdx) continue;
            const size_t  off    = ((size_t)(top + row) * kLiveMatrixWidth + (left + col)) * 3;
            gFrameBuffer[off    ] = palette[palIdx * 3    ];
            gFrameBuffer[off + 1] = palette[palIdx * 3 + 1];
            gFrameBuffer[off + 2] = palette[palIdx * 3 + 2];
        }
    }
    return 0;
}

// Advance through blocks until we find and render one image frame.
// Returns frame delay in centiseconds, or -1 at end-of-file / error.
static int gifNextFrame() {
    bool    transparent = false;
    uint8_t transpIdx   = 0;
    uint8_t disposal    = 0;
    int     delay_cs    = 4;  // default ~25fps

    while (true) {
        const uint8_t introducer = gifRB();

        if (introducer == 0x3B) return -1;  // GIF trailer

        if (introducer == 0x21) {           // Extension
            const uint8_t label = gifRB();
            if (label == 0xF9) {            // Graphic Control Extension
                gifRB();                    // block size (always 4)
                const uint8_t flags2 = gifRB();
                transparent = (flags2 & 0x01) != 0;
                disposal    = (flags2 >> 3) & 0x07;
                delay_cs    = (int)gifRU16();
                transpIdx   = gifRB();
                gifRB();                    // block terminator
            } else {
                gifSkipSubBlocks();
            }
            continue;
        }

        if (introducer == 0x2C) {           // Image descriptor
            gifDecodeFrame(transparent, transpIdx, disposal);
            return delay_cs;
        }

        // Unknown — give up
        Serial.print(F("GIF: unknown block 0x")); Serial.println(introducer, HEX);
        return -1;
    }
}

// ── Public API ───────────────────────────────────────────────────────────────

bool initGifPlayback(uint32_t firstCluster, uint32_t fileSize) {
    gGifReady = false;
    gFile.firstCluster = firstCluster;
    gFile.fileSize     = fileSize;
    gFile.curCluster   = firstCluster;
    gFile.curSec       = 0;
    gFile.curByte      = 0;
    gFile.filePos      = 0;
    gFile.loaded       = false;

    // Parse GIF header
    uint8_t hdr[6];
    if (!gifRead(hdr, 6)) return false;
    if (hdr[0] != 'G' || hdr[1] != 'I' || hdr[2] != 'F') {
        Serial.println(F("GIF: bad magic")); return false;
    }

    // Logical screen descriptor
    gifRU16(); gifRU16();               // width, height (we use display dimensions)
    const uint8_t flags    = gifRB();
    gGifBgIndex            = gifRB();
    gifRB();                            // pixel aspect ratio

    gHasGlobalPalette = (flags & 0x80) != 0;
    if (gHasGlobalPalette) {
        const uint16_t ctBytes = 3U << ((flags & 0x07) + 1);
        if (!gifRead(gGlobalPalette, ctBytes)) return false;
    }

    gFirstFramePos = gFile.filePos;

    Serial.print(F("GIF: ")); Serial.print(fileSize / 1024);
    Serial.println(F(" KB (looping)"));

    memset(gFrameBuffer, 0, kLiveFrameBytes);
    gNextGifFrameMs = millis();
    gGifReady = true;
    return true;
}

void updateGifPlayback() {
    if (!gGifReady) return;
    if ((int32_t)(millis() - gNextGifFrameMs) < 0) return;

    int delay_cs = gifNextFrame();

    if (delay_cs < 0) {
        // End of file — seek back and loop
        if (!gifSeek(gFirstFramePos)) { gGifReady = false; return; }
        memset(gFrameBuffer, 0, kLiveFrameBytes);
        delay_cs = gifNextFrame();
        if (delay_cs < 0) { gGifReady = false; return; }
    }

    applyCurrentLimit();
    renderFrameBuffer();

    const uint32_t delayMs = (delay_cs > 0) ? (uint32_t)delay_cs * 10U : 33U;
    gNextGifFrameMs = millis() + delayMs;
}
