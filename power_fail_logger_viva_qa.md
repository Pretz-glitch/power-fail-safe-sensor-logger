# Power-Fail-Safe Logger — Viva Question Bank

**Project:** Power-Fail-Safe Sensor Logger with Supercapacitor Hold-Up  
**Hardware:** ESP32 DevKit V1 + 5 F supercapacitor + DS18B20 + DS3231 + SSD1306 OLED

> **How to use this file:** Learn the first 2–3 sentences of each answer, then practise drawing the power path and the state machine without notes. The evaluator will follow your explanation, so being able to draw the system is more important than memorising every word.

---

## A. Project basics

### 1. What is your project?

A sensor logger that detects the loss of its 5 V adapter, uses a supercapacitor to stay alive for a few seconds, saves the last temperature reading and timestamp to non-volatile memory with a CRC, and restores the record on the next boot.

### 2. Why does a logger need power-fail protection?

A power failure during a memory write can corrupt the record, leave the file system inconsistent, or boot the device into an unknown state. The logger should detect the loss, finish the critical write, and shut down cleanly.

### 3. What is the difference between this and a UPS?

A UPS provides backup for minutes or hours using a battery. This project provides a **short hold-up of a few seconds** using a supercapacitor, just enough to save a record. It is not a battery replacement.

### 4. What is the one-sentence novelty?

"The concept of power-fail backup is established; our contribution is a low-cost, explainable implementation with a measured hold-up time, a CRC-protected record, and a deterministic recovery sequence."

### 5. What is the demo?

Power on → live temperature/time → unplug the adapter → OLED shows `POWER LOST` / `SAVING` / `SAVED` → plug back in → OLED shows `RECOVERED` with the last reading and timestamp. Repeat 20 times.

### 6. Why is the demo reliable?

The only physical action is unplugging a cable. There is no dropping, tether, water, RF, or machine learning. The supercapacitor is sized for seconds, and the save happens immediately.

### 7. What is the scope?

A proof-of-concept logger with one temperature sensor and one RTC. It is not a full UPS, not a file system, and not a certified product.

### 8. What is the cost?

Roughly ₹1,500–2,400 for the baseline, depending on the supercapacitor and whether you already have an ESP32.

### 9. Why not just use a battery?

A battery is better for long backup, but it adds charging circuitry, weight, maintenance, and safety concerns. A supercapacitor is simpler, has a very high cycle life, and is ideal for a few seconds of hold-up.

### 10. Why not just use a brownout detector and reset?

A brownout detector resets the CPU, but it does not guarantee that the last record was written. The supercapacitor gives the firmware time to save the record before the reset.

---

## B. Power path

### 11. Explain the power path.

The 5 V adapter feeds the rail through D1 and charges the supercapacitor through a 10 Ω resistor. When the adapter is removed, D1 blocks backfeed and D2 lets the supercapacitor power the rail. The ESP32's LDO maintains 3.3 V until the rail falls below its dropout limit.

### 12. What is the role of D1?

D1 prevents the supercapacitor from feeding current back into the adapter (or the USB port) when the adapter is removed. It also prevents the adapter from being back-driven by the backup.

### 13. What is the role of D2?

D2 connects the supercapacitor to the load when the adapter is absent. It is the backup path.

### 14. Why a Schottky diode?

A Schottky has a lower forward drop (~0.3 V) than a silicon diode (~0.7 V). Lower drop means more usable voltage and a longer hold-up.

### 15. Why 10 Ω in the charge path?

A discharged supercapacitor looks like a short circuit. The 10 Ω resistor limits the initial current to about 0.5 A and protects the adapter. It also sets the charge time constant.

### 16. Why 5 W for the resistor?

At 5 V and 10 Ω, the initial power is about 2.5 W. A 5 W resistor gives margin for the inrush and keeps the temperature rise reasonable.

### 17. Where is the adapter-sense divider connected?

On the **adapter side of D1**, before the diode. If it were after D1, the supercapacitor would hold the divider high and the firmware would never see the power failure.

### 18. How do you detect the power failure?

A 10k/10k divider scales the adapter voltage to half. The ESP32 reads it on GPIO34. If the calculated adapter voltage stays below 3.5 V for 20 ms, the firmware declares a power failure.

### 19. Why 20 ms debounce?

It rejects short glitches and contact bounce when the adapter is plugged or unplugged. The supercapacitor holds the rail during the debounce.

### 20. What happens if the adapter voltage sags but does not disappear?

If it stays above 3.5 V, the firmware keeps running. If it crosses the threshold for 20 ms, it treats it as a loss and saves. The threshold should be calibrated for your adapter.

### 21. Why is the 5 V rail about 4.7 V, not 5 V?

