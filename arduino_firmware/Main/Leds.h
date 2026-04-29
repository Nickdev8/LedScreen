#pragma once
#include <Arduino.h>

extern uint8_t* gFrameBuffer;
extern uint8_t* gFrameBufferBack;
extern bool     gIdleBlinkActive;

void initLeds();
void prepareLaneBuffers();
void startDisplay();
void waitForDisplay();
void renderFrameBuffer();
void applyCurrentLimit();

void resetIdleBlink();
void updateIdleBlink();
