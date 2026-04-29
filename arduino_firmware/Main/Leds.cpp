#include <Arduino.h>
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "Config.h"
#include "Leds.h"

static_assert(kLiveLaneCount == 8, "This driver requires exactly 8 lanes (4 per PIO block)");

// Keep the program pre-assembled so the firmware does not depend on pioasm at build time.
static const uint16_t kWs2812Insns[] = {
    0x6221,
    0x1123,
    0x1400,
    0xa442,
};
static const pio_program_t kWs2812Prog = {
    .instructions = kWs2812Insns,
    .length       = 4,
    .origin       = -1,
};

constexpr uint16_t kLedsPerLane = 3U * kLiveLedsPerPanel;

static uint     gProgOff[2];
static uint     gSm[kLiveLaneCount];
static PIO      gPioDev[kLiveLaneCount];
static int      gDmaCh[kLiveLaneCount];
// Pack GRB into the upper 24 bits so the PIO shift register emits it MSB-first.
static uint32_t gLaneBuf[kLiveLaneCount][kLedsPerLane];

static uint8_t gFrameBuf0[kLiveFrameBytes];
static uint8_t gFrameBuf1[kLiveFrameBytes];
uint8_t* gFrameBuffer     = gFrameBuf0;
uint8_t* gFrameBufferBack = gFrameBuf1;
bool     gIdleBlinkActive = false;

static uint32_t gLastShowUs = 0;

struct PhysicalPixel { uint8_t lane; uint16_t index; };

static PhysicalPixel mapLogicalToPhysical(uint16_t idx) {
    uint16_t x, y;
    if (kLiveInputColumnMajor) { x = idx / kLiveMatrixHeight; y = idx % kLiveMatrixHeight; }
    else                       { x = idx % kLiveMatrixWidth;  y = idx / kLiveMatrixWidth; }
    if (kMatrixFlipX) x = kLiveMatrixWidth  - 1U - x;
    if (kMatrixFlipY) y = kLiveMatrixHeight - 1U - y;

    const uint16_t panelsWide = kLiveMatrixWidth / kLivePanelWidth;
    const bool     isTopRow   = (y / kLivePanelHeight) == 0;

    // Row 180°: flip x for both panel selection and content, and flip py.
    const bool     rowRot180  = isTopRow ? kTopRowRot180 : kBotRowRot180;
    const uint16_t xr         = rowRot180 ? kLiveMatrixWidth - 1U - x : x;

    // Panel order reversal: flip x for panel selection only, content (px) is unaffected.
    const uint16_t xp = (isTopRow ? kTopRowReverseX : kBotRowReverseX)
                       ? kLiveMatrixWidth - 1U - xr : xr;
    const uint16_t panelIndex = (y / kLivePanelHeight) * panelsWide + (xp / kLivePanelWidth);

    uint8_t pil = static_cast<uint8_t>(panelIndex);
    uint8_t lane = 0;
    while (lane < kLiveLaneCount && pil >= kLivePanelsPerLane[lane])
        pil -= kLivePanelsPerLane[lane++];
    if (lane >= kLiveLaneCount) return {0, 0};

    uint16_t px = xr % kLivePanelWidth;
    uint16_t py = y  % kLivePanelHeight;
    if (rowRot180) py = kLivePanelHeight - 1U - py;

    if      (kPanelRotationQuarterTurnsCCW == 1) { uint16_t t = px; px = py; py = kLivePanelWidth  - 1U - t; }
    else if (kPanelRotationQuarterTurnsCCW == 2) { px = kLivePanelWidth  - 1U - px; py = kLivePanelHeight - 1U - py; }
    else if (kPanelRotationQuarterTurnsCCW == 3) { uint16_t t = py; py = px; px = kLivePanelHeight - 1U - t; }

    // Per-panel content corrections (applied before serpentine).
    if (isTopRow ? kTopPanelRot180 : kBotPanelRot180) {
        px = kLivePanelWidth  - 1U - px;
        py = kLivePanelHeight - 1U - py;
    }
    if (isTopRow ? kTopPanelFlipX : kBotPanelFlipX)
        px = kLivePanelWidth  - 1U - px;
    if (isTopRow ? kTopPanelFlipY : kBotPanelFlipY)
        py = kLivePanelHeight - 1U - py;

    if (kMatrixSerpentine) {
        const bool odd = (py & 1U) != 0;
        if (kMatrixReverseOddRows ? odd : !odd) px = kLivePanelWidth - 1U - px;
    }

    return { lane, static_cast<uint16_t>(pil * kLiveLedsPerPanel + py * kLivePanelWidth + px) };
}

