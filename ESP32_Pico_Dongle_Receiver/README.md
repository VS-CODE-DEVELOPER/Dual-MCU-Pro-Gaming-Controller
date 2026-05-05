# ESP32 1000Hz Receiver Dongle

This directory contains the firmware for the ESP32 receiver bridge. Its singular function is to intercept 2.4GHz ESP-NOW packets from the Controller Node, validate the checksum, and relay the payload to the GP2040-CE host via high-speed UART (921600 baud).

*(Note: This dongle is an optional component. The architecture only requires it if you wish to achieve 1000Hz low-latency wireless. Standard BLE and Wired setups bypass this hardware entirely).*

## ⚙️ Engineering & RF Methodology

### Thermal Mitigation & ACK Suppression
A significant issue with DIY wireless microcontrollers at 1000Hz is thermal throttling. Standard 802.11 protocols require "ACK" (Acknowledgement) packets for every payload received. Forcing an ESP32 to receive and acknowledge data every 1 millisecond causes immense physical heat buildup, degrading the PCB trace antenna and ultimately dropping packets. 

This firmware suppresses the thermal load by restricting TX power using `WiFi.setTxPower(WIFI_POWER_8_5dBm)`. This stabilizes silicon temperatures during continuous 1000Hz reception without sacrificing standard room-scale wireless range.

### Zero-Latency Loop Hack
To guarantee absolute minimum latency, the default Arduino execution loop is intentionally terminated using `vTaskDelete(NULL)`. This ensures 100% of the primary core's clock cycles are dedicated strictly to listening for hardware-level Wi-Fi interrupts, preventing background Arduino tasks from delaying telemetry injection.

## 🔌 Hardware Wiring to GP2040-CE (RP2040)

Our custom GP2040-CE `.uf2` build hardcodes the serial translator to **GPIO 4 and GPIO 5**. You must wire the dongle exactly to these pins. For optimal signal integrity, keep wire lengths under 50mm.

| ESP32-S3 Dongle Pin | Connects to | GP2040-CE Host Pin |
| :--- | :--- | :--- |
| **TX (Transmit) -> GPIO 43** | ➔ | **RX (Receive) -> GPIO 5** |
| **RX (Receive) -> GPIO 44** | ➔ | **TX (Transmit) -> GPIO 4** |
| **GND** | ➔ | **GND** |
| **5V / VIN** | ➔ | **VSYS (Pin 39)** *(Optimal Power Draw)* |

⚡ **Engineering Note on Power (`VSYS`):** 
The ESP32 `VIN` is specifically wired to the Pico's `VSYS` pin rather than the `3V3 OUT` pin. During 1000Hz continuous radio transmission, the ESP32 can draw current spikes of up to 250mA. By pulling power from `VSYS`, the ESP32 draws current directly from the host USB 5V line, bypassing the RP2040's onboard 3.3V regulator and preventing brownouts.

⚠️ **CRITICAL:** Before powering on, you must access the GP2040-CE Web Config menu (`http://192.168.7.1`), navigate to **Pin Mapping**, and set `GPIO 4` and `GPIO 5` to `None`. If you do not clear the default arcade button mappings from these pins, the Pico will misinterpret the 1000Hz data stream as physical button mashing.

## 🚥 Visual Diagnostics
The dongle utilizes a low-priority FreeRTOS background task on Core 1 to drive a NeoPixel status LED, ensuring visual updates do not block Core 0 radio interrupts.
* **[Extremely Faint Blue]:** Idle State. The dongle is powered, the RP2040 is active, but no telemetry stream is detected from the Controller Node.
* **[Extremely Faint Green]:** Active State. A mathematically verified 1000Hz data stream is currently being intercepted and passed to the UART buffer.