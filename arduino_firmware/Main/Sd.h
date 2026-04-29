#pragma once
#include <Arduino.h>

bool sdInit();
bool sdReadSector(uint32_t lba, uint8_t* buf);

extern uint8_t gSpc;  // sectors per cluster — shared with GIF reader
uint32_t fatClusterToLba(uint32_t cluster);
uint32_t fatNextCluster(uint32_t cluster);
bool fatMount(uint32_t& outFirstCluster, uint32_t& outFileSize);
// Scan root directory for first file matching a 3-char 8.3 extension (e.g. "GIF").
bool fatFindFile(const char ext[3], uint32_t& outCluster, uint32_t& outFileSize);

bool initSdAndAnimation();
void updateAnimationPlayback();

extern bool     gSdReady;
extern bool     gAnimReady;
extern uint32_t gNextSdRetryMs;
