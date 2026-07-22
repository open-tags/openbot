#include <Arduino.h>

static const int US_MIN = 1000;
static const int US_MAX = 2000;
static const uint32_t FRAME_US = 20000;
static const uint32_t MOVE_MS = 500;

enum MotorId {
  FRONT_LEFT = 0,
  FRONT_RIGHT = 1,
  BACK_LEFT = 2,
  BACK_RIGHT = 3,
};

struct Motor {
  const char* shortName;
  const char* pinName;
  int pin;
  int zeroUs;
  int polarity;
};

static const Motor MOTOR[] = {
  { "FL", "D1", D1, 1523, -1 },
  { "FR", "D3", D3, 1543, +1 },
  { "BL", "D2", D2, 1523, -1 },
  { "BR", "D0", D0, 1503, +1 },
};

static const int MOTOR_COUNT = sizeof(MOTOR) / sizeof(MOTOR[0]);

int targetUs[MOTOR_COUNT] = {
  MOTOR[FRONT_LEFT].zeroUs,
  MOTOR[FRONT_RIGHT].zeroUs,
  MOTOR[BACK_LEFT].zeroUs,
  MOTOR[BACK_RIGHT].zeroUs,
};

int forwardUs[MOTOR_COUNT] = { 40, 20, 40, 30 };
int backwardUs[MOTOR_COUNT] = { 20, 40, 20, 38 };

uint32_t frameStartUs = 0;
uint32_t stopAtMs = 0;
bool pinHigh[MOTOR_COUNT] = { false, false, false, false };
bool moving = false;
char line[80];
int lineLen = 0;

static void printPulses(const char* label) {
  Serial.printf("%s | FL=%d FR=%d BL=%d BR=%d\n",
                label,
                targetUs[FRONT_LEFT],
                targetUs[FRONT_RIGHT],
                targetUs[BACK_LEFT],
                targetUs[BACK_RIGHT]);
}

static void printCalibration() {
  Serial.println();
  Serial.println("Current calibration magnitudes:");
  Serial.printf("  forward:  FL=%d FR=%d BL=%d BR=%d\n",
                forwardUs[FRONT_LEFT],
                forwardUs[FRONT_RIGHT],
                forwardUs[BACK_LEFT],
                forwardUs[BACK_RIGHT]);
  Serial.printf("  backward: FL=%d FR=%d BL=%d BR=%d\n",
                backwardUs[FRONT_LEFT],
                backwardUs[FRONT_RIGHT],
                backwardUs[BACK_LEFT],
                backwardUs[BACK_RIGHT]);
  Serial.println();
}

static void setZeros(const char* label) {
  moving = false;
  stopAtMs = 0;
  for (int i = 0; i < MOTOR_COUNT; i++) {
    targetUs[i] = MOTOR[i].zeroUs;
  }
  printPulses(label);
}

static void setForwardBack(const char* label, bool forward) {
  for (int i = 0; i < MOTOR_COUNT; i++) {
    int magnitude = forward ? forwardUs[i] : backwardUs[i];
    int direction = forward ? +1 : -1;
    targetUs[i] = constrain(MOTOR[i].zeroUs + direction * magnitude * MOTOR[i].polarity,
                            US_MIN,
                            US_MAX);
  }
  printPulses(label);

  moving = true;
  stopAtMs = millis() + MOVE_MS;
}

static void setWheelIntent(const char* label, const int intent[MOTOR_COUNT]) {
  for (int i = 0; i < MOTOR_COUNT; i++) {
    int direction = intent[i];
    int magnitude = direction >= 0 ? forwardUs[i] : backwardUs[i];
    targetUs[i] = constrain(MOTOR[i].zeroUs + direction * magnitude * MOTOR[i].polarity,
                            US_MIN,
                            US_MAX);
  }
  printPulses(label);

  moving = true;
  stopAtMs = millis() + MOVE_MS;
}

static void servicePulses() {
  uint32_t nowUs = micros();

  if (static_cast<uint32_t>(nowUs - frameStartUs) >= FRAME_US) {
    frameStartUs = nowUs;
    for (int i = 0; i < MOTOR_COUNT; i++) {
      digitalWrite(MOTOR[i].pin, HIGH);
      pinHigh[i] = true;
    }
  }

  uint32_t elapsedUs = static_cast<uint32_t>(nowUs - frameStartUs);
  for (int i = 0; i < MOTOR_COUNT; i++) {
    if (pinHigh[i] && elapsedUs >= static_cast<uint32_t>(targetUs[i])) {
      digitalWrite(MOTOR[i].pin, LOW);
      pinHigh[i] = false;
    }
  }
}

static bool parseIntToken(char** cursor, int* out) {
  while (**cursor == ' ' || **cursor == '\t') (*cursor)++;
  if (**cursor == '\0') return false;

  char* end = nullptr;
  long value = strtol(*cursor, &end, 10);
  if (end == *cursor) return false;

  *cursor = end;
  *out = static_cast<int>(value);
  return true;
}

static void setAll(int values[MOTOR_COUNT], int us, const char* label) {
  us = constrain(us, 0, 400);
  for (int i = 0; i < MOTOR_COUNT; i++) {
    values[i] = us;
  }
  Serial.printf("%s all = %d\n", label, us);
  printCalibration();
}

