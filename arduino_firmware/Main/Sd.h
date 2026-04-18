#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// SD hardware layer
// ---------------------------------------------------------------------------
bool sdInit();
bool sdReadSector(uint32_t lba, uint8_t* buf);

// ---------------------------------------------------------------------------
// FAT32 reader
// ---------------------------------------------------------------------------
uint32_t fatClusterToLba(uint32_t cluster);
uint32_t fatNextCluster(uint32_t cluster);
// Mounts the filesystem and finds the first .LSA file in the root directory.
bool fatMount(uint32_t& outFirstCluster, uint32_t& outFileSize);

// ---------------------------------------------------------------------------
// Animation playback
// ---------------------------------------------------------------------------
// Call once at boot (and on retry). Returns true when animation is ready.
bool initSdAndAnimation();
// Call every loop iteration when SD and animation are ready.
void updateAnimationPlayback();

extern bool     gSdReady;
extern bool     gAnimReady;
extern uint32_t gNextSdRetryMs;
