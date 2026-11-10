/*
 * Power-Fail-Safe Sensor Logger — Arduino Uno R3
 * -------------------------------------------------
 * Board  : Arduino Uno R3 (5 V, 16 MHz)
 * Sensor : DS18B20 temperature sensor on D4
 * Clock  : DS3231 RTC on I2C (A4/A5)
 * Display: SSD1306 0.96" OLED on I2C (A4/A5)
 * Storage: ATmega328P EEPROM (1 KB)
 * Backup : 4× 1 F, 5.5 V supercapacitors in parallel (4 F)
 *
 * Wiring
 * ------
 * I2C (OLED + DS3231): SDA = A4, SCL = A5
 * DS18B20 data       : D4, 4.7 kΩ pull-up to 5 V
 * Adapter sense      : A0, 10k/10k divider from adapter side of D1
 * Supercap sense     : A1, 10k/10k divider from SC+
 * LED                : D8 + 220 Ω to GND
 * Buzzer             : D9 to GND (active buzzer)
 * Button             : D2 to GND (INPUT_PULLUP)
 *
 * Power path
 * ----------
 * 5 V adapter → micro-USB breakout → D1 (1N5819) → 5 V rail → Uno 5V pin
 * 5 V adapter → 10 Ω 5 W → SC+ (4× 1 F parallel)
 * SC+ → D2 (1N5819) → 5 V rail
 * SC− → GND
 *
 * The adapter-sense divider must be on the adapter side of D1.
 * Unplug USB during the demo; the Uno is powered from the 5 V rail.
 *
 * Libraries:
 *   - Adafruit GFX Library
 *   - Adafruit SSD1306
 *   - OneWire
 *   - DallasTemperature
 *   - RTClib (Adafruit)
 *   EEPROM and avr/wdt are included with the Arduino core.
 *
 * Serial: 115200 baud
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
#include <string.h>

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
#define OLED_ADDR     0x3C

// --------------------------- constants --------------------------
#define MAGIC                 0x50464C47UL
#define ADAPTER_LOSS_V        3.5f
#define ADC_REF               5.0f
#define ADC_MAX               1023.0f
#define DIVIDER_RATIO         2.0f
#define POWER_FAIL_DEBOUNCE_MS 20
#define TEMP_CONVERSION_MS    750
#define DISPLAY_PERIOD_MS     250

#define EEPROM_ADDR_REC 0
#define EEPROM_ADDR_PF  32

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
void resetMCU();
float readVoltage(uint8_t pin);
uint16_t crc16(const uint8_t *data, size_t len);
uint32_t nowUnix();
void timeString(uint32_t ts, char *buf, size_t len);
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
    Serial.println(F("OLED not found at 0x3C; trying 0x3D..."));
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
      Serial.println(F("ERROR: SSD1306 OLED not found."));
      while (true) { delay(10); }
    }
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("POWER-FAIL LOGGER"));
  display.println(F("Booting..."));
  display.display();

  rtcReady = rtc.begin();
  if (!rtcReady) {
    Serial.println(F("WARNING: DS3231 not found; timestamps use uptime."));
  } else if (rtc.lostPower()) {
    Serial.println(F("RTC lost power; setting to compile time."));
    rtc.adjust(DateTime(__DATE__, __TIME__));
  }

  sensors.begin();
  sensors.setResolution(11);
  sensors.setWaitForConversion(false);

  bool valid = loadRecord();
  Serial.print(F("BOOT valid="));
  Serial.print(valid ? 1 : 0);
  Serial.print(F(" seq="));
  Serial.print(seq);
  Serial.print(F(" pfCount="));
  Serial.println(pfCount);

  if (valid && rec.powerFail) {
    displayRecovery();
    Serial.print(F("RECOVERED seq="));
    Serial.print(rec.seq);
    Serial.print(F(" temp="));
    Serial.print(rec.tempC, 2);
    Serial.print(F(" ts="));
    Serial.print(rec.timestamp);
    Serial.print(F(" crc="));
    Serial.println(rec.crc, HEX);
    delay(2000);
    clearPowerFail();
  } else {
    displayNormal();
  }

  lastGoodMs = millis();
  lastDisplayMs = millis();
  Serial.println(F("READY"));
}

// ----------------------------- loop -----------------------------
void loop() {
  checkPower();
  if (powerFail) {
    handlePowerFail();   // does not return (reset or brown-out)
  }

  checkButton();
  updateTemperature();

  if (millis() - lastDisplayMs >= DISPLAY_PERIOD_MS) {
    displayNormal();
    lastDisplayMs = millis();
  }

  static uint32_t lastCsvMs = 0;
  if (millis() - lastCsvMs >= 100) {
    Serial.print(millis());
    Serial.print(',');
    Serial.print(readVoltage(PIN_SC), 3);
    Serial.print(',');
    Serial.print(readVoltage(PIN_ADAPTER), 3);
    Serial.print(',');
    Serial.println((int)powerFail);
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

void timeString(uint32_t ts, char *buf, size_t len) {
  if (ts == 0) {
    strncpy(buf, "--:--:--", len);
    buf[len - 1] = '\0';
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
  pfCount = 0;   // blank EEPROM reads 0xFFFFFFFF; start clean
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

  Serial.print(F("SAVE seq="));
  Serial.print(rec.seq);
  Serial.print(F(" temp="));
  Serial.print(rec.tempC, 2);
  Serial.print(F(" ts="));
  Serial.print(rec.timestamp);
  Serial.print(F(" pf="));
  Serial.print(rec.powerFail);
  Serial.print(F(" crc="));
  Serial.println(rec.crc, HEX);
}

void clearPowerFail() {
  rec.powerFail = 0;
  rec.crc = crc16((const uint8_t *)&rec, sizeof(rec) - sizeof(rec.crc));
  EEPROM.put(EEPROM_ADDR_REC, rec);
}

void displayNormal() {
  char tbuf[9];
  timeString(nowUnix(), tbuf, sizeof(tbuf));

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(F("POWER OK"));

  display.print(F("T: "));
  if (isnan(lastTemp)) display.println(F("--"));
  else { display.print(lastTemp, 1); display.println(F(" C")); }

  display.print(F("Time: "));
  display.println(tbuf);

  display.print(F("SC: "));
  display.print(readVoltage(PIN_SC), 2);
  display.println(F(" V"));

  display.print(F("PF: "));
  display.println(pfCount);

  display.display();
}

void displayRecovery() {
  char tbuf[9];
  timeString(rec.timestamp, tbuf, sizeof(tbuf));

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(F("POWER-FAIL LOGGER"));
  display.println(F("RECOVERED"));

  display.print(F("T: "));
  if (isnan(rec.tempC)) display.println(F("--"));
  else { display.print(rec.tempC, 1); display.println(F(" C")); }

  display.print(F("Time: "));
  display.println(tbuf);

  display.print(F("PF count: "));
  display.println(pfCount);

  display.display();
  beep();
}

void displayPowerLost() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(F("POWER LOST"));
  display.println(F("SAVING..."));
  display.print(F("SC: "));
  display.print(readVoltage(PIN_SC), 2);
  display.println(F(" V"));
  display.display();
}

void displaySaved() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(F("SAVED"));
  display.print(F("PF #"));
  display.println(pfCount);
  display.print(F("SC: "));
  display.print(readVoltage(PIN_SC), 2);
  display.println(F(" V"));
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
  delay(800);
  display.ssd1306_command(SSD1306_DISPLAYOFF);
  digitalWrite(PIN_LED, LOW);

  uint32_t lastWaitLog = 0;
  while (true) {
    if (millis() - lastWaitLog >= 100) {
      Serial.print(millis());
      Serial.print(',');
      Serial.print(readVoltage(PIN_SC), 3);
      Serial.print(',');
      Serial.print(readVoltage(PIN_ADAPTER), 3);
      Serial.println(F(",1"));
      lastWaitLog = millis();
    }

    if (readVoltage(PIN_ADAPTER) > ADAPTER_LOSS_V) {
      resetMCU();
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
      Serial.println(F("MANUAL_LOG"));
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

void resetMCU() {
  wdt_enable(WDTO_15MS);
  while (true) {}
}