static void setOne(int values[MOTOR_COUNT], int motorNumber, int us, const char* label) {
  if (motorNumber < 1 || motorNumber > MOTOR_COUNT) {
    Serial.println("Invalid motor. Use 1=FL 2=FR 3=BL 4=BR.");
    return;
  }

  int motorId = motorNumber - 1;
  values[motorId] = constrain(us, 0, 400);
  Serial.printf("%s %s/%s = %d\n",
                label,
                MOTOR[motorId].shortName,
                MOTOR[motorId].pinName,
                values[motorId]);
  printCalibration();
}

static void printHelp() {
  Serial.println();
  Serial.println("Manual open-loop mecanum calibration");
  Serial.println("Software-generated ESC pulses only. No IMU, Servo, LEDC, or auto-correction.");
  Serial.println("Each drive command lasts 500ms, then auto-stops.");
  Serial.println("Zeros: FL/D1=1523 FR/D3=1543 BL/D2=1523 BR/D0=1503");
  Serial.println("Motor ids: 1=FL/D1 2=FR/D3 3=BL/D2 4=BR/D0");
  Serial.println();
  Serial.println("Drive:");
  Serial.println("  w = forward");
  Serial.println("  s = backward");
  Serial.println("  a = left strafe");
  Serial.println("  d = right strafe");
  Serial.println("  x or space = stop");
  Serial.println();
  Serial.println("Tune magnitudes:");
  Serial.println("  f <motor> <us> = set one forward magnitude");
  Serial.println("  b <motor> <us> = set one backward magnitude");
  Serial.println("  fa <us> = set all forward magnitudes");
  Serial.println("  ba <us> = set all backward magnitudes");
  Serial.println("  p = print calibration values");
  Serial.println("  ? or h = help");
  printCalibration();
}

static void handleLine(char* text) {
  while (*text == ' ' || *text == '\t') text++;
  if (*text == '\0') return;

  if (!strcmp(text, "w") || !strcmp(text, "W")) {
    setForwardBack("forward", true);
    return;
  }
  if (!strcmp(text, "s") || !strcmp(text, "S")) {
    setForwardBack("backward", false);
    return;
  }
  if (!strcmp(text, "a") || !strcmp(text, "A")) {
    static const int intent[MOTOR_COUNT] = { -1, +1, +1, -1 };
    setWheelIntent("left", intent);
    return;
  }
  if (!strcmp(text, "d") || !strcmp(text, "D")) {
    static const int intent[MOTOR_COUNT] = { +1, -1, -1, +1 };
    setWheelIntent("right", intent);
    return;
  }
  if (!strcmp(text, "x") || !strcmp(text, "X")) {
    setZeros("manual zero");
    return;
  }
  if (!strcmp(text, "p") || !strcmp(text, "P")) {
    printCalibration();
    return;
  }
  if (!strcmp(text, "?") || !strcmp(text, "h") || !strcmp(text, "H")) {
    printHelp();
    return;
  }

  if (!strncmp(text, "fa", 2) || !strncmp(text, "FA", 2)) {
    char* cursor = text + 2;
    int us = 0;
    if (parseIntToken(&cursor, &us)) setAll(forwardUs, us, "forward");
    else Serial.println("Usage: fa <us>");
    return;
  }

  if (!strncmp(text, "ba", 2) || !strncmp(text, "BA", 2)) {
    char* cursor = text + 2;
    int us = 0;
    if (parseIntToken(&cursor, &us)) setAll(backwardUs, us, "backward");
    else Serial.println("Usage: ba <us>");
    return;
  }

  if (text[0] == 'f' || text[0] == 'F' || text[0] == 'b' || text[0] == 'B') {
    bool forward = text[0] == 'f' || text[0] == 'F';
    char* cursor = text + 1;
    int motorNumber = 0;
    int us = 0;
    if (parseIntToken(&cursor, &motorNumber) && parseIntToken(&cursor, &us)) {
      setOne(forward ? forwardUs : backwardUs,
             motorNumber,
             us,
             forward ? "forward" : "backward");
    } else {
      Serial.println("Usage: f <motor> <us> or b <motor> <us>");
    }
    return;
  }

  Serial.println("Unknown command. Type ? for help.");
}

void setup() {
  Serial.begin(115200);
  delay(300);

  for (int i = 0; i < MOTOR_COUNT; i++) {
    pinMode(MOTOR[i].pin, OUTPUT);
    digitalWrite(MOTOR[i].pin, LOW);
  }

  frameStartUs = micros() - FRAME_US;
  setZeros("boot zero");
  printHelp();
}

void loop() {
  servicePulses();

  if (moving && millis() >= stopAtMs) {
    setZeros("auto zero");
  }

  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      line[lineLen] = '\0';
      handleLine(line);
      lineLen = 0;
      continue;
    }
    if (c == ' ' && lineLen == 0) {
      setZeros("manual zero");
      continue;
    }
    if (lineLen < static_cast<int>(sizeof(line)) - 1) {
      line[lineLen++] = c;
    }
  }
}
