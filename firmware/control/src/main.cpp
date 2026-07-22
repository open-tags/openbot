#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BNO08x.h>

static const int US_MIN = 1000;
static const int US_MAX = 2000;
static const uint32_t FRAME_US = 20000;
static const uint32_t COMMAND_WATCHDOG_MS = 1500;

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

bool pinHigh[MOTOR_COUNT] = { false, false, false, false };
bool moving = false;
uint32_t frameStartUs = 0;
uint32_t stopAtMs = 0;
uint32_t lastCommandMs = 0;

char line[128];
int lineLen = 0;

Adafruit_BNO08x bno08x(-1);
sh2_SensorValue_t sensorValue;
bool imuReady = false;

static void servicePulses();

static int clampPct(int value) {
  return constrain(value, -100, 100);
}

static void writeZeros() {
  for (int i = 0; i < MOTOR_COUNT; i++) {
    targetUs[i] = MOTOR[i].zeroUs;
  }
}

static void stopMotors(const char* reason) {
  moving = false;
  stopAtMs = 0;
  writeZeros();
  Serial.printf("OK STOP reason=%s FL=%d FR=%d BL=%d BR=%d\n",
                reason,
                targetUs[FRONT_LEFT],
                targetUs[FRONT_RIGHT],
                targetUs[BACK_LEFT],
                targetUs[BACK_RIGHT]);
}

static int pulseFromSignedPct(int motorId, int signedPct) {
  signedPct = clampPct(signedPct);
  if (signedPct == 0) return MOTOR[motorId].zeroUs;

  int magnitude = signedPct > 0 ? forwardUs[motorId] : backwardUs[motorId];
  int scaled = (magnitude * abs(signedPct)) / 100;
  return constrain(MOTOR[motorId].zeroUs + (signedPct > 0 ? +1 : -1) * scaled * MOTOR[motorId].polarity,
                   US_MIN,
                   US_MAX);
}

