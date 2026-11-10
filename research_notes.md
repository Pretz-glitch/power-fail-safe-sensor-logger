# Research Notes — Power-Fail-Safe Logger

**Prepared:** 5 Nov 2026  
**Purpose:** Record the sources, calculations, component checks, and design corrections behind the report. This file is a working note, not part of the formal report.

---

## 1. Design target

A low-cost sensor logger that detects adapter loss, holds up the electronics for a few seconds with a supercapacitor, saves a CRC-protected record to non-volatile memory, and restores it on the next boot.

**Demo:** unplug the 5 V adapter → `POWER LOST` → `SAVING` → `SAVED` → plug back in → `RECOVERED`.

**Why this project:** deterministic demo (only an unplug/replug), no mechanical motion, no tether, no water, no RF, no machine learning, and academically sound (power-fail architecture, energy budget, NVS, CRC, watchdog).

---

## 2. Key research findings

### 2.1 Supercapacitor energy and hold-up

The energy stored in a capacitor is:

```text
E = 0.5 · C · (V1² − V2²)
```

The hold-up time is:

```text
t = E / P_load
```

The usable voltage window is the critical design parameter. A 5 V rail that is allowed to fall to 3.3 V stores much more energy than a 5 V rail that must stay above 4.6 V.

### 2.2 Why the first draft (5 V Arduino Nano) was rejected

- The ATmega328P at 16 MHz requires a supply of at least **4.5 V** (datasheet: 16 MHz operation over 4.5–5.5 V).
- A diode OR drops about **0.3 V**.
- The Nano's 5 V pin therefore sees the supercapacitor voltage minus ~0.3 V.
- If the Nano needs 4.5 V, the supercapacitor can only discharge from ~5.0 V to ~4.8 V before the Nano is out of spec.
- That is a very small usable window and a short hold-up.

**Correction:** use the **ESP32 DevKit at 3.3 V**. Its LDO input can fall to about 4.3–4.4 V while the 3.3 V rail stays regulated, giving a larger usable window. For a longer hold-up, add a **5 V boost** after the supercapacitor.

### 2.3 ESP32 DevKit LDO

- The DevKit uses an AMS1117-style 3.3 V LDO.
- Dropout is roughly **1.0–1.1 V** at the currents used here.
- The input must stay above about **4.3–4.4 V** for the 3.3 V rail to remain regulated.
- The conservative hold-up calculation uses **5.0 V → 4.6 V**, not 5.0 V → 3.3 V.

### 2.4 Diode OR behavior

- With the adapter present, the rail is about 5 V − 0.3 V = 4.7 V.
- The supercapacitor is charged to about 5 V; D2 is forward-biased when the capacitor is above the rail, so the capacitor can supply part of the load while the adapter charges it through the 10 Ω resistor.
- This is acceptable for a prototype but is not true isolation. A power-path switch or ideal-diode controller is the correct upgrade.

### 2.5 Supercapacitor charging

- 5 F with 10 Ω: τ = 50 s; 3τ ≈ 150 s to ~95%.
- 2.2 F: τ = 22 s; 3τ ≈ 66 s.
- 1 F: τ = 10 s; 3τ ≈ 30 s.
- Initial current: 5 V / 10 Ω = 0.5 A.
- Initial resistor power: I²R = 0.25 × 10 = 2.5 W; use a 5 W resistor.

### 2.6 NVS and CRC

- The ESP32 `Preferences` library stores key-value data in NVS; NVS is wear-leveled.
- A record with a magic number, sequence number, timestamp, temperature, power-fail flag, and CRC16 protects against partial writes.
- CRC16-CCITT (poly 0x1021, init 0xFFFF) is adequate for a 19-byte record.
- Double-buffering (two record slots) is the next step for atomicity.

### 2.7 DS18B20 and RTC

- DS18B20 conversion takes up to 750 ms at 12-bit; use 11-bit or non-blocking conversion.
- DS3231 is I²C at 0x68; the OLED is at 0x3C/0x3D. They can share the bus.
- The DS3231 coin cell keeps time across power loss; check it before the demo.

---

## 3. Component and price checks

Prices are approximate Indian retail prices at the time of writing. **Verify current stock and price before ordering.**

