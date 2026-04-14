#include <Adafruit_NeoPixel.h>
#include <SD.h>
#include <SPI.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace {

constexpr uint8_t LED_PIN = 28;
constexpr uint16_t LED_COUNT = 30;

constexpr uint8_t SD_SCK_PIN = 18;
constexpr uint8_t SD_MOSI_PIN = 19;
constexpr uint8_t SD_MISO_PIN = 20;
constexpr uint8_t SD_CS_PIN = 21;

constexpr size_t FRAME_SIZE = LED_COUNT * 3;
constexpr uint16_t DEFAULT_SD_FPS = 20;
constexpr uint32_t SD_RETRY_INTERVAL_MS = 3000;
constexpr uint32_t SERIAL_ACTIVITY_TIMEOUT_MS = 500;
constexpr uint8_t MAX_ANIMATIONS = 16;
constexpr size_t MAX_PATH_LENGTH = 64;

constexpr size_t LSA_HEADER_SIZE = 16;
constexpr uint8_t LSA_FLAG_LOOP = 0x01;
constexpr char LSA_MAGIC[4] = { 'L', 'S', 'A', '1' };

}

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

uint8_t frameBuffer[FRAME_SIZE];
size_t serialIndexPos = 0;
uint32_t lastSerialActivityMs = 0;

char animationPaths[MAX_ANIMATIONS][MAX_PATH_LENGTH];
uint8_t animationCount = 0;
uint8_t currentAnimationIndex = 0;

File animationFile;
uint32_t animationDataOffset = 0;
uint32_t currentFrameCount = 0;
uint32_t currentFrameIndex = 0;
uint16_t currentAnimationFps = 0;
bool currentAnimationLoops = false;

bool sdMounted = false;
uint32_t nextSdRetryMs = 0;
uint32_t nextFrameDueMs = 0;

void renderFrameBuffer() {

  for (uint16_t i = 0; i < LED_COUNT; i++) {
    strip.setPixelColor(
      i,
      frameBuffer[i * 3],
      frameBuffer[i * 3 + 1],
      frameBuffer[i * 3 + 2]
    );
  }

  strip.show();
}

bool endsWithIgnoreCase(const char* value, const char* suffix) {

  size_t valueLength = strlen(value);
  size_t suffixLength = strlen(suffix);

  if (suffixLength > valueLength) {
    return false;
  }

  for (size_t i = 0; i < suffixLength; i++) {
    char valueChar = value[valueLength - suffixLength + i];
    char suffixChar = suffix[i];

    if (tolower(static_cast<unsigned char>(valueChar)) != tolower(static_cast<unsigned char>(suffixChar))) {
      return false;
    }
  }

  return true;
}

bool isSupportedAnimationFile(const char* name) {
  return endsWithIgnoreCase(name, ".lsa")
    || endsWithIgnoreCase(name, ".rgb")
    || endsWithIgnoreCase(name, ".bin")
    || endsWithIgnoreCase(name, ".raw");
}

uint16_t readLittleEndian16(const uint8_t* bytes) {
  return static_cast<uint16_t>(bytes[0]) | (static_cast<uint16_t>(bytes[1]) << 8);
}

uint32_t readLittleEndian32(const uint8_t* bytes) {
  return static_cast<uint32_t>(bytes[0])
    | (static_cast<uint32_t>(bytes[1]) << 8)
    | (static_cast<uint32_t>(bytes[2]) << 16)
    | (static_cast<uint32_t>(bytes[3]) << 24);
}

void closeAnimationFile() {

  if (animationFile) {
    animationFile.close();
  }

  animationDataOffset = 0;
  currentFrameCount = 0;
  currentFrameIndex = 0;
  currentAnimationFps = 0;
  currentAnimationLoops = false;
}

bool addAnimationPath(const char* path) {

  if (animationCount >= MAX_ANIMATIONS) {
    Serial.println(F("Animation list full, skipping remaining files."));
    return false;
  }

  if (strlen(path) >= MAX_PATH_LENGTH) {
    Serial.print(F("Animation path too long, skipping: "));
    Serial.println(path);
    return false;
  }

  strncpy(animationPaths[animationCount], path, MAX_PATH_LENGTH - 1);
  animationPaths[animationCount][MAX_PATH_LENGTH - 1] = '\0';
  animationCount++;
  return true;
}

