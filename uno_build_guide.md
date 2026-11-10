# Power-Fail-Safe Logger — Arduino Uno R3 Build Guide

**Board:** Arduino Uno R3  
**Firmware:** `power_fail_logger_uno.ino`  
**Backup:** 4× 1 F, 5.5 V supercapacitors in parallel (4 F)  
**Power:** 5 V adapter → diode-OR → Uno **5V pin** (not VIN)

---

## 1. Inventory check

Before wiring, put everything on the table and confirm:

- [ ] Arduino Uno R3
- [ ] 4× 1 F, 5.5 V supercapacitors (3 minimum)
- [ ] 2× 1N5819 Schottky diodes — **not RL207**
- [ ] 10 Ω, 5 W resistor (or 2× 22 Ω, 2 W in parallel)
- [ ] 10 kΩ ×4, 4.7 kΩ ×1, 220 Ω ×2
- [ ] 0.96" SSD1306 OLED, 4-pin I²C
- [ ] DS18B20 temperature probe
- [ ] DS3231 RTC module + coin cell
- [ ] Half-size breadboard
- [ ] 2-pin terminal block
- [ ] Jumper wires (M–M and M–F)
- [ ] 5 V, 2 A adapter
- [ ] Micro-USB breakout **or** DC jack/bench supply
- [ ] USB-A to USB-B cable
- [ ] Multimeter
- [ ] LED, buzzer, push button (optional but recommended)

**Critical:** the RL207 is a standard rectifier and will drop 0.7–1 V. Use only the **1N5819** (or 1N5817/1N5818/SB140/SR340).

---

## 2. Wiring diagram

```text
5 V adapter + ──► D1 (1N5819) ──► 5 V rail ──► Uno 5V pin
       │                              ▲
       └── 10 Ω 5 W ──► SC+ bank ──► D2 (1N5819)
                              │
                             SC− ── GND

Adapter + ── 10k ── A0 ── 10k ── GND     (BEFORE D1)
SC+       ── 10k ── A1 ── 10k ── GND

I2C: OLED + DS3231  SDA = A4, SCL = A5, VCC = 5 V
DS18B20: VDD = 5 V, GND, DQ = D4, 4.7 kΩ from DQ to 5 V
LED: D8 ── 220 Ω ── LED ── GND
Buzzer: D9 ── buzzer ── GND
Button: D2 ── button ── GND
```

### Diode orientation

- **D1:** anode to adapter +, **cathode (stripe) to 5 V rail**
- **D2:** anode to SC+ bank, **cathode (stripe) to 5 V rail**

### Supercapacitor bank

- Four 1 F, 5.5 V capacitors **in parallel**: all **+** leads to SC+, all **−** leads to GND.
- Use the 2-pin terminal block. Do not force the leads into the breadboard.
- Observe polarity. A reversed capacitor can fail.

### Uno power

- Connect the 5 V rail to the Uno **5V pin**.
- Do **not** use VIN.
- **Unplug the Uno USB cable** during the demo. Program first, then disconnect USB and power from the 5 V rail.

---

## 3. Step 1 — Build the power path without the Uno

Do not connect the Uno yet.

1. Place D1, D2, the 10 Ω 5 W resistor, and the supercapacitor bank on the breadboard/terminal block.
2. Wire the adapter + to D1 anode; D1 cathode to the 5 V rail.
3. Wire the adapter + through the 10 Ω resistor to SC+.
4. Wire SC+ to D2 anode; D2 cathode to the 5 V rail.
5. Wire SC− and adapter − to the common ground.
6. Build the A0 divider from adapter + (before D1): 10 kΩ to A0, 10 kΩ from A0 to GND.
7. Build the A1 divider from SC+: 10 kΩ to A1, 10 kΩ from A1 to GND.
8. **Do not connect the Uno 5V pin yet.** Measure first.

### Measure with the multimeter

| Point | Expected |
|---|---|
| Adapter + to GND | ~5.0 V |
| 5 V rail to GND | ~4.6–4.7 V (1N5819 drop ~0.3 V) |
| A0 to GND | ~2.5 V with adapter connected; 0 V when unplugged |
| A1 to GND | ~2.5 V when the supercapacitor is charged |
| SC+ to GND | ~5.0 V after charging |

**If the 5 V rail is ~4.2 V:** you are using the RL207 or the diode is reversed. Fix it before connecting the Uno.

### Charge and hold-up check

