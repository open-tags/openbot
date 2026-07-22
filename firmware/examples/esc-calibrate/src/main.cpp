// Single ESC throttle CALIBRATION — XIAO ESP32-S3, signal on D0
//
// Wiring: ESC signal (orange) -> D0,  ESC ground (brown) -> GND,
//         ESC BEC red -> NOT connected,  ESC power leads -> 2-4S battery.
//
// WHY: calibration teaches the ESC your full-throttle and zero-throttle
// endpoints. The classic sequence is: send MAX first, power the ESC, it
// beeps to capture max; then send MIN, it beeps to confirm; done.
//
// HOW TO USE (battery UNPLUGGED to start):
//   1. Flash + open serial monitor. The signal boots at MAX (2000us).
//   2. NOW plug in the battery. The ESC should beep to capture full throttle.
//   3. Type 'l'  -> sends MIN (1000us). ESC beeps to confirm min = calibrated.
//   4. Type 'n'  -> neutral/stop (1500us). Done.
//
// Or type 'c' to run the whole timed sequence automatically.
//
// Manual keys any time:
//   h = HIGH/max (2000)   n = neutral/stop (1500)   l = LOW/min (1000)
//   c = auto calibrate     number = raw microseconds (e.g. 1600)
#include <Arduino.h>
#include <ESP32Servo.h>

#define ESC_PIN  D0
#define US_MIN   1000
#define US_MID   1500
#define US_MAX   2000

Servo esc;
uint32_t lastBlink = 0; bool ledOn = false;
int curUs = US_MID;                 // track current output for nudging

static void out(int us, const char* label) {
  curUs = constrain(us, US_MIN, US_MAX);
  esc.writeMicroseconds(curUs);
  Serial.printf(">> %s : %d us\n", label, curUs);
}

static void autoCalibrate() {
  Serial.println("== AUTO CALIBRATE (make sure ESC is powered) ==");
  out(US_MAX, "MAX  (hold 4s - ESC should beep to capture full throttle)");
  delay(4000);
  out(US_MIN, "MIN  (hold 4s - ESC should beep to confirm zero throttle)");
  delay(4000);
  out(US_MID, "NEUTRAL - calibration done, motor stopped");
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);

  ESP32PWM::allocateTimer(0);
  esc.setPeriodHertz(50);
  esc.attach(ESC_PIN, US_MIN, US_MAX);

  // Boot at MAX so you can power the ESC and have it capture full throttle.
  out(US_MAX, "BOOT at MAX");
  Serial.println("Now plug in the battery. ESC beeps = max captured.");
  Serial.println("Then: 'l'=min  'n'=neutral   (or 'c' for auto, 'h'=max)");
}

void loop() {
  if (millis() - lastBlink >= 500) { lastBlink = millis(); ledOn = !ledOn; digitalWrite(LED_BUILTIN, ledOn ? LOW : HIGH); }

  if (Serial.available()) {
    String t = Serial.readStringUntil('\n'); t.trim();
    if (!t.length()) return;
    switch (t[0]) {
      case 'h': case 'H': out(US_MAX, "MAX");     break;
      case 'l': case 'L': out(US_MIN, "MIN");     break;
      case 'n': case 'N': out(US_MID, "NEUTRAL"); break;
      case 'c': case 'C': autoCalibrate();        break;
      case '+':           out(curUs + 5, "nudge +5");  break;   // find true stop
      case '-':           out(curUs - 5, "nudge -5");  break;
      default:
        if (isdigit(t[0])) out(constrain(t.toInt(), US_MIN, US_MAX), "raw");
        else Serial.println("keys: h=max l=min n=neutral c=auto  +/- nudge  or raw us");
    }
  }
}