| Item | Approx. price | Notes |
|---|---:|---|
| ESP32 DevKit V1 | ₹450–600 | Common; USB cable required |
| Supercapacitor 1 F, 5.5 V | ~₹111 | Robu listing; may be out of stock |
| Supercapacitor 1.5 F, 5.5 V | ~₹123 | Robu listing; may be out of stock |
| Supercapacitor 5 F, 5.5 V | ₹200–400 | Check stock; 5 F gives margin |
| 1N5819 Schottky | ₹10–20 each | 1 A, ~0.3 V drop |
| 10 Ω, 5 W resistor | ₹20–30 | Wirewound preferred |
| 10 kΩ resistors | ₹10 for a pack | Dividers |
| 4.7 kΩ resistor | ₹5 | DS18B20 pull-up |
| 0.96" SSD1306 OLED | ₹180–300 | I²C, 128×64 |
| DS18B20 | ₹100–150 | TO-92 or waterproof probe |
| DS3231 RTC | ₹100–150 | Check CR2032 |
| Breadboard + jumpers | ₹150–250 | |
| 5 V, 1 A adapter | ₹150–250 | The thing you unplug |
| USB cable | ₹50–100 | Programming only |
| Optional 5 V boost (MT3608) | ₹80–150 | Longer hold-up |

**Availability note:** the 1 F and 1.5 F 5.5 V supercapacitors were listed around ₹111–123 at Robu but showed out of stock at the time of checking. The 5 F part is the better choice for margin; if it is unavailable, use two 2.2 F or 1 F parts in parallel (verify polarity and voltage rating) or a 5.5 V supercapacitor module.

---

## 4. Calculations used in the report

### 4.1 Load power

| Block | Current at 3.3 V | Power |
|---|---:|---:|
| ESP32 (WiFi off) | ~80 mA | 0.264 W |
| OLED | ~20 mA | 0.066 W |
| DS3231 | ~1 mA | 0.003 W |
| DS18B20 | ~1 mA | 0.003 W |
| LDO losses | ~10 mA equivalent | ~0.033 W |
| **Total** | **~110 mA** | **~0.37 W** |

Use **0.5 W** for a conservative hold-up calculation.

### 4.2 Baseline hold-up, 5 F, 5.0 → 4.6 V

```text
E = 0.5 × 5 × (25 − 21.16) = 9.6 J
t = 9.6 / 0.5 = 19.2 s theoretical
```

Derate 50% → about 9–10 s. Enough for the save and display.

### 4.3 Boost hold-up, 5 F, 5.0 → 2.0 V

```text
E = 0.5 × 5 × (25 − 4) = 52.5 J
t = 52.5 / (0.5 / 0.85) ≈ 89 s theoretical
```

### 4.4 Capacitance trade-off

| C | LDO hold-up (calc.) | Boost hold-up (calc.) | Charge to 95% (10 Ω) |
|---:|---:|---:|---:|
| 1 F | 3.8 s | 18 s | ~30 s |
| 2.2 F | 8.4 s | 39 s | ~66 s |
| 5 F | 19.2 s | 89 s | ~150 s |

---

## 5. Design review — anomalies found and fixed

1. **5 V Arduino Nano voltage window:** rejected; use ESP32 DevKit 3.3 V or add a 5 V boost.
2. **Adapter-sense divider placement:** must be before D1; otherwise the supercapacitor holds it high.
3. **USB backfeed:** unplug USB during the demo; optional Schottky from USB.
4. **Supercapacitor inrush:** 10 Ω, 5 W series resistor; pre-charge 2–3 min.
5. **Diode OR isolation:** not a true power-path switch; document it; optional upgrade.
6. **ESP32 LDO dropout:** use 5.0 → 4.6 V in the conservative calculation; measure.
7. **DS18B20 blocking conversion:** use non-blocking conversion.
8. **NVS atomicity:** magic + sequence + CRC; double-buffer future.
9. **RTC coin cell:** check/replace; sequence number still works.
10. **Supercapacitor self-discharge/ESR:** derate 50%; measure.
11. **OLED current:** turn off after `SAVED`.
12. **ADC linearity:** calibrate the threshold; average samples; add 100 nF.

---

## 6. Sources / further reading

- Espressif, ESP32 datasheet and DevKitC schematic.
- Texas Instruments / AMS1117 datasheet (LDO dropout).
- Vishay, 1N5819 datasheet (forward voltage).
- Microchip, ATmega328P datasheet (16 MHz at 4.5–5.5 V).
- Maxim/Analog Devices, DS3231 and DS18B20 datasheets.
- Adafruit, SSD1306 and RTClib documentation.
- Espressif, NVS and `Preferences` documentation.
- Robu.in, 1 F and 1.5 F 5.5 V supercapacitor product pages (price/stock reference).
- General supercapacitor application notes on hold-up time, ESR, and self-discharge.

> **Note:** verify every price, stock status, and datasheet value before submission. Component availability changes quickly.
