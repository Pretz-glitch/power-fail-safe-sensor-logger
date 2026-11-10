/*
 * Power-Fail-Safe Sensor Logger with Supercapacitor Hold-Up
 * ---------------------------------------------------------
 * Board  : ESP32 DevKit V1 (3.3 V logic, USB, NVS)
 * Sensor : DS18B20 temperature sensor
 * Clock  : DS3231 RTC (I2C)
 * Display: 0.96" SSD1306 OLED (I2C)
 * Storage: ESP32 NVS via the Preferences library
 * Backup : 5 F, 5.5 V supercapacitor + diode OR-ing
 *
 * Wiring
 * ------
 * I2C (OLED + DS3231): SDA = GPIO21, SCL = GPIO22
 * DS18B20 data       : GPIO4, 4.7k pull-up to 3.3 V
 * Adapter sense      : GPIO34, 10k/10k divider from the adapter side
 *                      (BEFORE diode D1, otherwise the supercap keeps it high)
 * Supercap sense     : GPIO35, 10k/10k divider from supercap +
 * LED                : GPIO26
 * Buzzer             : GPIO25 (optional)
 * Button             : GPIO16 (optional, INPUT_PULLUP)
 *
 * Power path
 * ----------
 * 5 V adapter ──► D1 (1N5819) ──► 5 V rail ──► ESP32 5V pin
 *      │                              ▲
 *      └── 10 Ω 5 W ──► SC+ ──► D2 ───┘
 *                        │
 *                       SC− ── GND
 *
 * The adapter-sense divider must be on the adapter side of D1. If it is
 * after D1, the supercapacitor holds the divider high and the firmware
 * never sees the power failure.
 *
 * Libraries (Arduino Library Manager):
 *   - Adafruit GFX Library
 *   - Adafruit SSD1306
 *   - OneWire
 *   - DallasTemperature
 *   - RTClib (Adafruit)
 *   Preferences is included with the ESP32 Arduino core.
 *
 * Serial output: 115200 baud
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <RTClib.h>
#include <Preferences.h>
#include <math.h>

// ----------------------------- pins -----------------------------
#define PIN_DS18B20 4
#define PIN_ADAPTER 34
#define PIN_SC      35
#define PIN_LED     26
#define PIN_BUZZER  25
#define PIN_BUTTON  16

// ----------------------------- OLED -----------------------------
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define OLED_ADDR     0x3C   // try 0x3D if the display stays blank

// --------------------------- constants --------------------------
#define MAGIC                 0x50464C47UL   // "PFLG"
#define ADAPTER_LOSS_V        3.5f           // calculated adapter voltage
#define ADC_REF               3.3f
#define ADC_MAX               4095.0f
#define DIVIDER_RATIO         2.0f           // 10k / 10k
#define POWER_FAIL_DEBOUNCE_MS 20
#define TEMP_CONVERSION_MS    750
#define DISPLAY_PERIOD_MS     250

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
OneWire oneWire(PIN_DS18B20);
DallasTemperature sensors(&oneWire);
RTC_DS3231 rtc;
Preferences prefs;

// Packed so the CRC covers a stable byte layout.
#pragma pack(push, 1)
struct Record {
  uint32_t magic;
  uint32_t seq;
  float    tempC;
  uint32_t timestamp;   // Unix time from DS3231
  uint8_t  powerFail;   // 1 = this record was written during a power failure
  uint16_t crc;         // CRC16-CCITT over the preceding bytes
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
String timeString(uint32_t ts);
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
  Serial.begin(115200);
  delay(300);

  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  digitalWrite(PIN_LED, LOW);
  digitalWrite(PIN_BUZZER, LOW);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);   // 0–3.3 V range

  Wire.begin(21, 22);
  Wire.setClock(100000);            // 100 kHz for a reliable I2C bus

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
    Serial.println("WARNING: DS3231 RTC not found; timestamps will use uptime.");
  } else if (rtc.lostPower()) {
    Serial.println("RTC lost power; setting to compile time.");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  sensors.begin();
  sensors.setResolution(11);          // 0.125 C, ~375 ms
  sensors.setWaitForConversion(false);

  bool valid = loadRecord();
  Serial.printf("BOOT valid=%d seq=%lu pfCount=%lu\n",
                valid ? 1 : 0, (unsigned long)seq, (unsigned long)pfCount);

  if (valid && rec.powerFail) {
    displayRecovery();
    Serial.printf("RECOVERED seq=%lu temp=%.2f ts=%lu crc=%04X\n",
                  (unsigned long)rec.seq, rec.tempC,
                  (unsigned long)rec.timestamp, rec.crc);
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
    handlePowerFail();   // does not return (restart or brown-out)
  }

  checkButton();
  updateTemperature();

  if (millis() - lastDisplayMs >= DISPLAY_PERIOD_MS) {
    displayNormal();
    lastDisplayMs = millis();
  }

  // CSV row every 100 ms for the optional Python plot.
  // Event lines (BOOT, SAVE, RECOVERED, ...) are words, not CSV;
  // the analysis script filters them out.
  static uint32_t lastCsvMs = 0;
  if (millis() - lastCsvMs >= 100) {
    Serial.printf("%lu,%.3f,%.3f,%d\n",
                  (unsigned long)millis(),
                  readVoltage(PIN_SC),
                  readVoltage(PIN_ADAPTER),
                  (int)powerFail);
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
  if (rtcReady) {
    return rtc.now().unixtime();
  }
  return millis() / 1000UL;
}

String timeString(uint32_t ts) {
  if (ts == 0) return "--:--:--";
  DateTime dt(ts);
  char buf[9];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
           dt.hour(), dt.minute(), dt.second());
  return String(buf);
}

bool loadRecord() {
  prefs.begin("pflog", true);
  size_t n = prefs.getBytes("rec", &rec, sizeof(rec));
  pfCount = prefs.getUInt("pfcount", 0);
  prefs.end();

  if (n == sizeof(rec) && rec.magic == MAGIC) {
    uint16_t c = crc16((const uint8_t *)&rec, sizeof(rec) - sizeof(rec.crc));
    if (c == rec.crc) {
      seq = rec.seq;
      return true;
    }
  }

  // No valid record: initialise a safe default.
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

  prefs.begin("pflog", false);
  prefs.putBytes("rec", &rec, sizeof(rec));
  prefs.putUInt("pfcount", pfCount);
  prefs.end();

  Serial.printf("SAVE seq=%lu temp=%.2f ts=%lu pf=%u crc=%04X\n",
                (unsigned long)rec.seq, rec.tempC,
                (unsigned long)rec.timestamp, rec.powerFail, rec.crc);
}

void clearPowerFail() {
  rec.powerFail = 0;
  rec.crc = crc16((const uint8_t *)&rec, sizeof(rec) - sizeof(rec.crc));

  prefs.begin("pflog", false);
  prefs.putBytes("rec", &rec, sizeof(rec));
  prefs.end();
}

void displayNormal() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("POWER OK");

  display.print("T: ");
  if (isnan(lastTemp)) display.println("--");
  else { display.print(lastTemp, 1); display.println(" C"); }

  display.print("Time: ");
  display.println(timeString(nowUnix()));

  display.print("SC: ");
  display.print(readVoltage(PIN_SC), 2);
  display.println(" V");

  display.print("PF: ");
  display.println(pfCount);

  display.display();
}

void displayRecovery() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("POWER-FAIL LOGGER");
  display.println("RECOVERED");

  display.print("T: ");
  if (isnan(rec.tempC)) display.println("--");
  else { display.print(rec.tempC, 1); display.println(" C"); }

  display.print("Time: ");
  display.println(timeString(rec.timestamp));

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
  delay(800);                         // let the evaluator see the message
  display.ssd1306_command(SSD1306_DISPLAYOFF);   // save OLED current
  digitalWrite(PIN_LED, LOW);

  // Wait for the adapter to return. If the supercapacitor drains first,
  // the ESP32 browns out; the record is already saved.
  // Log the decaying supercapacitor voltage for the optional plot.
  uint32_t lastWaitLog = 0;
  while (true) {
    if (millis() - lastWaitLog >= 100) {
      Serial.printf("%lu,%.3f,%.3f,1\n",
                    (unsigned long)millis(),
                    readVoltage(PIN_SC),
                    readVoltage(PIN_ADAPTER));
      lastWaitLog = millis();
    }

    if (readVoltage(PIN_ADAPTER) > ADAPTER_LOSS_V) {
      ESP.restart();                  // boot again and show RECOVERED
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
