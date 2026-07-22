// ============================================================================
//  demo bot — XIAO ESP32-S3
//  4x BLHeli_S ESC (analog/servo PWM) + I2C IMU bus scan
//
//  Serial-driven, bench-safe ESC tester. Drive it from the terminal:
//    ../deploy.sh            -> build + flash + open monitor
//
//  *** PROPS OFF FOR ALL TESTING ***
//
//  Wiring (XIAO ESP32-S3, see board pinout):
//    ESC1 signal -> D0 (GPIO1)      ESC3 signal -> D2 (GPIO3)
//    ESC2 signal -> D1 (GPIO2)      ESC4 signal -> D3 (GPIO4)
//    IMU SDA     -> D4 (GPIO5)      IMU SCL     -> D5 (GPIO6)
//    Common GND between XIAO, all ESCs, and IMU is MANDATORY.
//    ESCs have no BEC -> power the XIAO separately (USB-C or 5V reg).
//
//  Serial commands (115200 baud):
//    a        arm all ESCs (min-throttle hold)
//    0..9     set throttle 0%,10%,...,90% on the SELECTED motor(s)
//    + / -    nudge selected throttle by +/-5%
//    n        cycle selection: ALL -> M1 -> M2 -> M3 -> M4 -> ALL
//    s / SPC  EMERGENCY STOP all motors (throttle -> 0 / 1000us)
//    i        I2C bus scan (find the IMU address)
//    ?        print help + status
// ============================================================================

#include <Arduino.h>
#include <ESP32Servo.h>
#include <Wire.h>

// ---- config ----------------------------------------------------------------
static const int   ESC_PIN[4]   = { D0, D1, D2, D3 };
static const int   PWM_MIN_US   = 1000;   // 0% throttle / stop
static const int   PWM_MAX_US   = 2000;   // 100% throttle
static const int   ARM_HOLD_MS  = 3000;   // min-throttle hold to arm
static const int   SAFE_MAX_PCT = 40;     // bench safety cap (raise once trusted)

// ---- auto demo config ------------------------------------------------------
static const bool AUTO_DEMO      = true;  // ramp M1 up/down automatically, no input
static const int  AUTO_MOTOR     = 0;     // M1 -> D0
static const int  AUTO_MIN_PCT   = 8;     // stay above the motor's cogging/start threshold
static const int  AUTO_MAX_PCT   = 30;    // top of the sweep
static const int  AUTO_STEP_MS   = 150;   // time per 1% step
static const int  KICK_PCT       = 35;    // brief startup kick to break the motor loose
static const int  KICK_MS        = 500;

// ---- state -----------------------------------------------------------------
Servo esc[4];
int   throttlePct[4] = { 0, 0, 0, 0 };
int   selected       = -1;   // -1 = all motors, else 0..3

bool          autoRun   = AUTO_DEMO;
int           autoPct   = 0;
int           autoDir   = +1;
unsigned long autoLast  = 0;

static int pctToUs(int pct) {
  pct = constrain(pct, 0, 100);
  return map(pct, 0, 100, PWM_MIN_US, PWM_MAX_US);
}

static void applyMotor(int i) {
  esc[i].writeMicroseconds(pctToUs(throttlePct[i]));
}

static void setThrottle(int i, int pct) {
  throttlePct[i] = constrain(pct, 0, SAFE_MAX_PCT);
  applyMotor(i);
}

static void stopAll() {
  for (int i = 0; i < 4; i++) { throttlePct[i] = 0; applyMotor(i); }
  Serial.println(">>> STOP — all motors at 0%");
}

static void armAll() {
  Serial.printf("Arming: holding min throttle for %d ms ...\n", ARM_HOLD_MS);
  for (int i = 0; i < 4; i++) { throttlePct[i] = 0; esc[i].writeMicroseconds(PWM_MIN_US); }
  delay(ARM_HOLD_MS);
  Serial.println("Armed. (listen for ESC beeps)");
}

static void i2cScan() {
  Serial.println("I2C scan (D4=SDA, D5=SCL):");
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  device @ 0x%02X\n", addr);
      found++;
    }
  }
  if (!found) Serial.println("  (none found — check wiring / pullups / IMU power)");
}

