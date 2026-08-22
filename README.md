# TriMode Dual-MCU Pro Gamepad Architecture

A deterministic, high-performance embedded controller architecture combining ESP32-S3-based signal conditioning and communication control with an RP2040 running GP2040-CE for native USB HID output. The system supports Tri-Mode connectivity: UART (wired), ESP-NOW (encrypted 2.4GHz via custom dongle), and standard Bluetooth Low Energy (BLE). 

*(Note: The receiver dongle is entirely optional. The primary ESP32-S3 controller can operate as a standalone, independent device via direct Wired UART or standard BLE).*

## System Architecture & Methodology

Standard DIY controllers frequently encounter single-core processing bottlenecks, leading to USB polling jitter and dropped packets. By utilizing a **Dual-MCU Topology**, this project isolates the analog signal processing from the USB translation layer. 

### 1. Asynchronous Dual-Core Processing
Core 1 of the ESP32-S3 handles analog-to-digital conversion (ADC), physical switch debouncing, and floating-point vector mathematics. Core 0 is dedicated entirely to background radio transmission and queue management via FreeRTOS `xQueueOverwrite`. This completely decouples sensor reading from telemetry transmission, achieving zero input lag prior to RF broadcast.

### 2. Magnetic Sensor Signal Conditioning (TMR & Hall Effect)
Unlike standard ALPS potentiometers, magnetic sensors are highly sensitive to magnetic flux variations, requiring strict signal conditioning. This firmware handles both Tunneling Magnetoresistance (TMR) thumbsticks and **SS49E Linear Hall Effect** analog triggers (L2/R2) using:
* **Pythagorean Radial Math:** Forces a mathematically perfect circular output for TMR sticks, negating the physical jaggedness of 3D-printed or injection-molded stick gates.
* **Exponential Moving Averages (EMA):** Alpha-smoothing curves are applied dynamically to crush voltage ripple while maintaining micro-adjustment responsiveness.
* **Overshoot Compensation:** Scales outputs beyond the deadzone limits to guarantee 100% outer-ring actuation in esports titles.

### 3. Deterministic 1000Hz Pipeline
When using the optional wireless configuration, the ESP-NOW dongle offloads the USB HID polling overhead entirely to a dedicated Raspberry Pi Pico running GP2040-CE. Both the ESP32 controller and the receiver dongle utilize hard-coded 1ms RTOS ticks to tightly synchronize with the GP2040-CE 1000Hz USB polling rate.

## Repository Structure
This architecture relies on up to three distinct hardware components. 
1. **[Controller Node (ESP32-S3)](./ESP32_Pico_Pro_Controller):** The primary input processing unit featuring Tri-Mode Switching, SS49E trigger support, and NVS hardware calibration.
2. **[Receiver Dongle (ESP32-S3)](./ESP32_Pico_Dongle_Receiver):** *(Optional)* A dedicated 1000Hz wireless bridge engineered with thermal-throttled ACK packets.
3. **GP2040-CE Host (RP2040):** The serial translation layer that natively handles XInput, PS4, or Switch USB protocols.
4. **[Enclosure (CAD)](./cad):** Fusion 360 source and exportable STL/STEP files for the 3D-printed controller housing.

## Installation & Firmware Guide

All compiled binaries required for the RP2040 host are located on the Releases page.

** [Download the Firmware & Utility Files Here](https://github.com/VS-CODE-DEVLOPER/Dual-MCU-Pro-Gaming-Controller/releases/latest)**

### Step 1: Flash the RP2040 (GP2040-CE Host)
The Raspberry Pi Pico requires our specific build of GP2040-CE to accept the custom 16-byte UART payload from either the Controller (Wired) or the Dongle (Wireless).
1. Download `GP2040-CE_Custom_UART.uf2` from the Releases page.
2. Hold the `BOOTSEL` button on your RP2040 while plugging it into your PC via USB.
3. Drag and drop the `.uf2` file onto the `RPI-RP2` drive that appears. 
*(Note: If you encounter issues, use the included `flash_nuke.uf2` to wipe the board's memory first).*

### Step 2: Flash the ESP32 Nodes
1. Open the `.ino` files located in the Controller (and Dongle, if using) directories using the Arduino IDE.
2. **CRITICAL (Wireless Only):** If using the ESP-NOW dongle, locate this line in `ESP32_Pico_Pro_Controller.ino` and replace it with the MAC address of your specific Dongle ESP32:
   `uint8_t dongleAddress[] = {0xAC, 0xA7, 0x04, 0xEE, 0xCC, 0xB4};`
3. Flash the respective boards.

### Step 3: Hardware Wiring & GP2040-CE Pin Clearing
While the custom UART translation layer is built directly into our firmware, the base GP2040-CE system still assigns default arcade buttons to GPIO 4 and GPIO 5. You must clear these default assignments to prevent the Pico from misinterpreting your high-speed serial data as standard button presses.

1. Connect the RP2040 to your PC while holding `START` to enter Web Config mode.
2. Navigate to `http://192.168.7.1` in your browser. 
   > **Troubleshooting:** If the web menu will not open, simply drag and drop the included `force_web_config.uf2` file onto the RP2040 drive. **Do not use the flash nuke or delete any files.** This utility is non-destructive; it safely forces the web menu open without wiping your board's memory.
3. **Clear Default Pins:** Go to **Configuration > Pin Mapping**. Ensure `GPIO 4` and `GPIO 5` are completely unassigned (set them to `None`). Click **Save**.
4. **Finalize Wiring:** Wire the data lines from your ESP32 directly to the pre-defined hardware serial pins on the RP2040:
   * **ESP32 TX (GPIO 43)** ➔ **Pico RX (GPIO 5)**
   * **ESP32 RX (GPIO 44)** ➔ **Pico TX (GPIO 4)**
5. Reboot the device. The Pico will now seamlessly parse the 16-byte custom payload into native USB HID inputs.