#pragma once
#include <Arduino.h>

bool sdInit();
bool sdReadSector(uint32_t lba, uint8_t* buf);

extern uint8_t gSpc;
uint32_t fatClusterToLba(uint32_t cluster);
uint32_t fatNextCluster(uint32_t cluster);
bool fatMount();

bool initSdPlaylist();
void updateAnimationPlayback();

extern bool     gSdReady;
extern bool     gAnimReady;
extern uint32_t gNextSdRetryMs;