void startDisplay() {
    const uint32_t elapsed = (uint32_t)(micros() - gLastShowUs);
    if (elapsed < 50) delayMicroseconds(50 - elapsed);

    pio_set_sm_mask_enabled(pio0, 0xF, false);
    pio_set_sm_mask_enabled(pio1, 0xF, false);
    for (uint8_t l = 0; l < kLiveLaneCount; l++) {
        pio_sm_clear_fifos(gPioDev[l], gSm[l]);
        pio_sm_exec(gPioDev[l], gSm[l], pio_encode_jmp(gProgOff[l < 4 ? 0 : 1]));
    }

    uint32_t dmaMask = 0;
    for (uint8_t l = 0; l < kLiveLaneCount; l++) {
        dma_channel_set_read_addr(gDmaCh[l], gLaneBuf[l], false);
        dma_channel_set_trans_count(gDmaCh[l], kLedsPerLane, false);
        dmaMask |= (1u << gDmaCh[l]);
    }
    dma_start_channel_mask(dmaMask);
    pio_enable_sm_mask_in_sync(pio0, 0xF);
    pio_enable_sm_mask_in_sync(pio1, 0xF);
    // Returns immediately — DMA + PIO run autonomously.
}

void waitForDisplay() {
    for (uint8_t l = 0; l < kLiveLaneCount; l++)
        dma_channel_wait_for_finish_blocking(gDmaCh[l]);
    gLastShowUs = micros();
}

static void showLaneBufs() {
    startDisplay();
    waitForDisplay();
}

void applyCurrentLimit() {
    // The frame buffer is row-major. With kMatrixFlipY=true, logical rows 0-15 map to
    // the physical bottom row (bottom PSU) and rows 16-31 map to the physical top row (top PSU).
    constexpr size_t   kHalf     = kLiveFrameBytes / 2;
    constexpr uint32_t kMaxScale = (kGeneralMaxBrightnessPercent * 255U) / 100U;

    uint32_t sumBot = 0, sumTop = 0;
    for (size_t i = 0;     i < kHalf;           i++) sumBot += gFrameBuffer[i];
    for (size_t i = kHalf; i < kLiveFrameBytes; i++) sumTop += gFrameBuffer[i];

    // Maximum scale (0-255) that keeps a half-buffer within its PSU budget.
    // At scale S: current ≈ rowSum × kLedMaPerChannel × S / (255 × 255) ≤ kPsuCurrentMa
    // → S ≤ kPsuCurrentMa × 255² / (rowSum × kLedMaPerChannel)
    auto rowScale = [](uint32_t rowSum) -> uint32_t {
        if (rowSum == 0) return kMaxScale;
        const uint32_t s = (kPsuCurrentMa * 255UL * 255UL)
                         / (rowSum * (uint32_t)kLedMaPerChannel);
        return s < kMaxScale ? s : kMaxScale;
    };

    const uint32_t scaleBot = rowScale(sumBot);
    const uint32_t scaleTop = rowScale(sumTop);
    const uint32_t scale    = scaleBot < scaleTop ? scaleBot : scaleTop;

    if (scale >= 255U) return;
    for (size_t i = 0; i < kLiveFrameBytes; i++)
        gFrameBuffer[i] = static_cast<uint8_t>((gFrameBuffer[i] * scale) >> 8);
}

void prepareLaneBuffers() {
    for (uint16_t i = 0; i < kLiveLedCount; i++) {
        const PhysicalPixel p   = mapLogicalToPhysical(i);
        const size_t        off = static_cast<size_t>(i) * 3U;
        gLaneBuf[p.lane][p.index] =
            ((uint32_t)gFrameBuffer[off + 1] << 24) |
            ((uint32_t)gFrameBuffer[off    ] << 16) |
            ((uint32_t)gFrameBuffer[off + 2] <<  8);
    }
}

