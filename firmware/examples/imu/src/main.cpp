// BNO085 angle reader + always-on LED heartbeat — XIAO ESP32-S3
// Wiring: VCC->3V3, GND->GND, SDA->D4, SCL->D5, PS1->GND, PS0->GND, CS->3V3
// (RST/INT/ADO may be left unconnected; ADO open = address 0x4A)
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BNO08x.h>

Adafruit_BNO08x bno08x(-1);   // minimal: only VCC/GND/SDA/SCL wired, no reset pin
sh2_SensorValue_t sensorValue;

bool haveSensor = false;
uint32_t lastBlink = 0, lastScan = 0;
bool ledOn = false;

static void heartbeat() {                 // non-blocking 1 Hz blink, active-low LED
  if (millis() - lastBlink >= 500) {
    lastBlink = millis();
    ledOn = !ledOn;
    digitalWrite(LED_BUILTIN, ledOn ? LOW : HIGH);
  }
}

static bool tryStart() {
  if (bno08x.begin_I2C(0x4B) || bno08x.begin_I2C(0x4A)) {   // 0x4B is this board's default
    bno08x.enableReport(SH2_GAME_ROTATION_VECTOR, 20000);
    Serial.println("BNO085 FOUND - streaming angles");
    return true;
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  Wire.begin();          // D4=SDA, D5=SCL
  Wire.setClock(100000);
  delay(500);
  Serial.println("boot: heartbeat + BNO085 search (4-wire minimal)");
  haveSensor = tryStart();
}

void loop() {
  heartbeat();           // always blinks, no matter what

  if (!haveSensor) {
    if (millis() - lastScan >= 1500) {   // keep scanning until it shows up
      lastScan = millis();
      int found = 0;
      for (uint8_t a = 1; a < 127; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) { Serial.printf("i2c device 0x%02X\n", a); found++; }
      }
      Serial.println(found ? "^ device(s) seen; trying to start BNO085..." : "no i2c device (check CS->3V3)");
      if (found) haveSensor = tryStart();
    }
    return;
  }

  if (bno08x.wasReset()) bno08x.enableReport(SH2_GAME_ROTATION_VECTOR, 20000);
  if (bno08x.getSensorEvent(&sensorValue) &&
      sensorValue.sensorId == SH2_GAME_ROTATION_VECTOR) {
    auto &q = sensorValue.un.gameRotationVector;
    // Quaternion CSV for the 3D viewer:  Q,i,j,k,real
    Serial.printf("Q,%.5f,%.5f,%.5f,%.5f\n", q.i, q.j, q.k, q.real);
  }
}