D1 drops about 0.3 V. The ESP32's LDO can accept 4.7 V and still produce 3.3 V. The supercapacitor also feeds through D2, so the rail is slightly below the capacitor voltage.

### 22. What is the ESP32 LDO dropout?

The AMS1117-style LDO on the DevKit needs about 1.0–1.1 V of headroom. With a 3.3 V output, the input must stay above about 4.3–4.4 V. This limits the usable supercapacitor range.

### 23. How can you increase the hold-up?

Use a larger capacitor, reduce the load (turn off the OLED), or add a 5 V boost converter that keeps the rail at 5 V while the capacitor discharges to ~2 V.

### 24. What is the disadvantage of the simple diode OR?

The supercapacitor can supply part of the load even when the adapter is present, because D2 is forward-biased when the capacitor voltage is above the rail. A power-path switch or ideal diode isolates it better.

### 25. Is the diode OR safe?

For a 5 V bench prototype, yes, with the 10 Ω limit and correct polarity. A real product would use a power-path controller, reverse-polarity protection, and a fuse.

---

## C. Supercapacitor and energy

### 26. What is a supercapacitor?

A high-capacitance electrochemical capacitor. It stores energy in an electric double layer, has a very high cycle life, and can deliver high current, but it has low energy density compared with a battery.

### 27. What is the energy stored in a capacitor?

```text
E = 0.5 · C · (V1² − V2²)
```

`V1` is the initial voltage, `V2` is the minimum usable voltage.

### 28. Calculate the hold-up for 5 F, 5.0 V to 4.6 V.

```text
E = 0.5 × 5 × (25 − 21.16) = 9.6 J
t = E / P = 9.6 / 0.5 = 19.2 s
```

That is theoretical; derate by at least 50% for ESR, dropout, and efficiency, giving about 9–10 s.

### 29. Why is the measured hold-up shorter than the calculation?

ESR, self-discharge, LDO dropout, diode drop, and the actual load current. The calculation assumes an ideal capacitor and a constant load.

### 30. How long does the supercapacitor take to charge?

With 10 Ω and 5 F, the time constant is 50 s. To reach about 95%, allow 3τ ≈ 150 s, or 2.5 minutes. Pre-charge before the demo.

### 31. What is the inrush current?

At the first instant, `I = 5 V / 10 Ω = 0.5 A`. The resistor limits it; without the resistor, the adapter would see a near-short.

### 32. What is ESR?

Equivalent Series Resistance. It causes an immediate voltage drop when current flows and limits the capacitor's ability to deliver high current. A low-ESR capacitor is better for hold-up.

### 33. What is self-discharge?

The gradual loss of charge when the capacitor is left open. It is higher than a battery's, so a supercapacitor is not suitable for long backup.

### 34. Why 5.5 V rating?

The adapter is 5 V, so a 5.5 V capacitor has a small margin. Do not charge it above 5.5 V. A 5 V adapter is safe; a 6 V adapter is not.

### 35. Can you use a 1 F capacitor?

Yes, but the hold-up is much shorter: about 3.8 s theoretical for 5.0→4.6 V, and possibly only 1–2 s measured. The 5 F part gives margin. The 1 F part charges faster and is cheaper.

### 36. What happens if the supercapacitor is connected backwards?

It can be damaged, leak, or fail. Check polarity before powering. The negative stripe is SC−.

### 37. Why use a supercapacitor instead of a large electrolytic capacitor?

A supercapacitor stores far more energy for the same size at low voltages. An electrolytic of the same capacitance would be much larger and would not be practical.

### 38. Is the supercapacitor a battery replacement?

No. It holds seconds, not hours. It is a short-term hold-up device.

---

## D. Firmware, NVS, and CRC

### 39. What is NVS?

Non-Volatile Storage in the ESP32. It stores key-value data in flash with wear leveling. The `Preferences` library provides a simple API.

### 40. Why use NVS instead of EEPROM?

The ESP32 does not have user EEPROM like an AVR; NVS is the built-in wear-leveled key-value store. It is convenient and adequate for a small record.

### 41. What is stored in the record?

Magic number, sequence number, temperature, timestamp, power-fail flag, and CRC. The record is 19 bytes packed.

### 42. Why a magic number?

It identifies a valid record and rejects an erased or unrelated NVS region.

### 43. Why a sequence number?

It shows how many saves occurred and helps detect a stale record. It also makes the demo easy to follow: the count increments on every power failure.

### 44. Why a CRC?

A CRC detects a partially written or corrupted record. On boot, the firmware recomputes the CRC and rejects the record if it does not match.

### 45. What CRC do you use?

CRC16-CCITT with the polynomial `0x1021` and initial value `0xFFFF`. It is simple, fast, and adequate for a 19-byte record.