void renderFrameBuffer() {
    prepareLaneBuffers();
    showLaneBufs();
}

void initLeds() {
    if (!pio_can_add_program(pio0, &kWs2812Prog) || !pio_can_add_program(pio1, &kWs2812Prog)) {
        Serial.println(F("PIO: no instruction space")); return;
    }
    gProgOff[0] = pio_add_program(pio0, &kWs2812Prog);
    gProgOff[1] = pio_add_program(pio1, &kWs2812Prog);

    pio_claim_sm_mask(pio0, 0xF);
    pio_claim_sm_mask(pio1, 0xF);

    for (uint8_t l = 0; l < kLiveLaneCount; l++) {
        gPioDev[l] = (l < 4) ? pio0 : pio1;
        gSm[l]     = l & 3u;
        const uint8_t pin    = kLiveLanePins[l];
        const uint   offset  = gProgOff[l < 4 ? 0 : 1];

        pio_gpio_init(gPioDev[l], pin);
        pio_sm_set_consecutive_pindirs(gPioDev[l], gSm[l], pin, 1, true);

        pio_sm_config c = pio_get_default_sm_config();
        sm_config_set_sideset(&c, 1, false, false);
        sm_config_set_sideset_pins(&c, pin);
        sm_config_set_out_shift(&c, false, true, 24);
        sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
        sm_config_set_wrap(&c, offset, offset + 3);
        sm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) / (10.0f * 800000.0f));
        pio_sm_init(gPioDev[l], gSm[l], offset, &c);
    }

    for (uint8_t l = 0; l < kLiveLaneCount; l++) {
        gDmaCh[l] = dma_claim_unused_channel(false);
        if (gDmaCh[l] < 0) {
            Serial.print(F("DMA: no channel for lane ")); Serial.println(l); return;
        }
        dma_channel_config dc = dma_channel_get_default_config(gDmaCh[l]);
        channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
        channel_config_set_read_increment(&dc, true);
        channel_config_set_write_increment(&dc, false);
        channel_config_set_dreq(&dc, pio_get_dreq(gPioDev[l], gSm[l], true));
        dma_channel_configure(gDmaCh[l], &dc,
            &gPioDev[l]->txf[gSm[l]], gLaneBuf[l], kLedsPerLane, false);
    }

    Serial.println(F("PIO+DMA NeoPixel init OK"));

    for (size_t i = 0; i < kLiveFrameBytes; i += 3)
        { gFrameBuffer[i] = 40; gFrameBuffer[i+1] = 0; gFrameBuffer[i+2] = 0; }
    prepareLaneBuffers();
    showLaneBufs();
    delay(1000);
    memset(gFrameBuffer, 0, kLiveFrameBytes);
    prepareLaneBuffers();
    showLaneBufs();
}

static uint32_t gNextBlinkMs  = 0;
static bool     gBlinkOn      = false;
constexpr uint32_t kBlinkIntervalMs = 500;

static void setPanelLeds(uint8_t r, uint8_t g, uint8_t b) {
    for (size_t i = 0; i < kLiveFrameBytes; i += 3)
        { gFrameBuffer[i] = r; gFrameBuffer[i+1] = g; gFrameBuffer[i+2] = b; }
    prepareLaneBuffers();
    showLaneBufs();
}

void resetIdleBlink() {
    memset(gFrameBuffer, 0, kLiveFrameBytes);
    prepareLaneBuffers();
    showLaneBufs();
    gBlinkOn = false; gNextBlinkMs = millis(); gIdleBlinkActive = true;
}

void updateIdleBlink() {
    const uint32_t now = millis();
    if ((int32_t)(now - gNextBlinkMs) < 0) return;
    gBlinkOn = !gBlinkOn;
    setPanelLeds(gBlinkOn ? 40 : 0, 0, 0);
    gNextBlinkMs = now + kBlinkIntervalMs;
}
