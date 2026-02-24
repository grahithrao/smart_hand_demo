#include <Arduino.h>
#include <Wire.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>

#if __has_include(<LiquidCrystal_I2C.h>)
#include <LiquidCrystal_I2C.h>
#define HAS_LCD 1
#else
#define HAS_LCD 0
#endif

/*
  Smart Hand Gesture to Speech (Paper-aligned implementation)
  - 5x flex sensors (thumb/index/middle/ring/pinky)
  - MPU6050 orientation (pitch/roll/yaw)
  - Rule-based gesture recognition
  - Serial + HC-05 Bluetooth output
  - Optional I2C LCD output
  - EEPROM calibration persistence
*/

namespace Config {
const uint8_t FLEX_PIN_COUNT = 5;

/*
  Full mode (recommended): boards with A6 (Nano, some compatibles)
    - A0/A1/A2/A3/A6 for flex sensors
    - A4/A5 reserved for I2C MPU6050/LCD

  UNO-compat mode fallback (no A6):
    - A0/A1/A2/A3/A4 for flex sensors
    - IMU disabled by default to avoid A4/A5 I2C pin conflict
*/
#if defined(A6)
const uint8_t FLEX_PINS[FLEX_PIN_COUNT] = {A0, A1, A2, A3, A6};
const bool IMU_ENABLED_DEFAULT = true;
#else
const uint8_t FLEX_PINS[FLEX_PIN_COUNT] = {A0, A1, A2, A3, A4};
const bool IMU_ENABLED_DEFAULT = false;
#endif

const char *FINGER_NAMES[FLEX_PIN_COUNT] = {"THUMB", "INDEX", "MIDDLE", "RING", "PINKY"};

const uint8_t MPU_ADDR = 0x68;

// HC-05 on D10 (RX), D11 (TX) from Arduino point of view.
const uint8_t BT_RX = 10;
const uint8_t BT_TX = 11;
const long BT_BAUD = 9600;
const long SERIAL_BAUD = 115200;

const uint32_t SAMPLE_INTERVAL_MS = 40;        // ~25 Hz
const uint8_t STABLE_FRAMES_REQUIRED = 4;
const uint32_t REPEAT_LOCKOUT_MS = 1100;

const uint8_t CAL_VERSION = 3;
const int16_t CAL_MAGIC = 0x5348; // "SH"

// Percentage thresholds (0..100 bend)
const int BENT_THRESHOLD = 65;
const int OPEN_THRESHOLD = 30;
const int HALF_MIN = 35;
const int HALF_MAX = 70;

// IMU constraints for dynamic words
const float HELP_MAX_ABS_PITCH = 25.0f;
const float HELP_MAX_ABS_ROLL = 25.0f;
const float FOOD_MIN_ABS_PITCH = 7.0f;
} // namespace Config

struct FlexCalibration {
  int16_t straightRaw; // finger straight
  int16_t bentRaw;     // finger fully bent
};

struct PersistedCalibration {
  int16_t magic;
  uint8_t version;
  FlexCalibration fingers[Config::FLEX_PIN_COUNT];
  uint8_t checksum;
};

struct ImuState {
  float ax = 0;
  float ay = 0;
  float az = 0;
  float gx = 0;
  float gy = 0;
  float gz = 0;
  float pitch = 0;
  float roll = 0;
  float yaw = 0;
};

struct GestureEvent {
  const char *gestureCode = "NONE";
  const char *text = "";
  float confidence = 0.0f;
};

SoftwareSerial btSerial(Config::BT_RX, Config::BT_TX);

#if HAS_LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);
#endif

FlexCalibration gCalibration[Config::FLEX_PIN_COUNT] = {
    {550, 760}, // THUMB
    {540, 780}, // INDEX
    {530, 790}, // MIDDLE
    {530, 800}, // RING
    {520, 790}  // PINKY
};

int gFlexRaw[Config::FLEX_PIN_COUNT] = {0};
int gFlexPercent[Config::FLEX_PIN_COUNT] = {0};
ImuState gImu;

