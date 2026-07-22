// Simple mecanum bot firmware for XIAO ESP32-S3.
//
// Motor numbers:
//   0 = front left   D2
//   1 = front right  D3
//   2 = back left    D1
//   3 = back right   D0
//
// Serial monitor: 115200 baud. End typed commands with Enter.
// First test: wheels off the floor, low power, common ground.

#include <Arduino.h>
#include <ESP32Servo.h>
#include <Preferences.h>
#include <string.h>
#include <strings.h>

static const int MOTOR_COUNT = 4;
static const int PULSE_MIN_US = 1000;
static const int PULSE_MAX_US = 2000;
static const int CENTER_MIN_US = 1300;
static const int CENTER_MAX_US = 1700;
static const int GAIN_MIN_PCT = 20;
static const int GAIN_MAX_PCT = 200;
static const int POWER_MIN_US = 5;
static const int POWER_MAX_US = 300;
static const int TEST_POWER_PCT = 50;
static const uint32_t DEFAULT_MOVE_MS = 700;
static const uint32_t TEST_MS = 650;
static const uint32_t NEUTRAL_HOLD_MS = 350;
static const uint32_t NEUTRAL_REFRESH_MS = 20;
static const char* PREF_NAMESPACE = "mecanum_d2_fl";

enum MotorId {
  FRONT_LEFT = 0,
  FRONT_RIGHT = 1,
  BACK_LEFT = 2,
  BACK_RIGHT = 3,
};

struct MotorConfig {
  const char* name;
  const char* pinName;
  int pin;
  int centerUs;
  int direction;
  int gainPct;
};

static const MotorConfig DEFAULT_MOTOR[MOTOR_COUNT] = {
  { "FL", "D2", D2, 1500, -1, 100 },
  { "FR", "D3", D3, 1500, +1, 100 },
  { "BL", "D1", D1, 1540, -1, 100 },
  { "BR", "D0", D0, 1540, +1, 100 },
};

Servo esc[MOTOR_COUNT];
MotorConfig cfg[MOTOR_COUNT];
int currentUs[MOTOR_COUNT] = { 1500, 1500, 1500, 1500 };
bool motorAttached[MOTOR_COUNT] = { false, false, false, false };
int powerUs = 45;
uint32_t moveMs = DEFAULT_MOVE_MS;
uint32_t moveEndMs = 0;
uint32_t neutralUntilMs = 0;
uint32_t lastNeutralWriteMs = 0;
uint32_t lastBlinkMs = 0;
bool moving = false;
bool ledOn = false;
bool prefsOpen = false;
bool testAbort = false;
char line[96];
int lineLen = 0;

Preferences prefs;

