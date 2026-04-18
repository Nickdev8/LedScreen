#pragma once
#include <Arduino.h>

// Call every loop iteration — drains Serial and renders complete frames.
void serviceUsbStream();

// Returns true while USB data has been received within the last 500 ms.
// When true, SD playback is suppressed.
bool usbStreamActive();