String gPendingSerialCommand;
const char *gLastCandidate = "NONE";
uint8_t gStableFrames = 0;
const char *gLastEmitted = "NONE";
uint32_t gLastEmitMs = 0;
uint32_t gLastSampleMs = 0;
uint32_t gLastImuMs = 0;
bool gImuEnabled = Config::IMU_ENABLED_DEFAULT;

static uint8_t computeChecksum(const PersistedCalibration &blob) {
  const uint8_t *p = reinterpret_cast<const uint8_t *>(&blob);
  uint8_t acc = 0;
  for (size_t i = 0; i < sizeof(PersistedCalibration) - 1; i++) {
    acc ^= p[i];
  }
  return acc;
}

static void saveCalibration() {
  PersistedCalibration blob;
  blob.magic = Config::CAL_MAGIC;
  blob.version = Config::CAL_VERSION;
  for (uint8_t i = 0; i < Config::FLEX_PIN_COUNT; i++) {
    blob.fingers[i] = gCalibration[i];
  }
  blob.checksum = computeChecksum(blob);

  EEPROM.put(0, blob);
  Serial.println(F("CALIBRATION_SAVED"));
}

static bool loadCalibration() {
  PersistedCalibration blob;
  EEPROM.get(0, blob);
  if (blob.magic != Config::CAL_MAGIC || blob.version != Config::CAL_VERSION) {
    return false;
  }
  if (computeChecksum(blob) != blob.checksum) {
    return false;
  }
  for (uint8_t i = 0; i < Config::FLEX_PIN_COUNT; i++) {
    gCalibration[i] = blob.fingers[i];
  }
  return true;
}

static void printCalibration() {
  Serial.println(F("CALIBRATION_VALUES"));
  for (uint8_t i = 0; i < Config::FLEX_PIN_COUNT; i++) {
    Serial.print(Config::FINGER_NAMES[i]);
    Serial.print(F(" straight="));
    Serial.print(gCalibration[i].straightRaw);
    Serial.print(F(" bent="));
    Serial.println(gCalibration[i].bentRaw);
  }
}

