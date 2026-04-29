#pragma once
#include <Arduino.h>

extern bool gGifReady;

// Call after SD is initialised. firstCluster/fileSize come from fatFindFile("GIF",...).
bool initGifPlayback(uint32_t firstCluster, uint32_t fileSize);

// Call from main loop every iteration; non-blocking between frames.
void updateGifPlayback();
