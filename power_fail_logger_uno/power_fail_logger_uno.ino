/*
 * Power-Fail-Safe Sensor Logger — Arduino Uno R3 Version
 * -------------------------------------------------------
 * Board  : Arduino Uno R3 (5 V, ATmega328P)
 * Sensor : DS18B20 temperature probe
 * Clock  : DS3231 RTC (I2C)
 * Display: 0.96" SSD1306 OLED (I2C)
 * Storage: ATmega328P EEPROM (1 KB)
 * Backup : 4x 1 F, 5.5 V supercapacitors in parallel (4 F)
 *
 * Wiring
 * ------
 * A0  : adapter-sense divider midpoint (10k/10k, BEFORE D1)
 * A1  : supercapacitor-sense divider midpoint (10k/10k from SC+)
 * A4  : SDA (OLED + DS3231)
 * A5  : SCL (OLED + DS3231)
 * D4  : DS18B20 DQ, 4.7k pull-up to 5 V
 * D8  : LED + 220 ohm to GND
 * D9  : active buzzer to GND
 * D2  : push button to GND (INPUT_PULLUP)
 * 5V  : 5 V rail from the diode-OR power path (NOT VIN)
 * GND : common ground
 *
 * Power path
 * ----------
 * 5 V adapter + ──► D1 (1N5819) ──► 5 V rail ──► Uno 5V pin
 *        │                              ▲
 *        └── 10 Ω 5 W ──► SC+ bank ──► D2 (1N5819)
 *                              │
 *                             SC− ── GND
 *
 * The adapter-sense divider must be on the adapter side of D1.
 * Unplug the Uno USB cable during the demo; power comes from the 5 V rail.
 * The Uno's 5V pin is used, not VIN.
 *
 * Libraries:
 *   - Adafruit GFX Library
 *   - Adafruit SSD1306
 *   - OneWire
 *   - DallasTemperature
 *   - RTClib (Adafruit)
 *   EEPROM and Wire are included with the Arduino core.
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <RTClib.h>
#include <EEPROM.h>
#include <avr/wdt.h>
#include <math.h>
#include <stdio.h>

// ----------------------------- pins -----------------------------
#define PIN_DS18B20 4
#define PIN_ADAPTER A0
#define PIN_SC      A1
#define PIN_LED     8
#define PIN_BUZZER  9
#define PIN_BUTTON  2

// ----------------------------- OLED -----------------------------
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define OLED_ADDR     0x3C   // try 0x3D if blank

// --------------------------- constants --------------------------
#define MAGIC                 0x50464C47UL   // "PFLG"
#define ADAPTER_LOSS_V        3.5f           // calculated adapter voltage
#define ADC_REF               5.0f
#define ADC_MAX               1023.0f
#define DIVIDER_RATIO         2.0f           // 10k / 10k
#define POWER_FAIL_DEBOUNCE_MS 20
#define TEMP_CONVERSION_MS    750
#define DISPLAY_PERIOD_MS     250

#define EEPROM_ADDR_REC       0
#define EEPROM_ADDR_PF        32

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
OneWire oneWire(PIN_DS18B20);
DallasTemperature sensors(&oneWire);
RTC_DS3231 rtc;

#pragma pack(push, 1)
struct Record {
  uint32_t magic;
  uint32_t seq;
  float    tempC;
  uint32_t timestamp;
  uint8_t  powerFail;
  uint16_t crc;
};
#pragma pack(pop)

Record rec;

uint32_t seq = 0;
uint32_t pfCount = 0;
bool rtcReady = false;
bool powerFail = false;
uint32_t lastGoodMs = 0;
uint32_t lastDisplayMs = 0;
uint32_t lastTempRequestMs = 0;
bool tempRequested = false;
float lastTemp = NAN;

// --------------------------- prototypes -------------------------
float readVoltage(uint8_t pin);
uint16_t crc16(const uint8_t *data, size_t len);
uint32_t nowUnix();
void formatTime(uint32_t ts, char *buf, size_t len);
bool loadRecord();
void saveRecord(uint8_t pf);
void clearPowerFail();
void displayNormal();
void displayRecovery();
void displayPowerLost();
void displaySaved();
void beep();
void handlePowerFail();
void checkPower();
void checkButton();
void updateTemperature();

// ----------------------------- setup ----------------------------
void setup() {
  wdt_disable();

  Serial.begin(115200);
  delay(300);

  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  digitalWrite(PIN_LED, LOW);
  digitalWrite(PIN_BUZZER, LOW);

  Wire.begin();
  Wire.setClock(100000);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED not found at 0x3C; trying 0x3D...");
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
      Serial.println("ERROR: SSD1306 OLED not found.");
      while (true) { delay(10); }
    }
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("POWER-FAIL LOGGER");
  display.println("Booting...");
  display.display();

  rtcReady = rtc.begin();
  if (!rtcReady) {
    Serial.println("WARNING: DS3231 RTC not found; timestamps use uptime.");
  } else if (rtc.lostPower()) {
    Serial.println("RTC lost power; setting to compile time.");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  sensors.begin();
  sensors.setResolution(11);          // 0.125 C, ~375 ms
  sensors.setWaitForConversion(false);

  bool valid = loadRecord();
  Serial.print("BOOT valid=");
  Serial.print(valid ? 1 : 0);
  Serial.print(" seq=");
  Serial.print(seq);
  Serial.print(" pfCount=");
  Serial.println(pfCount);

  if (valid && rec.powerFail) {
    displayRecovery();
    Serial.print("RECOVERED seq=");
    Serial.print(rec.seq);
    Serial.print(" temp=");
    Serial.print(rec.tempC, 2);
    Serial.print(" ts=");
    Serial.print(rec.timestamp);
    Serial.print(" crc=0x");
    Serial.println(rec.crc, HEX);
    delay(2000);
    clearPowerFail();
  } else {
    displayNormal();
  }

  lastGoodMs = millis();
  lastDisplayMs = millis();
  Serial.println("READY");
}

// ----------------------------- loop -----------------------------
void loop() {
  checkPower();
  if (powerFail) {
    handlePowerFail();   // does not return (watchdog reset or brown-out)
  }

  checkButton();
  updateTemperature();

  if (millis() - lastDisplayMs >= DISPLAY_PERIOD_MS) {
    displayNormal();
    lastDisplayMs = millis();
  }

  // CSV row every 100 ms for the optional plot.
  static uint32_t lastCsvMs = 0;
  if (millis() - lastCsvMs >= 100) {
    Serial.print(millis());
    Serial.print(',');
    Serial.print(readVoltage(PIN_SC), 3);
    Serial.print(',');
    Serial.print(readVoltage(PIN_ADAPTER), 3);
    Serial.print(',');
    Serial.println(powerFail ? 1 : 0);
    lastCsvMs = millis();
  }

  delay(10);
}

// --------------------------- functions --------------------------

float readVoltage(uint8_t pin) {
  int raw = analogRead(pin);
  return (raw * ADC_REF / ADC_MAX) * DIVIDER_RATIO;
}

uint16_t crc16(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (uint8_t bit = 0; bit < 8; bit++) {
      if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
      else crc <<= 1;
    }
  }
  return crc;
}

uint32_t nowUnix() {
  if (rtcReady) return rtc.now().unixtime();
  return millis() / 1000UL;
}

void formatTime(uint32_t ts, char *buf, size_t len) {
  if (ts == 0) {
    snprintf(buf, len, "--:--:--");
    return;
  }
  DateTime dt(ts);
  snprintf(buf, len, "%02d:%02d:%02d",
           dt.hour(), dt.minute(), dt.second());
}

bool loadRecord() {
  EEPROM.get(EEPROM_ADDR_REC, rec);
  EEPROM.get(EEPROM_ADDR_PF, pfCount);

  if (rec.magic == MAGIC) {
    uint16_t c = crc16((const uint8_t *)&rec, sizeof(rec) - sizeof(rec.crc));
    if (c == rec.crc) {
      seq = rec.seq;
      return true;
    }
  }

  rec.magic = MAGIC;
  rec.seq = 0;
  rec.tempC = NAN;
  rec.timestamp = 0;
  rec.powerFail = 0;
  rec.crc = 0;
  seq = 0;
  return false;
}

void saveRecord(uint8_t pf) {
  rec.magic = MAGIC;
  rec.seq = ++seq;
  rec.tempC = lastTemp;
  rec.timestamp = nowUnix();
  rec.powerFail = pf;
  rec.crc = crc16((const uint8_t *)&rec, sizeof(rec) - sizeof(rec.crc));

  EEPROM.put(EEPROM_ADDR_REC, rec);
  EEPROM.put(EEPROM_ADDR_PF, pfCount);

  Serial.print("SAVE seq=");
  Serial.print(rec.seq);
  Serial.print(" temp=");
  Serial.print(rec.tempC, 2);
  Serial.print(" ts=");
  Serial.print(rec.timestamp);
  Serial.print(" pf=");
  Serial.print(rec.powerFail);
  Serial.print(" crc=0x");
  Serial.println(rec.crc, HEX);
}

void clearPowerFail() {
  rec.powerFail = 0;
  rec.crc = crc16((const uint8_t *)&rec, sizeof(rec) - sizeof(rec.crc));
  EEPROM.put(EEPROM_ADDR_REC, rec);
}

void displayNormal() {
  char tbuf[9];
  formatTime(nowUnix(), tbuf, sizeof(tbuf));

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("POWER OK");

  display.print("T: ");
  if (isnan(lastTemp)) display.println("--");
  else { display.print(lastTemp, 1); display.println(" C"); }

  display.print("Time: ");
  display.println(tbuf);

  display.print("SC: ");
  display.print(readVoltage(PIN_SC), 2);
  display.println(" V");

  display.print("PF: ");
  display.println(pfCount);

  display.display();
}

void displayRecovery() {
  char tbuf[9];
  formatTime(rec.timestamp, tbuf, sizeof(tbuf));

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("POWER-FAIL LOGGER");
  display.println("RECOVERED");

  display.print("T: ");
  if (isnan(rec.tempC)) display.println("--");
  else { display.print(rec.tempC, 1); display.println(" C"); }

  display.print("Time: ");
  display.println(tbuf);

  display.print("PF count: ");
  display.println(pfCount);

  display.display();
  beep();
}

void displayPowerLost() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("POWER LOST");
  display.println("SAVING...");
  display.print("SC: ");
  display.print(readVoltage(PIN_SC), 2);
  display.println(" V");
  display.display();
}

void displaySaved() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("SAVED");
  display.print("PF #");
  display.println(pfCount);
  display.print("SC: ");
  display.print(readVoltage(PIN_SC), 2);
  display.println(" V");
  display.display();
  beep();
}

void beep() {
  digitalWrite(PIN_BUZZER, HIGH);
  delay(80);
  digitalWrite(PIN_BUZZER, LOW);
}

void handlePowerFail() {
  pfCount++;
  digitalWrite(PIN_LED, HIGH);
  displayPowerLost();
  saveRecord(1);
  displaySaved();
  delay(800);                                  // show SAVED
  display.ssd1306_command(SSD1306_DISPLAYOFF); // save OLED current
  digitalWrite(PIN_LED, LOW);

  // Wait for adapter return. If the supercapacitor drains first, the Uno
  // browns out; the record is already in EEPROM.
  while (true) {
    if (readVoltage(PIN_ADAPTER) > ADAPTER_LOSS_V) {
      wdt_enable(WDTO_15MS);                   // clean reset
      while (true) { }
    }
    delay(10);
  }
}

void checkPower() {
  float adapterV = readVoltage(PIN_ADAPTER);

  if (adapterV < ADAPTER_LOSS_V) {
    if (millis() - lastGoodMs > POWER_FAIL_DEBOUNCE_MS) {
      powerFail = true;
    }
  } else {
    lastGoodMs = millis();
  }
}

void checkButton() {
  static bool lastReading = HIGH;
  static bool stableState = HIGH;
  static uint32_t lastChange = 0;

  bool reading = digitalRead(PIN_BUTTON);

  if (reading != lastReading) {
    lastChange = millis();
    lastReading = reading;
  }

  if ((millis() - lastChange) > 30 && reading != stableState) {
    stableState = reading;

    if (stableState == LOW) {   // pressed: save a normal record
      saveRecord(0);
      Serial.println("MANUAL_LOG");
    }
  }
}

void updateTemperature() {
  if (!tempRequested) {
    sensors.requestTemperatures();
    tempRequested = true;
    lastTempRequestMs = millis();
  } else if (millis() - lastTempRequestMs >= TEMP_CONVERSION_MS) {
    float t = sensors.getTempCByIndex(0);
    if (t != DEVICE_DISCONNECTED_C) lastTemp = t;
    tempRequested = false;
  }
}