1. Plug in the adapter; wait **2–3 minutes** (4 F through 10 Ω).
2. Confirm SC+ ≈ 5 V.
3. Unplug the adapter.
4. The 5 V rail should stay above ~4.5 V for several seconds. You can use a **100 Ω, 1 W** resistor as a dummy load (~50 mA) to estimate, or connect the Uno later.

---

## 4. Step 2 — Connect the Uno and peripherals

1. Disconnect the adapter and USB.
2. Connect the 5 V rail to the Uno **5V pin** and common GND.
3. Connect the OLED and DS3231 to **A4/A5**, 5 V, GND.
4. Connect the DS18B20: VDD 5 V, GND, DQ D4, 4.7 kΩ pull-up to 5 V.
5. Connect the LED to D8 through 220 Ω; buzzer to D9; button D2 to GND.
6. **Connect USB only for programming.** Disconnect the adapter while USB is connected if possible.

### Test the I²C bus

Upload an I²C scanner. Expected:

- OLED: `0x3C` (or `0x3D`)
- DS3231: `0x68`

### Test the DS18B20

Upload the DallasTemperature example. Confirm a plausible room temperature.

---

## 5. Step 3 — Upload the full firmware

Open:

```text
power_fail_logger_uno/power_fail_logger_uno.ino
```

Select **Arduino Uno**, correct COM port, upload.

Open Serial Monitor at **115200 baud**. You should see:

```text
BOOT valid=0 seq=0 pfCount=0
READY
```

The OLED should show `POWER OK`, temperature, time, SC voltage, and PF count.

### Normal test

1. Press the button. Serial should print `MANUAL_LOG`; the PF count should not increment.
2. Unplug the adapter.
3. OLED should show `POWER LOST` → `SAVING...` → `SAVED`.
4. Plug the adapter back in.
5. The Uno resets and shows `RECOVERED` with the same temperature and timestamp.
6. Repeat 20 times; count successes.

---

## 6. Step 4 — Measure the hold-up

1. Connect the multimeter to SC+ and GND.
2. Unplug the adapter and start a stopwatch.
3. Stop when the **serial output stops** or the Uno resets. Do **not** stop when the OLED turns off; the firmware turns the OLED off after `SAVED` to save energy.
4. Record the time.

Expected with 4 F: **~5–6 s**. With 3 F: ~3.5–4.5 s.

If the hold-up is under ~3 s:

- Confirm the 1N5819 (not RL207).
- Confirm all four 1 F capacitors are in parallel and charged.
- Add a fifth 1 F capacitor, or add the MT3608 5 V boost.

---

## 7. Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| OLED blank | Wrong I²C address | Try 0x3C/0x3D; run I²C scanner |
| RTC not found | Wiring/coin cell | Check 0x68; replace CR2032/LIR2032 |
| DS18B20 shows `--` | Missing pull-up | 4.7 kΩ from DQ to 5 V |
| No power-fail detection | Divider after D1 | Move divider to adapter side of D1 |
| A0 stays ~2.5 V after unplug | Divider on 5 V rail | Move to adapter + before D1 |
| 5 V rail ~4.2 V | RL207 or reversed diode | Use 1N5819; stripe to 5 V rail |
| Uno resets immediately on unplug | Hold-up too short | 4–5× 1 F; check diode; add boost |
| Save but no recovery | EEPROM/CRC | Check serial `SAVE` line; verify magic/CRC |
| USB backfeed | USB connected during demo | Unplug USB; power from 5 V rail |
| Random resets | 5 V rail <4.5 V | Check diode drop; 5 V adapter; supercap charge |

---

## 8. What to record for the report

- 5 V rail with adapter: `____ V`
- A0 with adapter / unplugged: `____ V / ____ V`
- SC+ after charging: `____ V`
- Charge time to ~4.9 V: `____ s`
- Hold-up time (serial stops): `____ s`
- Recovery successes: `____ / 20`
- Temperature and timestamp from the `RECOVERED` screen: `____`

---

## 9. Important reminders

- **Use the Uno 5V pin, not VIN.**
- **Unplug USB during the demo.**
- **Use 1N5819, not RL207.**
- **All 1 F capacitors in parallel; observe polarity.**
- **Pre-charge 2–3 minutes.**
- **The OLED turns off after `SAVED` on purpose; do not use that as the brownout time.**
- The current report/proposal describe the ESP32 version. The Uno version uses EEPROM, A0/A1, 10-bit ADC, and a watchdog reset; update the report accordingly after the hardware works.