static void printStatus() {
  Serial.println("------------------------------------------------------------");
  Serial.printf("selected: %s   safe-cap: %d%%\n",
                selected < 0 ? "ALL" : String(selected + 1).c_str(), SAFE_MAX_PCT);
  for (int i = 0; i < 4; i++)
    Serial.printf("  M%d (GPIO%d): %3d%%  (%d us)\n",
                  i + 1, ESC_PIN[i], throttlePct[i], pctToUs(throttlePct[i]));
  Serial.println("cmds: a=arm  0-9=throttle  +/-=nudge  n=next-motor  s/space=STOP  i=i2c  ?=help");
  Serial.println("------------------------------------------------------------");
}

static void applySelected(int pct) {
  if (selected < 0) for (int i = 0; i < 4; i++) setThrottle(i, pct);
  else              setThrottle(selected, pct);
}

static void nudge(int delta) {
  if (selected < 0) for (int i = 0; i < 4; i++) setThrottle(i, throttlePct[i] + delta);
  else              setThrottle(selected, throttlePct[selected] + delta);
}

void setup() {
  Serial.begin(115200);
  delay(300);

  Wire.begin();            // default I2C on D4/D5
  Wire.setClock(400000);

  // ESP32Servo needs explicit timer allocation
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  for (int i = 0; i < 4; i++) {
    esc[i].setPeriodHertz(50);
    esc[i].attach(ESC_PIN[i], PWM_MIN_US, PWM_MAX_US);
    esc[i].writeMicroseconds(PWM_MIN_US);   // boot at min throttle (safe)
  }

  Serial.println("\n=== demo bot ESC tester (XIAO ESP32-S3) ===");

  if (autoRun) {
    armAll();   // hold min throttle so the ESC arms (listen for beeps)
    Serial.printf("Startup kick: M1 -> %d%% for %d ms ...\n", KICK_PCT, KICK_MS);
    setThrottle(AUTO_MOTOR, KICK_PCT);     // break the motor loose
    delay(KICK_MS);
    autoPct = AUTO_MIN_PCT;
    setThrottle(AUTO_MOTOR, autoPct);
    Serial.printf("AUTO demo: M1 (D0) ramping %d%%<->%d%%. 's'=STOP, 'g'=resume.\n",
                  AUTO_MIN_PCT, AUTO_MAX_PCT);
    autoLast = millis();
  } else {
    Serial.println("PROPS OFF. 'a'=arm, 'n'=pick motor, 0-9=throttle, 's'=STOP.");
    printStatus();
  }
}

void loop() {
  // ---- automatic gentle ramp on M1 (no input needed) ----
  if (autoRun && millis() - autoLast >= AUTO_STEP_MS) {
    autoLast = millis();
    autoPct += autoDir;
    if (autoPct >= AUTO_MAX_PCT) { autoPct = AUTO_MAX_PCT; autoDir = -1; }
    if (autoPct <= AUTO_MIN_PCT) { autoPct = AUTO_MIN_PCT; autoDir = +1; }
    setThrottle(AUTO_MOTOR, autoPct);
    Serial.printf("M%d: %3d%%  (%d us)\n", AUTO_MOTOR + 1, autoPct, pctToUs(autoPct));
  }

  if (!Serial.available()) return;
  char c = Serial.read();

  // 's'/space always stops + halts the auto ramp; 'g' resumes it
  if (c == 's' || c == 'S' || c == ' ') { autoRun = false; stopAll(); return; }
  if (c == 'g' || c == 'G') { autoRun = true; autoLast = millis(); Serial.println(">>> AUTO resumed"); return; }

  switch (c) {
    case 'a': case 'A': armAll();            break;
    case 's': case 'S': case ' ': stopAll(); break;
    case 'i': case 'I': i2cScan();           break;
    case '?': case 'h': printStatus();       break;
    case '+': case '=': nudge(+5);  printStatus(); break;
    case '-': case '_': nudge(-5);  printStatus(); break;
    case 'n': case 'N':
      selected = (selected >= 3) ? -1 : selected + 1;
      if (selected < 0) Serial.println("selected: ALL");
      else Serial.printf("selected: M%d (GPIO%d)\n", selected + 1, ESC_PIN[selected]);
      break;
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
      applySelected((c - '0') * 10);
      printStatus();
      break;
    default: break;   // ignore CR/LF and unknown keys
  }
}
