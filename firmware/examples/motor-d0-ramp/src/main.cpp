// D0-only bidirectional motor ramp test - XIAO ESP32-S3
//
// Defines motor command 0 as the measured center/neutral pulse: 1540us.
// Serial 'g' starts a repeating ramp:
//   0 -> max over 5s, max -> 0 over 5s, wait 10s,
//   0 -> min over 5s, min -> 0 over 5s, wait 10s.
//
// Props off. Common ground required.

#include <Arduino.h>
#include <ESP32Servo.h>

static const int ESC_PIN = D0;
static const int US_MIN = 1000;
static const int US_CENTER = 1540;
static const int US_MAX = 2000;

static const uint32_t RAMP_MS = 5000;
static const uint32_t WAIT_MS = 10000;
static const uint32_t UPDATE_MS = 20;

Servo esc;
int currentUs = US_CENTER;
uint32_t lastBlink = 0;
bool ledOn = false;
bool running = false;

enum Phase {
  PHASE_TO_MAX,
  PHASE_MAX_TO_ZERO,
  PHASE_WAIT_AFTER_MAX,
  PHASE_TO_MIN,
  PHASE_MIN_TO_ZERO,
  PHASE_WAIT_AFTER_MIN,
};

Phase phase = PHASE_TO_MAX;
uint32_t phaseStartMs = 0;
uint32_t lastUpdateMs = 0;

static int clampPulse(int us) {
  return constrain(us, US_MIN, US_MAX);
}

static void writePulse(int us, const char* label) {
  currentUs = clampPulse(us);
  esc.writeMicroseconds(currentUs);
  Serial.printf(">> %s: %d us on D0\n", label, currentUs);
}

static int lerpPulse(int fromUs, int toUs, uint32_t elapsedMs, uint32_t durationMs) {
  if (elapsedMs >= durationMs) return toUs;
  long delta = static_cast<long>(toUs - fromUs);
  return fromUs + static_cast<int>((delta * static_cast<long>(elapsedMs)) / static_cast<long>(durationMs));
}

static const char* phaseName(Phase p) {
  switch (p) {
    case PHASE_TO_MAX: return "0 -> MAX";
    case PHASE_MAX_TO_ZERO: return "MAX -> 0";
    case PHASE_WAIT_AFTER_MAX: return "WAIT AFTER MAX";
    case PHASE_TO_MIN: return "0 -> MIN";
    case PHASE_MIN_TO_ZERO: return "MIN -> 0";
    case PHASE_WAIT_AFTER_MIN: return "WAIT AFTER MIN";
  }
  return "UNKNOWN";
}

static void enterPhase(Phase next) {
  phase = next;
  phaseStartMs = millis();
  lastUpdateMs = 0;
  Serial.printf("\n-- phase: %s --\n", phaseName(phase));

  if (phase == PHASE_WAIT_AFTER_MAX || phase == PHASE_WAIT_AFTER_MIN) {
    writePulse(US_CENTER, "WAIT / CENTER");
  }
}

static void stopRamp(const char* label) {
  running = false;
  enterPhase(PHASE_TO_MAX);
  writePulse(US_CENTER, label);
  Serial.println("Ramp stopped. Send 'g' to start again.");
}

static void startRamp() {
  running = true;
  writePulse(US_CENTER, "START / CENTER");
  enterPhase(PHASE_TO_MAX);
}

