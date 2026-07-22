// D0-only ESC calibration/discovery test - XIAO ESP32-S3
//
// Purpose:
//   Discover what the ESC treats as stop/neutral before writing any run test.
//   The firmware boots at the measured neutral pulse and never spins automatically.
//
// Wiring:
//   ESC signal -> D0
//   ESC ground -> XIAO GND
//   ESC battery/power -> unplugged until serial confirms neutral output
//
// Props off. Common ground required.

#include <Arduino.h>
#include <ESP32Servo.h>

static const int ESC_PIN = D0;
static const int US_MIN = 1000;
static const int US_MAX = 2000;
static const int NUDGE_US = 5;

// Observed D0 motor/ESC behavior:
//   1520us first moved counter-clockwise.
//   1565us moved clockwise.
// Use 1540us as the practical neutral stop command from bench testing.
static const int US_CCW_START = 1520;
static const int US_CW_START = 1565;
static const int US_NEUTRAL = 1540;

Servo esc;
int currentUs = US_NEUTRAL;
uint32_t lastBlink = 0;
bool ledOn = false;

static int clampPulse(int us) {
  return constrain(us, US_MIN, US_MAX);
}

static void writePulse(int us, const char* label) {
  currentUs = clampPulse(us);
  esc.writeMicroseconds(currentUs);
  Serial.printf(">> %s: holding %d us on D0\n", label, currentUs);
}

static void printPowerOrder() {
  Serial.println();
  Serial.println("=== D0 MOTOR CALIBRATION / DISCOVERY TEST ===");
  Serial.println("NO AUTO SWEEP. NO AUTOMATIC SPIN.");
  Serial.println();
  Serial.println("Exact power/order checklist:");
  Serial.println("  1. Remove props.");
  Serial.println("  2. Keep the ESC battery UNPLUGGED.");
  Serial.println("  3. Wire ESC signal to XIAO D0.");
  Serial.println("  4. Wire ESC ground to XIAO GND. Common ground is mandatory.");
  Serial.println("  5. Plug the XIAO into USB.");
  Serial.println("  6. Flash firmware and open serial monitor at 115200 baud.");
  Serial.printf("  7. Confirm this monitor says it is holding %d us on D0.\n", US_NEUTRAL);
  Serial.println("  8. Only then plug in the ESC battery/power.");
  Serial.println("  9. Observe ESC beeps/status before sending any active command.");
  Serial.printf(" 10. Start with 'n' or 's' for neutral (%d us). Test 1000/2000 only when ready.\n", US_NEUTRAL);
  Serial.println();
}

static void printHelp() {
  printPowerOrder();
  Serial.println("Serial commands:");
  Serial.println("  ? or h  print this checklist/help");
  Serial.printf("  s       stop/neutral immediately: %d us\n", US_NEUTRAL);
  Serial.printf("  n       neutral: %d us\n", US_NEUTRAL);
  Serial.println("  l       low endpoint: 1000 us");
  Serial.println("  m       max endpoint: 2000 us");
  Serial.println("  +       nudge +5 us");
  Serial.println("  -       nudge -5 us");
  Serial.println("  1475    raw pulse in microseconds, clamped to 1000..2000");
  Serial.println();
  Serial.printf("Current output: %d us on D0\n", currentUs);
  Serial.println("Invalid commands do not change the output.");
  Serial.println("================================================");
  Serial.println();
}

static bool parseRawPulse(const String& text, int* outUs) {
  if (!text.length()) return false;
  for (size_t i = 0; i < text.length(); i++) {
    if (!isDigit(text[i])) return false;
  }
  *outUs = text.toInt();
  return true;
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);

  // Drive D0 low until the servo generator is attached, avoiding a floating
  // signal line during boot.
  pinMode(ESC_PIN, OUTPUT);
  digitalWrite(ESC_PIN, LOW);

  ESP32PWM::allocateTimer(0);
  esc.setPeriodHertz(50);
  esc.attach(ESC_PIN, US_MIN, US_MAX);
  writePulse(US_NEUTRAL, "BOOT NEUTRAL");

  delay(300);
  printHelp();
}

void loop() {
  if (millis() - lastBlink >= 500) {
    lastBlink = millis();
    ledOn = !ledOn;
    digitalWrite(LED_BUILTIN, ledOn ? LOW : HIGH);
  }

  if (!Serial.available()) return;

  String text = Serial.readStringUntil('\n');
  text.trim();
  if (!text.length()) return;

  int rawUs = 0;
  if (parseRawPulse(text, &rawUs)) {
    writePulse(rawUs, "RAW");
    return;
  }

  if (text.length() != 1) {
    Serial.printf("Invalid command '%s'. Output unchanged at %d us. Type '?' for help.\n",
                  text.c_str(), currentUs);
    return;
  }

  switch (text[0]) {
    case '?':
    case 'h':
    case 'H':
      printHelp();
      break;
    case 's':
    case 'S':
      writePulse(US_NEUTRAL, "STOP / NEUTRAL");
      break;
    case 'n':
    case 'N':
      writePulse(US_NEUTRAL, "NEUTRAL");
      break;
    case 'l':
    case 'L':
      writePulse(US_MIN, "LOW ENDPOINT");
      break;
    case 'm':
    case 'M':
      writePulse(US_MAX, "MAX ENDPOINT");
      break;
    case '+':
    case '=':
      writePulse(currentUs + NUDGE_US, "NUDGE +5");
      break;
    case '-':
    case '_':
      writePulse(currentUs - NUDGE_US, "NUDGE -5");
      break;
    default:
      Serial.printf("Invalid command '%s'. Output unchanged at %d us. Type '?' for help.\n",
                    text.c_str(), currentUs);
      break;
  }
}