void sortAnimationPaths() {

  for (uint8_t i = 1; i < animationCount; i++) {
    char value[MAX_PATH_LENGTH];
    strncpy(value, animationPaths[i], MAX_PATH_LENGTH);

    int8_t j = static_cast<int8_t>(i) - 1;
    while (j >= 0 && strcmp(animationPaths[j], value) > 0) {
      strncpy(animationPaths[j + 1], animationPaths[j], MAX_PATH_LENGTH);
      j--;
    }

    strncpy(animationPaths[j + 1], value, MAX_PATH_LENGTH);
  }
}

void scanAnimationDirectory(const char* directoryPath) {

  File directory = SD.open(directoryPath);
  if (!directory || !directory.isDirectory()) {
    return;
  }

  while (true) {
    File entry = directory.openNextFile();
    if (!entry) {
      break;
    }

    if (entry.isDirectory()) {
      entry.close();
      continue;
    }

    const char* entryName = entry.name();
    if (!isSupportedAnimationFile(entryName)) {
      entry.close();
      continue;
    }

    char fullPath[MAX_PATH_LENGTH];
    if (strcmp(directoryPath, "/") == 0) {
      snprintf(fullPath, sizeof(fullPath), "/%s", entryName);
    } else {
      snprintf(fullPath, sizeof(fullPath), "%s/%s", directoryPath, entryName);
    }

    addAnimationPath(fullPath);
    entry.close();
  }

  directory.close();
}

bool loadAnimationList() {

  animationCount = 0;
  scanAnimationDirectory("/");
  scanAnimationDirectory("/animations");
  sortAnimationPaths();

  if (animationCount == 0) {
    Serial.println(F("No animation files found on SD card."));
    return false;
  }

  Serial.print(F("Found "));
  Serial.print(animationCount);
  Serial.println(F(" animation file(s)."));
  return true;
}

bool parseLsaHeader(File& file, uint16_t& fps, uint32_t& frameCount, bool& shouldLoop) {

  uint8_t header[LSA_HEADER_SIZE];
  if (file.read(header, sizeof(header)) != static_cast<int>(sizeof(header))) {
    return false;
  }

  if (memcmp(header, LSA_MAGIC, sizeof(LSA_MAGIC)) != 0) {
    return false;
  }

  uint16_t ledCount = readLittleEndian16(header + 4);
  fps = readLittleEndian16(header + 6);
  frameCount = readLittleEndian32(header + 8);
  shouldLoop = (header[12] & LSA_FLAG_LOOP) != 0;

  uint32_t fileSize = file.size();
  if (fileSize < LSA_HEADER_SIZE) {
    return false;
  }

  uint64_t expectedDataSize = static_cast<uint64_t>(frameCount) * FRAME_SIZE;
  uint32_t availableData = fileSize - LSA_HEADER_SIZE;

  if (ledCount != LED_COUNT || fps == 0 || frameCount == 0 || expectedDataSize > availableData) {
    return false;
  }

  return true;
}

bool openAnimationAt(uint8_t index) {

  if (index >= animationCount) {
    return false;
  }

  closeAnimationFile();

  animationFile = SD.open(animationPaths[index], FILE_READ);
  if (!animationFile) {
    Serial.print(F("Failed to open animation: "));
    Serial.println(animationPaths[index]);
    return false;
  }

  uint16_t fps = DEFAULT_SD_FPS;
  uint32_t frameCount = 0;
  bool shouldLoop = false;

  if (endsWithIgnoreCase(animationPaths[index], ".lsa")) {
    if (!parseLsaHeader(animationFile, fps, frameCount, shouldLoop)) {
      Serial.print(F("Invalid .lsa animation header: "));
      Serial.println(animationPaths[index]);
      closeAnimationFile();
      return false;
    }
    animationDataOffset = animationFile.position();
  } else {
    uint32_t dataSize = animationFile.size();
    if (dataSize == 0 || (dataSize % FRAME_SIZE) != 0) {
      Serial.print(F("Raw animation size must be a multiple of "));
      Serial.print(FRAME_SIZE);
      Serial.print(F(" bytes: "));
      Serial.println(animationPaths[index]);
      closeAnimationFile();
      return false;
    }
    frameCount = dataSize / FRAME_SIZE;
    animationDataOffset = 0;
  }

  currentAnimationIndex = index;
  currentFrameCount = frameCount;
  currentFrameIndex = 0;
  currentAnimationFps = fps;
  currentAnimationLoops = shouldLoop;
  nextFrameDueMs = millis();

  Serial.print(F("Playing "));
  Serial.print(animationPaths[index]);
  Serial.print(F(" at "));
  Serial.print(currentAnimationFps);
  Serial.print(F(" FPS, "));
  Serial.print(currentFrameCount);
  Serial.println(F(" frame(s)."));
  return true;
}

