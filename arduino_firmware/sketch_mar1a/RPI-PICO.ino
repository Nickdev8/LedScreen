#include <Adafruit_NeoPixel.h>

#define LED_PIN 2
#define LED_COUNT 30

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

uint8_t frameBuffer[LED_COUNT * 3];
uint32_t indexPos = 0;

void setup() {
  Serial.begin(500000);
  while (!Serial) { }
  strip.begin();
  strip.show();
}

void loop() {
  while (Serial.available()) {
    frameBuffer[indexPos++] = Serial.read();

    if (indexPos >= sizeof(frameBuffer)) {
      for (int i = 0; i < LED_COUNT; i++) {
        strip.setPixelColor(
          i,
          frameBuffer[i * 3],
          frameBuffer[i * 3 + 1],
          frameBuffer[i * 3 + 2]
        );
      }
      strip.show();
      indexPos = 0;
    }
  }
}