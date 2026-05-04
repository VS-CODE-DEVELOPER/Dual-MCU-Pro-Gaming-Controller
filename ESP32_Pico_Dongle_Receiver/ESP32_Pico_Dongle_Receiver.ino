/*
 * ESP32-S3 Pro Controller - DONGLE RECEIVER (TMR Optimized)
 * Matches the 16-Byte V1.11 Hybrid Packet | 1000Hz Cool-Running
 * Features: RTOS Low-Intensity Status LED (Blue = Idle, Green = Receiving)
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

// --- CONFIGURATION ---
#define UART_BAUD 921600
#define ESP_TX_PIN 43 // Connect to Dongle Pico RX
#define ESP_RX_PIN 44 // Connect to Dongle Pico TX
#define RGB_PIN 48    // Standard ESP32-S3 Built-in RGB Pin

// --- THE 16-BYTE PACKET ---
struct __attribute__((packed)) UARTPacket {
    uint8_t sync1 = 0xA5; 
    uint8_t sync2 = 0x5A;
    uint16_t buttons; 
    uint8_t dpad;
    uint8_t lx, ly, rx, ry, lt, rt;
    uint8_t aux[4]; 
    uint8_t checksum;
};

UARTPacket receivedPacket;

// Global timestamp to track the heartbeat without slowing down the Wi-Fi callback
volatile unsigned long lastRecvTime = 0;

// --- ESP-NOW CALLBACK ---
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
    // 1. Log the exact moment we got data (Zero overhead)
    lastRecvTime = millis();

    // 2. Check for exactly 16 bytes
    if (len == sizeof(UARTPacket)) {
        memcpy(&receivedPacket, incomingData, sizeof(UARTPacket));

        // 3. Check Magic Sync Bytes
        if (receivedPacket.sync1 == 0xA5 && receivedPacket.sync2 == 0x5A) {
            
            // 4. 🔒 Verify V1.11 Checksum (Looping exactly to index 14)
            uint8_t crc = 0;
            uint8_t* raw = (uint8_t*)&receivedPacket;
            for(int i = 2; i < 15; i++) crc ^= raw[i];
            
            // 5. Blast to GP2040-CE if math is perfect
            if (crc == receivedPacket.checksum) {
                Serial1.write(incomingData, len);
            }
        }
    }
}

// --- DEDICATED LED STATUS TASK ---
// This runs completely independent of the Wi-Fi radio, keeping latency at zero.
void statusLedTask(void *pvParameters) {
    for(;;) {
        // If we received a packet within the last 150 milliseconds
        if (millis() - lastRecvTime < 150) {
            // ACTIVELY RECEIVING: Extremely Faint Green (R:0, G:2, B:0)
            neopixelWrite(RGB_PIN, 0, 2, 0); 
        } else {
            // IDLE / DISCONNECTED: Extremely Faint Blue (R:0, G:0, B:2)
            neopixelWrite(RGB_PIN, 0, 0, 2); 
        }
        
        // Sleep for 100ms. We only need to update the visual LED 10 times a second.
        vTaskDelay(pdMS_TO_TICKS(100)); 
    }
}

void setup() {
    Serial1.begin(UART_BAUD, SERIAL_8N1, ESP_RX_PIN, ESP_TX_PIN);
    
    WiFi.mode(WIFI_STA);
    
    // 🛑 THE HEAT FIX: Force the Dongle to whisper its ACK packets 🛑
    WiFi.setTxPower(WIFI_POWER_8_5dBm); 
    
    if (esp_now_init() == ESP_OK) {
        esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
    }

    // Spin up the background LED task on Core 1
    xTaskCreatePinnedToCore(statusLedTask, "LED_Task", 2048, NULL, 1, NULL, 1);

    // ⚡ ZERO LATENCY & COOLING FIX: 
    // Delete the unused Core 1 Arduino loop entirely. 
    vTaskDelete(NULL); 
}

void loop() {
    // Completely dead and empty. Will never execute.
}