static void mpuWrite(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(Config::MPU_ADDR);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

static void initMpu() {
  if (!gImuEnabled) {
    return;
  }
  // Wake up MPU6050 (sleep bit = 0)
  mpuWrite(0x6B, 0x00);
  // +/-2g accel (default)
  mpuWrite(0x1C, 0x00);
  // +/-250 deg/s gyro (default)
  mpuWrite(0x1B, 0x00);
  gLastImuMs = millis();
}

static bool readMpuRaw(int16_t &ax, int16_t &ay, int16_t &az, int16_t &gx, int16_t &gy, int16_t &gz) {
  Wire.beginTransmission(Config::MPU_ADDR);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  Wire.requestFrom(static_cast<int>(Config::MPU_ADDR), 14);
  if (Wire.available() < 14) {
    return false;
  }

  ax = (Wire.read() << 8) | Wire.read();
  ay = (Wire.read() << 8) | Wire.read();
  az = (Wire.read() << 8) | Wire.read();
  Wire.read(); // temp high
  Wire.read(); // temp low
  gx = (Wire.read() << 8) | Wire.read();
  gy = (Wire.read() << 8) | Wire.read();
  gz = (Wire.read() << 8) | Wire.read();
  return true;
}

static void updateImu() {
  if (!gImuEnabled) {
    gImu.pitch = 0.0f;
    gImu.roll = 0.0f;
    gImu.yaw = 0.0f;
    return;
  }

  int16_t axRaw = 0, ayRaw = 0, azRaw = 0, gxRaw = 0, gyRaw = 0, gzRaw = 0;
  if (!readMpuRaw(axRaw, ayRaw, azRaw, gxRaw, gyRaw, gzRaw)) {
    return;
  }

  gImu.ax = static_cast<float>(axRaw) / 16384.0f;
  gImu.ay = static_cast<float>(ayRaw) / 16384.0f;
  gImu.az = static_cast<float>(azRaw) / 16384.0f;
  gImu.gx = static_cast<float>(gxRaw) / 131.0f;
  gImu.gy = static_cast<float>(gyRaw) / 131.0f;
  gImu.gz = static_cast<float>(gzRaw) / 131.0f;

  // Paper-aligned orientation estimates from accelerometer and gyro.
  gImu.pitch = atan2f(gImu.ax, sqrtf((gImu.ay * gImu.ay) + (gImu.az * gImu.az))) * 180.0f / PI;
  gImu.roll = atan2f(gImu.ay, sqrtf((gImu.ax * gImu.ax) + (gImu.az * gImu.az))) * 180.0f / PI;

  uint32_t now = millis();
  float dt = (now - gLastImuMs) / 1000.0f;
  gLastImuMs = now;
  if (dt > 0.0f && dt < 0.2f) {
    gImu.yaw += gImu.gz * dt;
  }
}

static int rawToPercent(uint8_t idx, int raw) {
  const FlexCalibration &c = gCalibration[idx];
  int span = c.bentRaw - c.straightRaw;
  if (span == 0) {
    return 0;
  }
  int pct = ((raw - c.straightRaw) * 100) / span;
  if (pct < 0) {
    pct = 0;
  }
  if (pct > 100) {
    pct = 100;
  }
  return pct;
}

static void readFlexSensors() {
  for (uint8_t i = 0; i < Config::FLEX_PIN_COUNT; i++) {
    int raw = analogRead(Config::FLEX_PINS[i]);
    gFlexRaw[i] = raw;
    gFlexPercent[i] = rawToPercent(i, raw);
  }
}

static bool isBent(uint8_t idx) { return gFlexPercent[idx] >= Config::BENT_THRESHOLD; }
static bool isOpen(uint8_t idx) { return gFlexPercent[idx] <= Config::OPEN_THRESHOLD; }
static bool isHalf(uint8_t idx) {
  return gFlexPercent[idx] >= Config::HALF_MIN && gFlexPercent[idx] <= Config::HALF_MAX;
}

static GestureEvent classifyGesture() {
  // finger order: 0 thumb, 1 index, 2 middle, 3 ring, 4 pinky
  bool thumbBent = isBent(0);
  bool indexBent = isBent(1);
  bool middleBent = isBent(2);
  bool ringBent = isBent(3);
  bool pinkyBent = isBent(4);

  bool thumbOpen = isOpen(0);
  bool indexOpen = isOpen(1);
  bool middleOpen = isOpen(2);
  bool ringOpen = isOpen(3);
  bool pinkyOpen = isOpen(4);

  // Paper-focused gestures
  if (indexBent && middleBent && ringBent && pinkyBent && thumbOpen) {
    return {"A", "A", 0.95f};
  }

  if (thumbBent && indexOpen && middleOpen && ringOpen && pinkyOpen) {
    return {"B", "B", 0.92f};
  }

  // Open hand with relatively stable orientation -> HELP
  if (thumbOpen && indexOpen && middleOpen && ringOpen && pinkyOpen &&
      (!gImuEnabled || (fabs(gImu.pitch) <= Config::HELP_MAX_ABS_PITCH &&
                        fabs(gImu.roll) <= Config::HELP_MAX_ABS_ROLL))) {
    return {"HELP", "Help", 0.90f};
  }

  // Multiple bend + orientation change -> I NEED FOOD
  if (thumbBent && indexBent && middleBent && ringOpen &&
      (!gImuEnabled || fabs(gImu.pitch) >= Config::FOOD_MIN_ABS_PITCH)) {
    return {"I_NEED_FOOD", "I need food", 0.88f};
  }

  // Optional extension hooks
  if (isHalf(1) && isHalf(2) && isHalf(3) && isHalf(4) && isHalf(0)) {
    return {"C", "C", 0.80f};
  }
  if (indexOpen && middleBent && ringBent && pinkyBent && isHalf(0)) {
    return {"D", "D", 0.78f};
  }
  if (thumbBent && indexBent && middleBent && ringBent && pinkyBent) {
    return {"E", "E", 0.85f};
  }
  if (indexOpen && thumbOpen && middleBent && ringBent && pinkyBent && fabs(gImu.roll) > 20.0f) {
    return {"G", "G", 0.75f};
  }

  return {"NONE", "", 0.0f};
}

static void sendMessage(const GestureEvent &event) {
  // JSON-like compact format (easy to parse in Python/mobile app).
  char buffer[256];
  snprintf(buffer, sizeof(buffer),
           "{\"gesture\":\"%s\",\"text\":\"%s\",\"confidence\":%.2f,"
           "\"pitch\":%.2f,\"roll\":%.2f,\"yaw\":%.2f,"
           "\"flex\":[%d,%d,%d,%d,%d]}",
           event.gestureCode, event.text, event.confidence, gImu.pitch, gImu.roll, gImu.yaw,
           gFlexPercent[0], gFlexPercent[1], gFlexPercent[2], gFlexPercent[3], gFlexPercent[4]);
  Serial.println(buffer);
  btSerial.println(buffer);
}

static void updateDisplay(const GestureEvent &event) {
#if HAS_LCD
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Gesture:");
  lcd.setCursor(8, 0);
  lcd.print(event.gestureCode);
  lcd.setCursor(0, 1);
  lcd.print(event.text);
#else
  (void)event;
#endif
}

static void emitIfStable(const GestureEvent &candidate) {
  const char *code = candidate.gestureCode;
  if (strcmp(code, gLastCandidate) == 0) {
    if (gStableFrames < 255) {
      gStableFrames++;
    }
  } else {
    gLastCandidate = code;
    gStableFrames = 1;
  }

  if (strcmp(code, "NONE") == 0) {
    return;
  }

  uint32_t now = millis();
  bool lockoutExpired = (now - gLastEmitMs) >= Config::REPEAT_LOCKOUT_MS;
  bool differentFromLast = strcmp(code, gLastEmitted) != 0;

  if (gStableFrames >= Config::STABLE_FRAMES_REQUIRED && (differentFromLast || lockoutExpired)) {
    sendMessage(candidate);
    updateDisplay(candidate);
    gLastEmitted = code;
    gLastEmitMs = now;
  }
}

static int parseFingerIndex(const String &name) {
  for (uint8_t i = 0; i < Config::FLEX_PIN_COUNT; i++) {
    if (name.equalsIgnoreCase(Config::FINGER_NAMES[i])) {
      return i;
    }
  }
  return -1;
}

static void printHelp() {
  Serial.println(F("COMMANDS:"));
  Serial.println(F(" SHOW_CAL"));
  Serial.println(F(" SET_CAL <FINGER> <STRAIGHT_RAW> <BENT_RAW>"));
  Serial.println(F(" SAVE_CAL"));
  Serial.println(F(" LOAD_CAL"));
  Serial.println(F(" READINGS"));
  Serial.println(F(" IMU_ON"));
  Serial.println(F(" IMU_OFF"));
  Serial.println(F(" HELP"));
}

static void printReadings() {
  Serial.print(F("RAW: "));
  for (uint8_t i = 0; i < Config::FLEX_PIN_COUNT; i++) {
    Serial.print(gFlexRaw[i]);
    if (i + 1 < Config::FLEX_PIN_COUNT) {
      Serial.print(',');
    }
  }
  Serial.println();

  Serial.print(F("PCT: "));
  for (uint8_t i = 0; i < Config::FLEX_PIN_COUNT; i++) {
    Serial.print(gFlexPercent[i]);
    if (i + 1 < Config::FLEX_PIN_COUNT) {
      Serial.print(',');
    }
  }
  Serial.println();

  Serial.print(F("IMU pitch/roll/yaw: "));
  Serial.print(gImu.pitch, 2);
  Serial.print('/');
  Serial.print(gImu.roll, 2);
  Serial.print('/');
  Serial.println(gImu.yaw, 2);
}

static void handleCommand(const String &line) {
  if (line.length() == 0) {
    return;
  }

  if (line.equalsIgnoreCase("SHOW_CAL")) {
    printCalibration();
    return;
  }
  if (line.equalsIgnoreCase("SAVE_CAL")) {
    saveCalibration();
    return;
  }
  if (line.equalsIgnoreCase("LOAD_CAL")) {
    if (loadCalibration()) {
      Serial.println(F("CALIBRATION_LOADED"));
    } else {
      Serial.println(F("CALIBRATION_LOAD_FAILED"));
    }
    return;
  }
  if (line.equalsIgnoreCase("READINGS")) {
    printReadings();
    return;
  }
  if (line.equalsIgnoreCase("IMU_ON")) {
    gImuEnabled = true;
    initMpu();
    Serial.println(F("IMU_ENABLED"));
    return;
  }
  if (line.equalsIgnoreCase("IMU_OFF")) {
    gImuEnabled = false;
    Serial.println(F("IMU_DISABLED"));
    return;
  }
  if (line.equalsIgnoreCase("HELP")) {
    printHelp();
    return;
  }

  // SET_CAL <FINGER> <STRAIGHT> <BENT>
  if (line.startsWith("SET_CAL ")) {
    int p1 = line.indexOf(' ');
    int p2 = line.indexOf(' ', p1 + 1);
    int p3 = line.indexOf(' ', p2 + 1);
    if (p2 < 0 || p3 < 0) {
      Serial.println(F("ERR_BAD_SET_CAL_FORMAT"));
      return;
    }

    String finger = line.substring(p1 + 1, p2);
    int idx = parseFingerIndex(finger);
    if (idx < 0) {
      Serial.println(F("ERR_UNKNOWN_FINGER"));
      return;
    }

    int straight = line.substring(p2 + 1, p3).toInt();
    int bent = line.substring(p3 + 1).toInt();
    if (straight <= 0 || bent <= 0 || bent == straight) {
      Serial.println(F("ERR_INVALID_CAL_VALUES"));
      return;
    }
    gCalibration[idx].straightRaw = static_cast<int16_t>(straight);
    gCalibration[idx].bentRaw = static_cast<int16_t>(bent);
    Serial.print(F("CAL_UPDATED "));
    Serial.println(Config::FINGER_NAMES[idx]);
    return;
  }

  Serial.println(F("UNKNOWN_COMMAND"));
  printHelp();
}

static void processSerialInput() {
  while (Serial.available() > 0) {
    char c = static_cast<char>(Serial.read());
    if (c == '\n' || c == '\r') {
      if (gPendingSerialCommand.length() > 0) {
        handleCommand(gPendingSerialCommand);
        gPendingSerialCommand = "";
      }
    } else {
      gPendingSerialCommand += c;
      if (gPendingSerialCommand.length() > 100) {
        gPendingSerialCommand = "";
      }
    }
  }
}

void setup() {
  Serial.begin(Config::SERIAL_BAUD);
  btSerial.begin(Config::BT_BAUD);
  Wire.begin();

#if HAS_LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Smart Hand Boot");
#endif

  initMpu();
  Serial.println(gImuEnabled ? F("IMU_DEFAULT=ON") : F("IMU_DEFAULT=OFF"));
  bool loaded = loadCalibration();
  Serial.println(loaded ? F("CALIBRATION_LOADED") : F("DEFAULT_CALIBRATION_ACTIVE"));
  printHelp();
}

void loop() {
  processSerialInput();

  uint32_t now = millis();
  if (now - gLastSampleMs < Config::SAMPLE_INTERVAL_MS) {
    return;
  }
  gLastSampleMs = now;

  readFlexSensors();
  updateImu();
  GestureEvent candidate = classifyGesture();
  emitIfStable(candidate);
}