static int clampInt(int value, int low, int high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

static bool isMotor(int id) {
  return id >= 0 && id < MOTOR_COUNT;
}

static void printPrompt() {
  Serial.print("> ");
}

static void attachMotor(int id) {
  if (motorAttached[id]) return;
  esc[id].setPeriodHertz(50);
  esc[id].attach(cfg[id].pin, PULSE_MIN_US, PULSE_MAX_US);
  motorAttached[id] = true;
}

static void writePulse(int id, int pulseUs) {
  attachMotor(id);
  currentUs[id] = clampInt(pulseUs, PULSE_MIN_US, PULSE_MAX_US);
  esc[id].writeMicroseconds(currentUs[id]);
}

static void writeAllCenters() {
  for (int i = 0; i < MOTOR_COUNT; i++) {
    writePulse(i, cfg[i].centerUs);
  }
}

static void centerAll(const char* label) {
  moving = false;
  moveEndMs = 0;
  writeAllCenters();
  neutralUntilMs = millis() + NEUTRAL_HOLD_MS;
  lastNeutralWriteMs = 0;
  Serial.printf("%s centered: FL=%dus FR=%dus BL=%dus BR=%dus\n",
                label,
                currentUs[FRONT_LEFT],
                currentUs[FRONT_RIGHT],
                currentUs[BACK_LEFT],
                currentUs[BACK_RIGHT]);
}

static int commandToPulse(int id, int commandPct) {
  commandPct = clampInt(commandPct, -100, 100);
  long delta = static_cast<long>(commandPct) * powerUs * cfg[id].gainPct;
  delta = delta / 10000;
  return cfg[id].centerUs + cfg[id].direction * static_cast<int>(delta);
}

static void drive(int xPct, int yPct, int turnPct, uint32_t durationMs, const char* label) {
  xPct = clampInt(xPct, -100, 100);       // + = strafe right
  yPct = clampInt(yPct, -100, 100);       // + = forward
  turnPct = clampInt(turnPct, -100, 100); // + = clockwise

  int wheel[MOTOR_COUNT] = {
    yPct + xPct + turnPct, // FL
    yPct - xPct - turnPct, // FR
    yPct - xPct + turnPct, // BL
    yPct + xPct - turnPct, // BR
  };

  int biggest = 100;
  for (int i = 0; i < MOTOR_COUNT; i++) {
    int mag = abs(wheel[i]);
    if (mag > biggest) biggest = mag;
  }
  for (int i = 0; i < MOTOR_COUNT; i++) {
    wheel[i] = wheel[i] * 100 / biggest;
    writePulse(i, commandToPulse(i, wheel[i]));
  }

  moving = true;
  neutralUntilMs = 0;
  moveEndMs = millis() + durationMs;
  Serial.printf("%s x=%d y=%d turn=%d for %lums | FL=%dus FR=%dus BL=%dus BR=%dus\n",
                label,
                xPct,
                yPct,
                turnPct,
                static_cast<unsigned long>(durationMs),
                currentUs[FRONT_LEFT],
                currentUs[FRONT_RIGHT],
                currentUs[BACK_LEFT],
                currentUs[BACK_RIGHT]);
}

static void stopNow() {
  testAbort = true;
  centerAll("STOP");
}

static void copyDefaults() {
  for (int i = 0; i < MOTOR_COUNT; i++) {
    cfg[i] = DEFAULT_MOTOR[i];
  }
  powerUs = 45;
  moveMs = DEFAULT_MOVE_MS;
}

static void loadSettings() {
  copyDefaults();
  if (!prefsOpen) {
    prefs.begin(PREF_NAMESPACE, false);
    prefsOpen = true;
  }

  for (int i = 0; i < MOTOR_COUNT; i++) {
    char key[8];
    snprintf(key, sizeof(key), "c%d", i);
    cfg[i].centerUs = clampInt(prefs.getInt(key, cfg[i].centerUs), CENTER_MIN_US, CENTER_MAX_US);

    snprintf(key, sizeof(key), "d%d", i);
    cfg[i].direction = prefs.getInt(key, cfg[i].direction) < 0 ? -1 : +1;

    snprintf(key, sizeof(key), "g%d", i);
    cfg[i].gainPct = clampInt(prefs.getInt(key, cfg[i].gainPct), GAIN_MIN_PCT, GAIN_MAX_PCT);
  }

  powerUs = clampInt(prefs.getInt("power", powerUs), POWER_MIN_US, POWER_MAX_US);
  moveMs = static_cast<uint32_t>(clampInt(prefs.getInt("move_ms", moveMs), 50, 10000));
}

static void saveSettings() {
  for (int i = 0; i < MOTOR_COUNT; i++) {
    char key[8];
    snprintf(key, sizeof(key), "c%d", i);
    prefs.putInt(key, cfg[i].centerUs);
    snprintf(key, sizeof(key), "d%d", i);
    prefs.putInt(key, cfg[i].direction);
    snprintf(key, sizeof(key), "g%d", i);
    prefs.putInt(key, cfg[i].gainPct);
  }
  prefs.putInt("power", powerUs);
  prefs.putInt("move_ms", moveMs);
  Serial.println("Saved tuning to flash.");
}

static void clearSavedSettings() {
  prefs.clear();
  copyDefaults();
  centerAll("DEFAULTS");
  Serial.println("Cleared saved tuning. Use save if these defaults are what you want.");
}

static void printConfig() {
  Serial.println();
  Serial.println("Motor tuning:");
  for (int i = 0; i < MOTOR_COUNT; i++) {
    Serial.printf("  %d %-2s pin=%s center=%dus dir=%+d gain=%d%% now=%dus\n",
                  i,
                  cfg[i].name,
                  cfg[i].pinName,
                  cfg[i].centerUs,
                  cfg[i].direction,
                  cfg[i].gainPct,
                  currentUs[i]);
  }
  Serial.printf("power=%dus  time=%lums\n",
                powerUs,
                static_cast<unsigned long>(moveMs));
  Serial.println();
}

static void printHelp() {
  Serial.println();
  Serial.println("=== simple mecanum bot ===");
  Serial.println("Type a command, then Enter. Space at an empty prompt stops immediately.");
  Serial.println();
  Serial.println("Drive:");
  Serial.println("  w/s/a/d        forward/back/left/right");
  Serial.println("  q/e or -/+     rotate ccw/cw");
  Serial.println("  drive x y t    custom mix, each -100..100. Example: drive 30 70 0");
  Serial.println("  x              stop and center");
  Serial.println();
  Serial.println("Tune live:");
  Serial.println("  Motor ids      0=FL/D2  1=FR/D3  2=BL/D1  3=BR/D0");
  Serial.println("  c m us         set center. Example: c 1 1495");
  Serial.println("  trim m delta   add to center. Example: trim 0 -20");
  Serial.println("  inv m          flip motor direction");
  Serial.println("  gain m pct     scale one motor, 20..200. Example: gain 2 90");
  Serial.println("  power us       drive strength around center. Start with 30..60");
  Serial.println("  time ms        duration for keyboard moves");
  Serial.println();
  Serial.println("Debug:");
  Serial.println("  test m         spin one motor forward/center/back");
  Serial.println("  u              test all motors one at a time; x/space aborts");
  Serial.println("  raw m us       direct write one motor");
  Serial.println("  off m          detach one motor signal and hold pin LOW");
  Serial.println("  p              print tuning");
  Serial.println("  save/load      persist or reload tuning");
  Serial.println("  defaults       restore built-in tuning in RAM");
  Serial.println("  ?              this help");
  printConfig();
}

static void setCenter(int id, int centerUs) {
  if (!isMotor(id)) {
    Serial.println("Motor must be 0..3.");
    return;
  }
  cfg[id].centerUs = clampInt(centerUs, CENTER_MIN_US, CENTER_MAX_US);
  centerAll("CENTER");
  printConfig();
}

static void trimCenter(int id, int deltaUs) {
  if (!isMotor(id)) {
    Serial.println("Motor must be 0..3.");
    return;
  }
  cfg[id].centerUs = clampInt(cfg[id].centerUs + deltaUs, CENTER_MIN_US, CENTER_MAX_US);
  centerAll("CENTER");
  Serial.printf("%s center %+dus -> %dus\n",
                cfg[id].name,
                deltaUs,
                cfg[id].centerUs);
}

static void invertMotor(int id) {
  if (!isMotor(id)) {
    Serial.println("Motor must be 0..3.");
    return;
  }
  cfg[id].direction = -cfg[id].direction;
  centerAll("INVERT");
  Serial.printf("%s direction is now %+d.\n", cfg[id].name, cfg[id].direction);
}

static void setGain(int id, int gainPct) {
  if (!isMotor(id)) {
    Serial.println("Motor must be 0..3.");
    return;
  }
  cfg[id].gainPct = clampInt(gainPct, GAIN_MIN_PCT, GAIN_MAX_PCT);
  Serial.printf("%s gain -> %d%%.\n", cfg[id].name, cfg[id].gainPct);
}

static void setPower(int newPowerUs) {
  powerUs = clampInt(newPowerUs, POWER_MIN_US, POWER_MAX_US);
  Serial.printf("Power -> %dus from center at full command.\n", powerUs);
}

static void setMoveTime(int newMoveMs) {
  moveMs = static_cast<uint32_t>(clampInt(newMoveMs, 50, 10000));
  Serial.printf("Move time -> %lums.\n", static_cast<unsigned long>(moveMs));
}

static void rawWrite(int id, int pulseUs) {
  if (!isMotor(id)) {
    Serial.println("Motor must be 0..3.");
    return;
  }
  moving = false;
  writePulse(id, pulseUs);
  Serial.printf("RAW %s -> %dus.\n", cfg[id].name, currentUs[id]);
}

static void turnOffMotor(int id) {
  if (!isMotor(id)) {
    Serial.println("Motor must be 0..3.");
    return;
  }
  moving = false;
  if (motorAttached[id]) {
    esc[id].detach();
    motorAttached[id] = false;
  }
  pinMode(cfg[id].pin, OUTPUT);
  digitalWrite(cfg[id].pin, LOW);
  currentUs[id] = 0;
  Serial.printf("OFF %s signal held LOW. Reattach by centering or driving.\n",
                cfg[id].name);
}

static bool waitDuringTest(uint32_t durationMs) {
  uint32_t startMs = millis();
  while (millis() - startMs < durationMs) {
    while (Serial.available()) {
      char c = static_cast<char>(Serial.read());
      if (c == ' ' || c == 'x' || c == 'X') {
        stopNow();
        lineLen = 0;
        Serial.println("Test aborted.");
        return false;
      }
    }
    delay(10);
  }
  return !testAbort;
}

static bool testMotor(int id) {
  if (!isMotor(id)) {
    Serial.println("Motor must be 0..3.");
    return false;
  }

  moving = false;
  testAbort = false;
  Serial.printf("Testing %d %s on %s. Only this motor changes pulse.\n",
                id,
                cfg[id].name,
                cfg[id].pinName);

  writePulse(id, cfg[id].centerUs);
  if (!waitDuringTest(300)) return false;
  writePulse(id, commandToPulse(id, TEST_POWER_PCT));
  Serial.printf("  forward-ish: %dus\n", currentUs[id]);
  if (!waitDuringTest(TEST_MS)) return false;
  writePulse(id, cfg[id].centerUs);
  if (!waitDuringTest(300)) return false;
  writePulse(id, commandToPulse(id, -TEST_POWER_PCT));
  Serial.printf("  reverse-ish: %dus\n", currentUs[id]);
  if (!waitDuringTest(TEST_MS)) return false;
  writePulse(id, cfg[id].centerUs);
  Serial.println("  centered.");
  return true;
}

static void testAllMotors() {
  testAbort = false;
  for (int i = 0; i < MOTOR_COUNT; i++) {
    if (!testMotor(i)) break;
    if (!waitDuringTest(300)) break;
  }
  centerAll("TEST DONE");
}

static bool readIntArg(char* token, int* out) {
  if (token == nullptr) return false;
  char* end = nullptr;
  long value = strtol(token, &end, 10);
  if (end == token || *end != '\0') return false;
  *out = static_cast<int>(value);
  return true;
}

static void handleOneKey(char c) {
  switch (c) {
    case 'w': case 'W': drive(0, +100, 0, moveMs, "FORWARD"); break;
    case 's': case 'S': drive(0, -100, 0, moveMs, "BACK"); break;
    case 'a': case 'A': drive(-100, 0, 0, moveMs, "LEFT"); break;
    case 'd': case 'D': drive(+100, 0, 0, moveMs, "RIGHT"); break;
    case 'q': case 'Q': case '-': case '_': drive(0, 0, -100, moveMs, "CCW"); break;
    case 'e': case 'E': case '+': case '=': drive(0, 0, +100, moveMs, "CW"); break;
    case 'x': case 'X': stopNow(); break;
    case 'u': case 'U': testAllMotors(); break;
    case 'p': case 'P': printConfig(); break;
    case '?': case 'h': case 'H': printHelp(); break;
    default:
      Serial.printf("Unknown command '%c'. Type ? for help.\n", c);
      break;
  }
}

static void handleLine(char* input) {
  char* cmd = strtok(input, " \t");
  if (cmd == nullptr) return;
  char* arg1 = strtok(nullptr, " \t");

  if (strlen(cmd) == 1 && arg1 == nullptr) {
    handleOneKey(cmd[0]);
    return;
  }

  if (!strcasecmp(cmd, "c") || !strcasecmp(cmd, "center")) {
    int id, centerUs;
    if (!readIntArg(arg1, &id) ||
        !readIntArg(strtok(nullptr, " \t"), &centerUs)) {
      Serial.println("Format: c <motor 0-3> <center_us>");
      return;
    }
    setCenter(id, centerUs);
    return;
  }

  if (!strcasecmp(cmd, "inv") || !strcasecmp(cmd, "invert")) {
    int id;
    if (!readIntArg(arg1, &id)) {
      Serial.println("Format: inv <motor 0-3>");
      return;
    }
    invertMotor(id);
    return;
  }

  if (!strcasecmp(cmd, "trim") || !strcasecmp(cmd, "t")) {
    int id, deltaUs;
    if (!readIntArg(arg1, &id) ||
        !readIntArg(strtok(nullptr, " \t"), &deltaUs)) {
      Serial.println("Format: trim <motor 0-3> <delta_us>");
      return;
    }
    trimCenter(id, deltaUs);
    return;
  }

  if (!strcasecmp(cmd, "gain") || !strcasecmp(cmd, "g")) {
    int id, gainPct;
    if (!readIntArg(arg1, &id) ||
        !readIntArg(strtok(nullptr, " \t"), &gainPct)) {
      Serial.println("Format: gain <motor 0-3> <20..200>");
      return;
    }
    setGain(id, gainPct);
    return;
  }

  if (!strcasecmp(cmd, "power")) {
    int newPowerUs;
    if (!readIntArg(arg1, &newPowerUs)) {
      Serial.println("Format: power <microseconds>");
      return;
    }
    setPower(newPowerUs);
    return;
  }

  if (!strcasecmp(cmd, "time")) {
    int newMoveMs;
    if (!readIntArg(arg1, &newMoveMs)) {
      Serial.println("Format: time <milliseconds>");
      return;
    }
    setMoveTime(newMoveMs);
    return;
  }

  if (!strcasecmp(cmd, "drive")) {
    int xPct, yPct, turnPct, customMs;
    if (!readIntArg(arg1, &xPct) ||
        !readIntArg(strtok(nullptr, " \t"), &yPct) ||
        !readIntArg(strtok(nullptr, " \t"), &turnPct)) {
      Serial.println("Format: drive <x -100..100> <y -100..100> <turn -100..100> [ms]");
      return;
    }
    char* msArg = strtok(nullptr, " \t");
    uint32_t durationMs = moveMs;
    if (msArg != nullptr) {
      if (!readIntArg(msArg, &customMs)) {
        Serial.println("Bad duration.");
        return;
      }
      durationMs = static_cast<uint32_t>(clampInt(customMs, 50, 10000));
    }
    drive(xPct, yPct, turnPct, durationMs, "DRIVE");
    return;
  }

  if (!strcasecmp(cmd, "test")) {
    int id;
    if (!readIntArg(arg1, &id)) {
      Serial.println("Format: test <motor 0-3>");
      return;
    }
    testMotor(id);
    return;
  }

  if (!strcasecmp(cmd, "raw") || !strcasecmp(cmd, "r")) {
    int id, pulseUs;
    if (!readIntArg(arg1, &id) ||
        !readIntArg(strtok(nullptr, " \t"), &pulseUs)) {
      Serial.println("Format: raw <motor 0-3> <1000..2000>");
      return;
    }
    rawWrite(id, pulseUs);
    return;
  }

  if (!strcasecmp(cmd, "off")) {
    int id;
    if (!readIntArg(arg1, &id)) {
      Serial.println("Format: off <motor 0-3>");
      return;
    }
    turnOffMotor(id);
    return;
  }

  if (!strcasecmp(cmd, "save")) {
    saveSettings();
    return;
  }

  if (!strcasecmp(cmd, "load")) {
    loadSettings();
    centerAll("LOAD");
    printConfig();
    return;
  }

  if (!strcasecmp(cmd, "defaults")) {
    clearSavedSettings();
    printConfig();
    return;
  }

  Serial.printf("Unknown command '%s'. Type ? for help.\n", cmd);
}

static void readSerial() {
  while (Serial.available()) {
    char c = static_cast<char>(Serial.read());

    if ((c == ' ' || c == 'x' || c == 'X') && lineLen == 0) {
      stopNow();
      printPrompt();
      continue;
    }

    if (c == '\r' || c == '\n') {
      if (lineLen > 0) {
        line[lineLen] = '\0';
        handleLine(line);
        lineLen = 0;
      }
      printPrompt();
      continue;
    }

    if (c == 8 || c == 127) {
      if (lineLen > 0) lineLen--;
      continue;
    }

    if (lineLen < static_cast<int>(sizeof(line)) - 1) {
      line[lineLen++] = c;
    } else {
      lineLen = 0;
      Serial.println("Input too long; cleared.");
      printPrompt();
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  pinMode(LED_BUILTIN, OUTPUT);

  loadSettings();

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  for (int i = 0; i < MOTOR_COUNT; i++) {
    pinMode(cfg[i].pin, OUTPUT);
    digitalWrite(cfg[i].pin, LOW);
    attachMotor(i);
    writePulse(i, cfg[i].centerUs);
  }

  Serial.println();
  Serial.println("Simple mecanum bot ready. Type ? for help.");
  centerAll("BOOT");
  printPrompt();
}

void loop() {
  readSerial();

  if (moving && static_cast<int32_t>(millis() - moveEndMs) >= 0) {
    centerAll("DONE");
    printPrompt();
  }

  if (neutralUntilMs != 0) {
    if (static_cast<int32_t>(millis() - neutralUntilMs) >= 0) {
      neutralUntilMs = 0;
    } else if (lastNeutralWriteMs == 0 || millis() - lastNeutralWriteMs >= NEUTRAL_REFRESH_MS) {
      lastNeutralWriteMs = millis();
      writeAllCenters();
    }
  }

  if (millis() - lastBlinkMs >= 500) {
    lastBlinkMs = millis();
    ledOn = !ledOn;
    digitalWrite(LED_BUILTIN, ledOn ? LOW : HIGH);
  }
}
