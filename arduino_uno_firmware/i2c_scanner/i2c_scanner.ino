/*
 * I2C scanner — Arduino Uno R3
 * ----------------------------
 * Use this before the full firmware to verify the OLED and RTC wiring.
 *
 * Connections:
 *   OLED  VCC -> 5V,  GND -> GND,  SDA -> A4,  SCL -> A5
 *   DS3231 VCC -> 5V, GND -> GND, SDA -> A4,  SCL -> A5
 *
 * Expected addresses:
 *   0x3C (or 0x3D) = SSD1306 OLED
 *   0x68           = DS3231 RTC
 *
 * Serial Monitor: 115200 baud
 */

#include <Wire.h>

void setup() {
  Serial.begin(115200);
  Wire.begin();
  Serial.println(F("I2C scanner"));
}

void loop() {
  byte count = 0;

  for (byte address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.print(F("Found 0x"));
      if (address < 16) Serial.print('0');
      Serial.println(address, HEX);
      count++;
    }
  }

  Serial.print(F("Devices: "));
  Serial.println(count);
  delay(2000);
}