bool openNextAnimationFrom(uint8_t startIndex) {

  if (animationCount == 0) {
    return false;
  }

  for (uint8_t attempt = 0; attempt < animationCount; attempt++) {
    uint8_t index = (startIndex + attempt) % animationCount;
    if (openAnimationAt(index)) {
      return true;
    }
  }

  closeAnimationFile();
  return false;
}

void restartCurrentAnimation() {

  if (!animationFile) {
    return;
  }

  if (!animationFile.seek(animationDataOffset)) {
    Serial.print(F("Failed to rewind "));
    Serial.println(animationPaths[currentAnimationIndex]);
    advanceToNextAnimation();
    return;
  }

  currentFrameIndex = 0;
  nextFrameDueMs = millis();
}

void advanceToNextAnimation() {

  if (animationCount == 0) {
    closeAnimationFile();
    return;
  }

  uint8_t nextIndex = (currentAnimationIndex + 1) % animationCount;
  if (!openNextAnimationFrom(nextIndex)) {
    Serial.println(F("No playable animations available."));
  }
}

void updateAnimationPlayback() {

  if (!sdMounted || animationCount == 0) {
    return;
  }

  if (!animationFile && !openNextAnimationFrom(currentAnimationIndex)) {
    return;
  }

  uint32_t now = millis();
  if (static_cast<int32_t>(now - nextFrameDueMs) < 0) {
    return;
  }

  if (currentFrameIndex >= currentFrameCount) {
    if (currentAnimationLoops) {
      restartCurrentAnimation();
    } else {
      advanceToNextAnimation();
    }
    return;
  }

  if (animationFile.read(frameBuffer, FRAME_SIZE) != static_cast<int>(FRAME_SIZE)) {
    Serial.print(F("Short read while playing "));
    Serial.println(animationPaths[currentAnimationIndex]);
    advanceToNextAnimation();
    return;
  }

  renderFrameBuffer();
  currentFrameIndex++;

  uint32_t frameIntervalMs = 1000UL / currentAnimationFps;
  if (frameIntervalMs == 0) {
    frameIntervalMs = 1;
  }
  nextFrameDueMs = now + frameIntervalMs;
}

bool initializeSdCard() {

  closeAnimationFile();

  SPI.setRX(SD_MISO_PIN);
  SPI.setTX(SD_MOSI_PIN);
  SPI.setSCK(SD_SCK_PIN);
  SPI.setCS(SD_CS_PIN);

  pinMode(SD_CS_PIN, OUTPUT);
  digitalWrite(SD_CS_PIN, HIGH);

  if (!SD.begin(SD_CS_PIN)) {
    Serial.println(F("SD init failed."));
    sdMounted = false;
    animationCount = 0;
    return false;
  }

  sdMounted = true;
  loadAnimationList();
  currentAnimationIndex = 0;
  return true;
}

void serviceSerialStream() {

  while (Serial.available()) {
    int incoming = Serial.read();
    if (incoming < 0) {
      break;
    }

    lastSerialActivityMs = millis();

    if (serialIndexPos < FRAME_SIZE) {
      frameBuffer[serialIndexPos++] = static_cast<uint8_t>(incoming);
    }

    if (serialIndexPos >= FRAME_SIZE) {
      renderFrameBuffer();
      serialIndexPos = 0;
    }
  }

  if (serialIndexPos > 0 && (millis() - lastSerialActivityMs) > SERIAL_ACTIVITY_TIMEOUT_MS) {
    serialIndexPos = 0;
  }
}

bool serialPlaybackActive() {
  return (millis() - lastSerialActivityMs) <= SERIAL_ACTIVITY_TIMEOUT_MS;
}

void setup() {

  Serial.begin(500000);
  delay(1000);

  strip.begin();
  strip.clear();
  strip.show();

  strip.setPixelColor(0, 0, 40, 0);
  strip.show();
  delay(300);
  strip.clear();
  strip.show();

  initializeSdCard();
}

void loop() {

  serviceSerialStream();

  if (serialPlaybackActive()) {
    return;
  }

  if (!sdMounted && millis() >= nextSdRetryMs) {
    nextSdRetryMs = millis() + SD_RETRY_INTERVAL_MS;
    initializeSdCard();
  }

  updateAnimationPlayback();
}