static void printHelp() {
  Serial.println();
  Serial.println("=== D0 BIDIRECTIONAL RAMP TEST ===");
  Serial.println("Order of operations:");
  Serial.println("  1. Remove props.");
  Serial.println("  2. Keep ESC battery UNPLUGGED.");
  Serial.println("  3. Wire ESC signal to XIAO D0.");
  Serial.println("  4. Wire ESC ground to XIAO GND. Common ground is mandatory.");
  Serial.println("  5. Plug XIAO into USB.");
  Serial.println("  6. Flash firmware and open serial monitor at 115200 baud.");
  Serial.printf("  7. Confirm this monitor says center is %d us and output is centered.\n", US_CENTER);
  Serial.println("  8. Only then plug in the ESC battery/power.");
  Serial.println("  9. Send 'g' to start the ramp. Send 's' to stop immediately.");
  Serial.println();
  Serial.printf("0/center: %d us\n", US_CENTER);
  Serial.printf("max:      %d us\n", US_MAX);
  Serial.printf("min:      %d us\n", US_MIN);
  Serial.printf("ramp:     %lu ms each leg\n", static_cast<unsigned long>(RAMP_MS));
  Serial.printf("wait:     %lu ms at center after each direction\n", static_cast<unsigned long>(WAIT_MS));
  Serial.println();
  Serial.println("Commands:");
  Serial.println("  g       start/resume repeating ramp");
  Serial.println("  s       stop immediately at center");
  Serial.println("  ? or h  print this help");
  Serial.println();
}

static void runRamp() {
  uint32_t now = millis();
  uint32_t elapsed = now - phaseStartMs;

  if (lastUpdateMs && now - lastUpdateMs < UPDATE_MS) return;
  lastUpdateMs = now;

  switch (phase) {
    case PHASE_TO_MAX:
      esc.writeMicroseconds(lerpPulse(US_CENTER, US_MAX, elapsed, RAMP_MS));
      if (elapsed >= RAMP_MS) {
        writePulse(US_MAX, "MAX");
        enterPhase(PHASE_MAX_TO_ZERO);
      }
      break;
    case PHASE_MAX_TO_ZERO:
      esc.writeMicroseconds(lerpPulse(US_MAX, US_CENTER, elapsed, RAMP_MS));
      if (elapsed >= RAMP_MS) {
        writePulse(US_CENTER, "CENTER");
        enterPhase(PHASE_WAIT_AFTER_MAX);
      }
      break;
    case PHASE_WAIT_AFTER_MAX:
      if (elapsed >= WAIT_MS) enterPhase(PHASE_TO_MIN);
      break;
    case PHASE_TO_MIN:
      esc.writeMicroseconds(lerpPulse(US_CENTER, US_MIN, elapsed, RAMP_MS));
      if (elapsed >= RAMP_MS) {
        writePulse(US_MIN, "MIN");
        enterPhase(PHASE_MIN_TO_ZERO);
      }
      break;
    case PHASE_MIN_TO_ZERO:
      esc.writeMicroseconds(lerpPulse(US_MIN, US_CENTER, elapsed, RAMP_MS));
      if (elapsed >= RAMP_MS) {
        writePulse(US_CENTER, "CENTER");
        enterPhase(PHASE_WAIT_AFTER_MIN);
      }
      break;
    case PHASE_WAIT_AFTER_MIN:
      if (elapsed >= WAIT_MS) enterPhase(PHASE_TO_MAX);
      break;
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);

  pinMode(ESC_PIN, OUTPUT);
  digitalWrite(ESC_PIN, LOW);

  ESP32PWM::allocateTimer(0);
  esc.setPeriodHertz(50);
  esc.attach(ESC_PIN, US_MIN, US_MAX);
  writePulse(US_CENTER, "BOOT CENTER");

  delay(300);
  printHelp();
}

void loop() {
  if (millis() - lastBlink >= 500) {
    lastBlink = millis();
    ledOn = !ledOn;
    digitalWrite(LED_BUILTIN, ledOn ? LOW : HIGH);
  }

  if (Serial.available()) {
    String text = Serial.readStringUntil('\n');
    text.trim();

    if (text == "g" || text == "G") startRamp();
    else if (text == "s" || text == "S") stopRamp("STOP / CENTER");
    else if (text == "?" || text == "h" || text == "H") printHelp();
    else if (text.length()) {
      Serial.printf("Invalid command '%s'. Use g=start, s=stop, ?=help.\n", text.c_str());
    }
  }

  if (running) runRamp();
}
