// 4x bidirectional brushless ESC driver — XIAO ESP32-S3  (simple)
//
// Signals: Motor 0->D0, 1->D1, 2->D2, 3->D3.  ESC grounds -> XIAO GND.
// ESC BEC red wires NOT connected. Each ESC power leads -> 2-4S battery.
//
// Throttle: 1500us = STOP (stand still), 2000us = full fwd, 1000us = full rev.
//
// SERIAL (115200):
//   2 40     -> motor 2 to 40%   (negative = reverse, e.g. "2 -30")
//   x        -> STOP ALL
#include <Arduino.h>
#include <ESP32Servo.h>

#define NUM     4
#define US_STOP 1500
#define US_MIN  1000
#define US_MAX  2000

const int PINS[NUM] = { D0, D1, D2, D3 };
Servo esc[NUM];
uint32_t lastBlink = 0; bool ledOn = false;

static void stopAll() {                      // hold every ESC at neutral = stand still
  for (int m = 0; m < NUM; m++) esc[m].writeMicroseconds(US_STOP);
}

static void setMotor(int m, int pct) {
  if (m < 0 || m >= NUM) return;
  pct = constrain(pct, -100, 100);
  esc[m].writeMicroseconds(US_STOP + pct * (US_MAX - US_STOP) / 100);
  Serial.printf("motor %d -> %d%%\n", m, pct);
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  for (int m = 0; m < NUM; m++) {
    esc[m].setPeriodHertz(50);
    esc[m].attach(PINS[m], US_MIN, US_MAX);
  }

  stopAll();                 // neutral immediately, before anything else
  delay(3000);               // hold neutral so all ESCs arm to a standstill
  stopAll();
  Serial.println("Armed, all motors stopped. Cmd: '<motor> <pct>'  or  'x' to stop all.");
}

void loop() {
  // heartbeat so you can see the board is alive
  if (millis() - lastBlink >= 500) { lastBlink = millis(); ledOn = !ledOn; digitalWrite(LED_BUILTIN, ledOn ? LOW : HIGH); }

  if (Serial.available()) {
    String t = Serial.readStringUntil('\n'); t.trim();
    if (t.length()) {
      if (t[0] == 'x' || t[0] == 'X') { stopAll(); Serial.println("ALL STOP"); }
      else {
        int sp = t.indexOf(' ');
        if (sp > 0) setMotor(t.substring(0, sp).toInt(), t.substring(sp + 1).toInt());
        else Serial.println("format: <motor 0-3> <pct -100..100>,  or 'x'");
      }
    }
  }
}
