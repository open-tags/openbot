// Single ESC timed sweep test — XIAO ESP32-S3, signal on D0
//
// Wiring: ESC signal (orange) -> D0, ESC ground (brown) -> GND,
//         ESC BEC red -> NOT connected, ESC power leads -> 2-4S battery.
//
// Repeating sequence (steps, then holds):
//     0%  for 20s   (neutral / stop)
//   100%  for  5s   (full forward)
//     0%  for 20s
//  -100%  for  5s   (full reverse)
//   ...loops forever.
#include <Arduino.h>
#include <ESP32Servo.h>

#define ESC_PIN  D0
#define US_MIN   1000
#define US_MID   1500
#define US_MAX   2000

Servo esc;
uint32_t lastBlink = 0; bool ledOn = false;

struct Step { int pct; uint32_t ms; };
const Step SEQ[] = {
  {   0, 20000 },
  { 100,  5000 },
  {   0, 20000 },
  {-100,  5000 },
};
const int NSTEPS = sizeof(SEQ) / sizeof(SEQ[0]);

int stepIdx = 0;
uint32_t stepStart = 0;

static void writePct(int pct) {
  pct = constrain(pct, -100, 100);
  esc.writeMicroseconds(US_MID + pct * (US_MAX - US_MID) / 100);
}

static void enterStep(int i) {
  stepIdx = i;
  stepStart = millis();
  writePct(SEQ[i].pct);
  Serial.printf("step %d: %d%% for %lus\n", i, SEQ[i].pct, SEQ[i].ms / 1000);
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);

  // Hold the signal line LOW ASAP so a powered ESC sees a clean "no signal"
  // during boot instead of floating glitches (prevents mis-arming).
  pinMode(ESC_PIN, OUTPUT);
  digitalWrite(ESC_PIN, LOW);

  ESP32PWM::allocateTimer(0);
  esc.setPeriodHertz(50);
  esc.attach(ESC_PIN, US_MIN, US_MAX);

  writePct(0);
  Serial.println("Arming (neutral 3s)...");
  delay(3000);
  Serial.println("Starting sweep sequence.");
  enterStep(0);
}

void loop() {
  if (millis() - lastBlink >= 500) { lastBlink = millis(); ledOn = !ledOn; digitalWrite(LED_BUILTIN, ledOn ? LOW : HIGH); }

  if (millis() - stepStart >= SEQ[stepIdx].ms) {
    enterStep((stepIdx + 1) % NSTEPS);
  }
}
