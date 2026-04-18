#pragma once
#include <Arduino.h>

extern uint8_t gFrameBuffer[];

void initLeds();
void renderFrameBuffer();
void applyCurrentLimit();

void resetIdleBlink();
void updateIdleBlink();
extern bool gIdleBlinkActive;
