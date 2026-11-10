# Power-Fail-Safe Sensor Logger — File Guide

**Project:** Power-Fail-Safe Sensor Logger with Supercapacitor Hold-Up  
**Current build:** Arduino Uno R3 + 4× 1 F, 5.5 V supercapacitors (4 F) + DS18B20 + DS3231 + SSD1306 OLED

## Start here

0. **For evaluator approval:** `project_proposal_for_approval.pdf` (3 pages; student name and roll number are filled in)
1. **Submission-style project report:** `power_fail_logger_project_report.pdf` (2 pages; same structure as the reference project sheet — Team No., registration, problem statement, objectives, components, circuit diagram, pin connections, notes)
2. **Read the Uno build report:** `arduino_uno_build_report.md` (or `.pdf`)
3. **Upload the Uno firmware:** `arduino_uno_firmware/arduino_uno_firmware.ino`
4. **Wire and measure the power path first** — before connecting the Uno
5. **Test the I²C bus and DS18B20**
6. **Run the unplug/replug test and measure the hold-up**
7. **Prepare for viva:** `power_fail_logger_viva_qa.md` (the ESP32-specific questions will be adapted to the Uno)
8. **Fill the results:** replace every `[FILL ...]` with your own measurements

> The original `power_fail_logger_report.md` and `power_fail_logger_firmware/` are the **ESP32 version**. The Uno build report and firmware are the current version; they use EEPROM, A0/A1, a 10-bit ADC, and a watchdog reset.

## Files

| File | What it is |
|---|---|
| `power_fail_logger_project_report.pdf` / `.html` | **Submission report (2 pages)** in the same structure/style as the reference project sheet |
| `figures/project_report_block_diagram.png` / `.svg` | Block diagram used in the submission report |
| `arduino_uno_build_report.md` / `.pdf` | Detailed Arduino Uno wiring, component purposes, power-path, test, and troubleshooting report |
| `arduino_uno_firmware/arduino_uno_firmware.ino` | Complete Arduino Uno sketch (EEPROM, A0/A1, 10-bit ADC) |
| `power_fail_logger_report.md` | Earlier ESP32 report draft (reference; the Uno build report is current) |
| `power_fail_logger_viva_qa.md` | 70 viva questions with answers (some ESP32-specific; adapt for Uno) |
| `power_fail_logger_firmware/power_fail_logger_firmware.ino` | Original ESP32 sketch (reference) |
| `figures/block_diagram.png` / `.svg` | System block diagram |
| `figures/power_path.png` / `.svg` | Power path: adapter OR-ing, supercapacitor, sense dividers |
| `figures/state_machine.png` / `.svg` | Power-fail state machine |
| `figures/hold_up_timeline.png` / `.svg` | Hold-up timeline |
| `figures/hold_up_vs_capacitance.png` / `.svg` | Calculated hold-up vs capacitance |
| `research_notes.md` | Sources, price checks, and design-review notes |

## Windows / Arduino IDE setup