static void setWheelPercents(const int wheelPct[MOTOR_COUNT], uint32_t durationMs, const char* label) {
  for (int i = 0; i < MOTOR_COUNT; i++) {
    targetUs[i] = pulseFromSignedPct(i, wheelPct[i]);
  }

  moving = true;
  uint32_t cappedMs = constrain(durationMs, 1UL, COMMAND_WATCHDOG_MS);
  stopAtMs = millis() + cappedMs;
  lastCommandMs = millis();

  Serial.printf("OK %s ms=%lu pct=%d,%d,%d,%d pulse=%d,%d,%d,%d\n",
                label,
                static_cast<unsigned long>(cappedMs),
                wheelPct[FRONT_LEFT],
                wheelPct[FRONT_RIGHT],
                wheelPct[BACK_LEFT],
                wheelPct[BACK_RIGHT],
                targetUs[FRONT_LEFT],
                targetUs[FRONT_RIGHT],
                targetUs[BACK_LEFT],
                targetUs[BACK_RIGHT]);
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

static float wrapDeg(float angleDeg) {
  while (angleDeg > 180.0f) angleDeg -= 360.0f;
  while (angleDeg < -180.0f) angleDeg += 360.0f;
  return angleDeg;
}

static float yawFromQuaternion(float i, float j, float k, float real) {
  float sinyCosp = 2.0f * (real * k + i * j);
  float cosyCosp = 1.0f - 2.0f * (j * j + k * k);
  return wrapDeg(atan2f(sinyCosp, cosyCosp) * 180.0f / PI);
}

static bool startImu() {
  Wire.begin();
  Wire.setClock(100000);

  if (bno08x.begin_I2C(0x4B) || bno08x.begin_I2C(0x4A)) {
    bno08x.enableReport(SH2_GAME_ROTATION_VECTOR, 20000);
    return true;
  }
  return false;
}

static bool readImu(float* qi, float* qj, float* qk, float* qr, float* yawDeg, uint32_t timeoutMs) {
  if (!imuReady) return false;
  if (moving) return false;

  uint32_t startMs = millis();
  while (millis() - startMs < timeoutMs) {
    servicePulses();

    if (bno08x.wasReset()) {
      bno08x.enableReport(SH2_GAME_ROTATION_VECTOR, 20000);
    }

    while (bno08x.getSensorEvent(&sensorValue)) {
      if (sensorValue.sensorId != SH2_GAME_ROTATION_VECTOR) continue;

      auto &q = sensorValue.un.gameRotationVector;
      *qi = q.i;
      *qj = q.j;
      *qk = q.k;
      *qr = q.real;
      *yawDeg = yawFromQuaternion(q.i, q.j, q.k, q.real);
      return true;
    }
  }
  return false;
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

static void printHelp() {
  Serial.println("OK HELP commands=PING,HELP,STOP,STATUS,CAL?,IMU?,YAW?,SETF,SETB,WHEEL,DRIVE");
  Serial.println("OK HELP SETF <motor 1..4> <us>; SETB <motor 1..4> <us>");
  Serial.println("OK HELP WHEEL <fl -100..100> <fr> <bl> <br> <ms>");
  Serial.println("OK HELP DRIVE <x -100..100> <y -100..100> <turn -100..100> <ms>");
}

static void printStatus() {
  Serial.printf("OK STATUS moving=%d imu=%d pulse=%d,%d,%d,%d zero=%d,%d,%d,%d\n",
                moving ? 1 : 0,
                imuReady ? 1 : 0,
                targetUs[FRONT_LEFT],
                targetUs[FRONT_RIGHT],
                targetUs[BACK_LEFT],
                targetUs[BACK_RIGHT],
                MOTOR[FRONT_LEFT].zeroUs,
                MOTOR[FRONT_RIGHT].zeroUs,
                MOTOR[BACK_LEFT].zeroUs,
                MOTOR[BACK_RIGHT].zeroUs);
}

static void printCalibration() {
  Serial.printf("OK CAL forward=%d,%d,%d,%d backward=%d,%d,%d,%d\n",
                forwardUs[FRONT_LEFT],
                forwardUs[FRONT_RIGHT],
                forwardUs[BACK_LEFT],
                forwardUs[BACK_RIGHT],
                backwardUs[FRONT_LEFT],
                backwardUs[FRONT_RIGHT],
                backwardUs[BACK_LEFT],
                backwardUs[BACK_RIGHT]);
}

static void setCal(int values[MOTOR_COUNT], int motorNumber, int us, const char* label) {
  if (motorNumber < 1 || motorNumber > MOTOR_COUNT) {
    Serial.println("ERR motor must be 1..4");
    return;
  }

  int motorId = motorNumber - 1;
  values[motorId] = constrain(us, 0, 400);
  Serial.printf("OK %s motor=%d value=%d\n", label, motorNumber, values[motorId]);
}

static void handleLine(char* text) {
  while (*text == ' ' || *text == '\t') text++;
  if (*text == '\0') return;

  if (!strcasecmp(text, "PING")) {
    Serial.println("OK PONG");
    return;
  }
  if (!strcasecmp(text, "HELP")) {
    printHelp();
    return;
  }
  if (!strcasecmp(text, "STOP")) {
    stopMotors("command");
    return;
  }
  if (!strcasecmp(text, "STATUS")) {
    printStatus();
    return;
  }
  if (!strcasecmp(text, "CAL?")) {
    printCalibration();
    return;
  }
  if (!strcasecmp(text, "IMU?") || !strcasecmp(text, "YAW?")) {
    if (moving) {
      Serial.println("ERR IMU busy motors_moving");
      return;
    }
    float qi = 0.0f, qj = 0.0f, qk = 0.0f, qr = 1.0f, yawDeg = 0.0f;
    if (!readImu(&qi, &qj, &qk, &qr, &yawDeg, 200)) {
      Serial.println("ERR IMU unavailable");
      return;
    }
    if (!strcasecmp(text, "YAW?")) {
      Serial.printf("OK YAW deg=%.2f\n", yawDeg);
    } else {
      Serial.printf("OK IMU qi=%.5f qj=%.5f qk=%.5f qr=%.5f yaw=%.2f\n",
                    qi,
                    qj,
                    qk,
                    qr,
                    yawDeg);
    }
    return;
  }

  if (!strncasecmp(text, "SETF", 4) || !strncasecmp(text, "SETB", 4)) {
    bool forward = !strncasecmp(text, "SETF", 4);
    char* cursor = text + 4;
    int motorNumber = 0, us = 0;
    if (!parseIntToken(&cursor, &motorNumber) || !parseIntToken(&cursor, &us)) {
      Serial.println("ERR usage SETF|SETB <motor 1..4> <us>");
      return;
    }
    setCal(forward ? forwardUs : backwardUs, motorNumber, us, forward ? "SETF" : "SETB");
    return;
  }

  if (!strncasecmp(text, "WHEEL", 5)) {
    char* cursor = text + 5;
    int wheelPct[MOTOR_COUNT] = { 0, 0, 0, 0 };
    int ms = 0;
    if (!parseIntToken(&cursor, &wheelPct[FRONT_LEFT]) ||
        !parseIntToken(&cursor, &wheelPct[FRONT_RIGHT]) ||
        !parseIntToken(&cursor, &wheelPct[BACK_LEFT]) ||
        !parseIntToken(&cursor, &wheelPct[BACK_RIGHT]) ||
        !parseIntToken(&cursor, &ms)) {
      Serial.println("ERR usage WHEEL <fl> <fr> <bl> <br> <ms>");
      return;
    }
    for (int i = 0; i < MOTOR_COUNT; i++) wheelPct[i] = clampPct(wheelPct[i]);
    setWheelPercents(wheelPct, static_cast<uint32_t>(max(ms, 1)), "WHEEL");
    return;
  }

  if (!strncasecmp(text, "DRIVE", 5)) {
    char* cursor = text + 5;
    int x = 0, y = 0, turn = 0, ms = 0;
    if (!parseIntToken(&cursor, &x) ||
        !parseIntToken(&cursor, &y) ||
        !parseIntToken(&cursor, &turn) ||
        !parseIntToken(&cursor, &ms)) {
      Serial.println("ERR usage DRIVE <x> <y> <turn> <ms>");
      return;
    }

    x = clampPct(x);
    y = clampPct(y);
    turn = clampPct(turn);
    int wheelPct[MOTOR_COUNT] = {
      y + x + turn,
      y - x - turn,
      y - x + turn,
      y + x - turn,
    };

    int largest = 100;
    for (int i = 0; i < MOTOR_COUNT; i++) {
      largest = max(largest, abs(wheelPct[i]));
    }
    for (int i = 0; i < MOTOR_COUNT; i++) {
      wheelPct[i] = wheelPct[i] * 100 / largest;
    }

    setWheelPercents(wheelPct, static_cast<uint32_t>(max(ms, 1)), "DRIVE");
    return;
  }

  Serial.println("ERR unknown_command");
}

void setup() {
  Serial.begin(115200);
  delay(300);

  for (int i = 0; i < MOTOR_COUNT; i++) {
    pinMode(MOTOR[i].pin, OUTPUT);
    digitalWrite(MOTOR[i].pin, LOW);
  }

  frameStartUs = micros() - FRAME_US;
  writeZeros();
  imuReady = startImu();
  lastCommandMs = millis();

  Serial.printf("OK BOOT openbot imu=%d\n", imuReady ? 1 : 0);
  printHelp();
}

void loop() {
  servicePulses();

  if (moving && millis() >= stopAtMs) {
    stopMotors("timeout");
  }

  if (moving && millis() - lastCommandMs > COMMAND_WATCHDOG_MS) {
    stopMotors("watchdog");
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
    if (lineLen < static_cast<int>(sizeof(line)) - 1) {
      line[lineLen++] = c;
    }
  }
}
