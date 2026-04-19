#pragma once
#include <Arduino.h>

void serviceUsbStream();

// Active USB traffic suppresses SD playback.
bool usbStreamActive();