1. Install **Arduino IDE 2.x** from [arduino.cc](https://www.arduino.cc/en/software).
2. Clone the repo on Windows:

   ```text
   git clone https://github.com/Pretz-glitch/power-fail-safe-sensor-logger.git
   ```

   Or, if GitHub CLI is installed:

   ```text
   gh repo clone Pretz-glitch/power-fail-safe-sensor-logger
   ```

3. Open `arduino_uno_firmware/arduino_uno_firmware.ino` in the Arduino IDE.
4. Select **Tools → Board → Arduino Uno** and the correct **COM** port.
   - If no port appears, install the **CH340 driver** (common on clone Uno boards).
5. Install these libraries from **Library Manager**:
   - Adafruit GFX Library
   - Adafruit SSD1306
   - OneWire
   - DallasTemperature
   - RTClib (Adafruit)
6. First test: open `arduino_uno_firmware/i2c_scanner/i2c_scanner.ino`, upload it, and open Serial Monitor at **115200**. The OLED should appear as **0x3C** (or **0x3D**); the RTC, once connected, appears as **0x68**.
7. Do **not** connect the external 5 V adapter to the Uno 5V pin while the USB cable is connected.

## Quick wiring

| Arduino Uno R3 | Module |
|---|---|
| 5V pin (not VIN) | OLED VCC, DS3231 VCC, DS18B20 VDD, 4.7 kΩ pull-up |
| GND | OLED GND, DS3231 GND, DS18B20 GND, dividers, SC− |
| A4 | OLED SDA, DS3231 SDA |
| A5 | OLED SCL, DS3231 SCL |
| D4 | DS18B20 DQ |
| A0 | Adapter-sense divider midpoint |
| A1 | Supercapacitor-sense divider midpoint |
| D8 | LED + 220 Ω |
| D9 | Buzzer + (optional) |
| D2 | Button to GND (optional) |

**Unplug the Uno USB cable during the demo.** Power comes from the 5 V rail into the **5V pin**.

## Power path

```text
5 V adapter + ──► D1 (1N5819) ──► 5 V rail ──► Uno 5V pin
       │                              ▲
       └── 10 Ω 5 W ──► SC+ bank ──► D2
                          │
                         SC− ── GND

Adapter + ── 10k ── A0 ── 10k ── GND   (BEFORE D1!)
SC+       ── 10k ── A1 ── 10k ── GND
```

**Critical:** the adapter-sense divider must be on the adapter side of D1. If it is after D1, the supercapacitor keeps it high and the firmware never sees the power failure.

**Critical:** unplug the USB cable during the demo. Program first, then power from the adapter/supercapacitor path.

## Key numbers

| Parameter | Value |
|---|---:|
| Supercapacitor | 4× 1 F, 5.5 V in parallel = 4 F |
| Charge resistor | 10 Ω, 5 W (or 2× 22 Ω, 2 W parallel) |
| Charge time to ~95% | ~120 s (2 min) |
| Calculated hold-up, 5.0→4.8 V, 0.4 W | 9.8 s theoretical |
| Derated hold-up | ~5 s |
| Adapter-loss threshold | 3.5 V calculated |
| Debounce | 20 ms |
| Recovery test | 20 unplug/replug cycles |

## Important notes

- **No results are invented.** The report has `[FILL ...]` placeholders for hold-up time, charge time, and recovery counts. Fill them with your real measurements.
- **Build the power path first.** Measure the 5 V rail, A0/A1, charge time, and hold-up before connecting the Uno.
- **No soldering iron/perfboard?** Use **two 4-pin terminal blocks** (one for all four + tabs, one for all four − tabs). Robu link: [4 Pin 5.08 mm Pitch Pluggable Screw Terminal Block (pack of 3)](https://robu.in/product/4-pin-5-08mm-pitch-pluggable-screw-terminal-block-pack-of-3/). Do not twist-and-tape.
- **Pre-charge the 4 F bank** for 2–3 minutes before the demo.
- **Use the Uno 5V pin, not VIN.** Unplug USB during the demo.
- **The Uno needs ≥4.5 V.** The 1N5819 drop leaves ~4.7 V; the 4 F bank gives about 5 s. If the measured hold-up is under 3 s, add a fifth 1 F or the MT3608 boost.
- **Use 1N5819, not RL207.** The RL207 drops too much voltage.
- **Safety:** 5 V only. The supercapacitor bank can deliver a large current if shorted; keep the leads insulated. Do not reverse polarity.

## If you have only one day

1. Build the power path; measure the 5 V rail and A0/A1.
2. Wire the Uno, OLED, RTC, and DS18B20; run the examples.
3. Upload the Uno sketch; confirm the normal screen.
4. Unplug/replug; confirm `POWER LOST` → `SAVED` → `RECOVERED`.
5. Record a video and fill the results table with what you measured.
6. Rehearse the 5-minute demo and the top 25 viva questions.

## Design corrections already applied

- Current build uses the **Arduino Uno R3**, 4× 1 F (4 F), **1N5819**, and **EEPROM**.
- The adapter-sense divider is **before D1**.
- The DS18B20 conversion is **non-blocking**.
- The record has **magic + sequence + CRC**.
- The OLED is turned off after `SAVED` to save energy.
- The 10 Ω resistor is rated **5 W** for inrush.
- The Uno uses the **5V pin**, not VIN; USB is unplugged during the demo.