### 46. What happens if the CRC fails?

The firmware treats the record as invalid, reports it on the serial monitor, and starts with a safe default. It does not display a corrupted reading as valid.

### 47. What is the power-fail flag?

A byte in the record that is set to 1 when the record is written during a power failure. On boot, if the flag is set, the firmware displays `RECOVERED` and then clears it.

### 48. Why clear the flag after recovery?

So the same record is not shown as "recovered" on every normal boot. The record remains, but the flag is cleared after the recovery screen.

### 49. Why non-blocking DS18B20 conversion?

The DS18B20 takes up to 750 ms to convert. If the firmware blocks during that time, a power failure could be detected late. The firmware starts the conversion, returns to the main loop, and reads the result later.

### 50. What is the state machine?

`RUN → POWER_FAIL → SAVE → DISPLAY → WAIT → RECOVER → RUN`. The record is saved before the `SAVED` message; the first `POWER LOST` screen is only a notification.

### 51. What happens when the adapter returns?

The firmware is waiting in the `WAIT` state. It sees the adapter voltage return, calls `ESP.restart()`, and the boot code reads the record and shows `RECOVERED`.

### 52. What happens if the supercapacitor drains before the adapter returns?

The ESP32 browns out. The record was already saved. When the adapter returns, the ESP32 boots and shows `RECOVERED`.

### 53. Why turn the OLED off after `SAVED`?

The OLED draws current. Turning it off extends the hold-up and reduces the load on the supercapacitor.

### 54. What is the watchdog for?

A watchdog resets the MCU if the firmware hangs. It is good practice in a power-fail design, though the prototype's main risk is power loss, not a hang.

### 55. What is the difference between a brownout and a power failure?

A brownout is a temporary sag; a power failure is a loss of supply. The firmware treats a sustained below-threshold adapter voltage as a power failure and saves.

---

## E. Testing and results

### 56. How did you test the hold-up?

Unplug the adapter, measure the time until the serial output stops or the ESP32 resets, and record the supercapacitor voltage. The OLED turns off intentionally after `SAVED`, so do not use the OLED-off time as the brownout time. Repeat five times and report the mean and standard deviation.

### 57. How did you test recovery?

Unplug for 2 s, plug back in, and confirm the `RECOVERED` screen shows the same temperature and timestamp. Repeat 20 times.

### 58. How did you test the CRC?

Write a record with a deliberately wrong CRC, reboot, and confirm the firmware rejects it. Remove the test mode before the demo.

### 59. What is your hold-up time?

`[FILL: measured value]` s at `[FILL]` F. The calculated value is `[FILL]` s; the difference is due to ESR, dropout, and load.

### 60. What is your recovery success rate?

`[FILL: successful cycles] / 20`. Report the failures and their causes honestly.

### 61. What is the charge time?

`[FILL]` s to about 4.9 V with `[FILL]` F and 10 Ω. It matches the RC time constant within the measurement error.

### 62. What was the hardest part?

`[FILL: e.g., the adapter-sense divider placement, the supercapacitor charge time, the NVS write timing]`. Explain what you changed.

---

## F. Novelty, limitations, and future work

### 63. This already exists. What is novel?

The concept is established. The contribution is the low-cost, explainable implementation with a measured hold-up time, a CRC-protected record, a deterministic recovery sequence, and a reproducible testbed. We do not claim a new concept.

### 64. What are the limitations?

Seconds, not hours; simple diode OR; LDO dropout limits the usable range; one record; NVS wear; RTC coin cell; no file system; no field trials.

### 65. What would you do with more time?

Add a 5 V boost or power-path switch, double-buffer the records, use FRAM, add more sensors, build a PCB/enclosure, and test brownouts/transients with an oscilloscope.

### 66. Why not use a file system?

A file system is complex and can be corrupted by a power loss. For one record, NVS + CRC is simpler and more robust. A file system is future work for a real log.

### 67. Why not use a battery?

A battery gives hours of backup but adds charging, weight, maintenance, and safety. The supercapacitor is the right size for a few seconds of hold-up.

### 68. What is the most important design lesson?

The usable hold-up depends on the **voltage window**, not just the capacitance. The LDO dropout and diode drops set the window. The first draft used a 5 V Nano and had a very narrow window; the final design uses the ESP32 and recommends a boost for more margin.

### 69. What would you tell a student starting this project?

Build and measure the power path first, before writing the full firmware. Confirm the adapter-sense divider is before D1, pre-charge the supercapacitor, and test the unplug/replug sequence 20 times. Keep the OLED current low.

### 70. What is the one-sentence takeaway?

"A supercapacitor cannot replace a battery, but it can buy enough time to save the last record and shut down cleanly — if the voltage window is designed correctly."
