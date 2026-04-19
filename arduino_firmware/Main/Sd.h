#pragma once
#include <Arduino.h>

bool sdInit();
bool sdReadSector(uint32_t lba, uint8_t* buf);

uint32_t fatClusterToLba(uint32_t cluster);
uint32_t fatNextCluster(uint32_t cluster);
// This loader always picks the first root-level .LSA entry.
bool fatMount(uint32_t& outFirstCluster, uint32_t& outFileSize);

bool initSdAndAnimation();
void updateAnimationPlayback();

extern bool     gSdReady;
extern bool     gAnimReady;
extern uint32_t gNextSdRetryMs;
