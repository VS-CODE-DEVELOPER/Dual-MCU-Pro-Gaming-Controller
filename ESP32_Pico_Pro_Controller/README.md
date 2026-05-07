# ESP32-S3 Pro Controller Firmware

This sub-repository contains the primary firmware for the ESP32-S3 processing node. It acts as the mathematical and analog authority for the controller architecture. 

*(Note: This controller can operate as a completely standalone, independent device via direct Wired UART or standard BLE, even without the companion receiver dongle).*

## Core Technical Features

### Magnetic Sensor Signal Conditioning
This build is specifically optimized for magnetic input modules, supporting both **TMR (Tunneling Magnetoresistance)** thumbsticks and **SS49E Linear Hall Effect** analog triggers. The firmware applies localized exponential moving averages (EMA) to crush voltage ripple, and utilizes Pythagorean vector math to guarantee perfect circularity on the thumbsticks. 

### 16-Byte Hybrid Packet Architecture
To communicate flawlessly with GP2040-CE via UART, this firmware constructs a highly optimized data payload. It utilizes `__attribute__((packed))` to prevent the 32-bit ESP32 processor from artificially padding the struct with empty memory bytes. The resulting 16-byte packet includes magic sync bytes (`0xA5 0x5A`) and is validated by a bitwise XOR checksum before transmission.

### Non-Volatile Hardware Calibration
Standard DIY controllers rely on external PC software for deadzone configuration. This firmware contains a Live Calibration State Machine. Analog bounds, trigger resting states, and center-points are calculated on-device and written directly to the ESP32 `Preferences` flash memory. 

## Hardware Pinout

| Analog / ADC Inputs | ESP32-S3 GPIO | Digital Inputs | ESP32-S3 GPIO |
| :--- | :--- | :--- | :--- |
| **Left Stick X (TMR)** | GPIO 1 | **D-Pad UP/DOWN** | GPIO 11 / 12 |
| **Left Stick Y (TMR)** | GPIO 2 | **D-Pad L/R** | GPIO 13 / 18 |
| **Right Stick X (TMR)**| GPIO 3 | **Face Buttons (A/B/X/Y)** | 14 / 36 / 17 / 40 |
| **Right Stick Y (TMR)**| GPIO 4 | **Bumpers (L1/R1)** | GPIO 7 / 8 |
| **L2 Trigger (SS49E)** | GPIO 5 | **Stick Clicks (L3/R3)**| GPIO 9 / 10 |
| **R2 Trigger (SS49E)** | GPIO 6 | **System (Share/Opt/Home)**| 39 / 38 / 21 |

*(Note: NeoPixel Status LED is assigned to GPIO 48).*

## Mode Switching Protocol
The controller can hot-swap connection protocols dynamically. Hold **HOME** + a face button for 1.5 seconds:
* `HOME` + `OPTIONS`: **UART Wired Mode** (Direct serial transmission, LED flashes Green)
* `HOME` + `SHARE`: **ESP-NOW Dongle Mode** (1000Hz Encrypted 2.4GHz, LED flashes Orange)
* `HOME` + `TRIANGLE`: **BLE Mode** (Standard Bluetooth for mobile/PC, LED flashes Blue)

## User Guide: Calibration Sequence
Magnetic sensors must be calibrated to establish center-points and maximum vectors.
1. Power the controller. Press and hold `SHARE + L1 + R1` for two seconds. The status LED will illuminate **Yellow**.
2. **Hands off the sticks and triggers.** Wait 1.5 seconds for the firmware to register the resting baselines. The LED will change to **Purple**.
3. Rotate both analog sticks in complete, maximum-radius circles 3 to 4 times.
4. The LED will change to **Cyan**. Pull both analog triggers (L2/R2) completely to the bottom 3 to 4 times.
5. The calibration parameters are saved to NVS memory, the LED flashes confirmation, and normal operation resumes.