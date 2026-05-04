/*
 * ESP32-S3 Pro Controller Master Code - THE TMR GOD BUILD
 * Architecture: Dual-Core FreeRTOS & 16-Byte GP2040-CE Packet
 * Math Engine: V1.11 Perfect-Circle Radial Math (TMR Tuned)
 * Polling: True 1000Hz (1ms FreeRTOS Tick)
 */

#include <Arduino.h>
#include <BleGamepad.h>
#include <math.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_now.h>

#define UART_BAUD 921600
#define RGB_PIN 48
#define ESP_TX_PIN 43
#define ESP_RX_PIN 44

// 🛑 REPLACE THIS with the MAC Address of your DONGLE ESP32 
uint8_t dongleAddress[] = {0xAC, 0xA7, 0x04, 0xEE, 0xCC, 0xB4};
esp_now_peer_info_t peerInfo;

Preferences prefs; 

enum ControllerMode { MODE_UART = 0, MODE_ESPNOW = 1, MODE_BLE = 2 };
ControllerMode currentMode; 

BleGamepadConfiguration bleConfig;
BleGamepad bleGamepad("ESP32_Pro_Controller", "DIY-LAB", 100);
bool bleStarted = false;

// --- THE V1.11 16-BYTE PACKET (Crucial for GP2040-CE compatibility) ---
struct __attribute__((packed)) UARTPacket {
    uint8_t sync1 = 0xA5; uint8_t sync2 = 0x5A;
    uint16_t buttons; uint8_t dpad;
    uint8_t lx, ly, rx, ry, lt, rt;
    uint8_t aux[4]; uint8_t checksum;
};
UARTPacket packet = {0xA5, 0x5A, 0, 0, 128,128,128,128, 0,0, {0,0,0,0}, 0};

// --- MULTI-CORE RTOS QUEUE ---
QueueHandle_t packetQueue;

// --- TMR TUNED BUTTERY MATH ---
// Was 0.35. Dropped slightly to crush the 1000Hz voltage ripple
const float STICK_ALPHA = 0.2;   // Lightened for TMR: Faster response, still smooth
const float TRIGGER_ALPHA = 0.08; 
float emaValues[6] = {2048, 2048, 2048, 2048, 2048, 2048};
enum AnalogAxis { AXIS_LX, AXIS_LY, AXIS_RX, AXIS_RY, AXIS_L2, AXIS_R2 };

int LX_CENTER, LY_CENTER, RX_CENTER, RY_CENTER;
int L2_RELEASE, R2_RELEASE;
int LX_MIN = 4095, LX_MAX = 0; int LY_MIN = 4095, LY_MAX = 0;
int RX_MIN = 4095, RX_MAX = 0; int RY_MIN = 4095, RY_MAX = 0;
int L2_MIN = 4095, L2_MAX = 0; int R2_MIN = 4095, R2_MAX = 0;
// Bump this up to 0.10 or 0.12 to swallow that remaining according to the  (7.4%) physical drift
const float STICK_DEADZONE_PCT = 0.10;   // Tightened for TMR: Instant micro-adjustments
const float TRIGGER_DEADZONE_PCT = 0.08; 
const float STICK_OVERSHOOT = 1.05;      
const float TRIGGER_OVERSHOOT = 1.15; 

#define PIN_STICK_LX 1
#define PIN_STICK_LY 2
#define PIN_STICK_RX 3
#define PIN_STICK_RY 4
#define PIN_L2 5
#define PIN_R2 6
#define PIN_L1 7
#define PIN_R1 8
#define PIN_L3 9
#define PIN_R3 10
#define PIN_DPAD_UP 11
#define PIN_DPAD_DOWN 12
#define PIN_DPAD_LEFT 13
#define PIN_DPAD_RIGHT 18
#define PIN_CROSS 14
#define PIN_CIRCLE 36
#define PIN_SQUARE 17
#define PIN_TRIANGLE 40
#define PIN_SHARE 39  
#define PIN_OPTIONS 38 
#define PIN_PS_HOME 21

// ================= HELPER FUNCTIONS =================
static int avgAnalog(int pin, int samples = 200) {
    long total = 0;
    for (int i = 0; i < samples; i++) { total += analogRead(pin); delayMicroseconds(200); }
    return (int)(total / samples);
}

void setModeLED() {
    if (currentMode == MODE_UART) neopixelWrite(RGB_PIN, 0, 50, 0);         // 🟩 Green
    else if (currentMode == MODE_ESPNOW) neopixelWrite(RGB_PIN, 50, 20, 0); // 🟧 Orange
    else if (currentMode == MODE_BLE) neopixelWrite(RGB_PIN, 0, 0, 50);     // 🟦 Blue
}

void switchControllerMode(ControllerMode newMode) {
    if (currentMode != newMode) {
        prefs.putInt("sysMode", newMode);
        neopixelWrite(RGB_PIN, 50, 50, 50); // Flash White confirmation
        delay(300); 
        ESP.restart(); 
    }
}

// ================= CORE 0: DEDICATED TRANSMISSION TASK =================
void transmissionTask(void *pvParameters) {
    UARTPacket txPacket; 
    for(;;) {
        // Wait indefinitely until Core 1 hands us a fully processed packet
        if (xQueueReceive(packetQueue, &txPacket, portMAX_DELAY) == pdPASS) {
            if (currentMode == MODE_UART) {
                Serial1.write((uint8_t*)&txPacket, sizeof(UARTPacket));
            } 
            else if (currentMode == MODE_ESPNOW) {
                esp_now_send(dongleAddress, (uint8_t*)&txPacket, sizeof(UARTPacket));
            }
        }
    }
}

// ================= ESP-NOW SETUP =================
void initESPNOW() {
    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK) return; 
    memcpy(peerInfo.peer_addr, dongleAddress, 6);
    peerInfo.channel = 0;     
    peerInfo.encrypt = false; 
    esp_now_add_peer(&peerInfo);
}

// ================= CALIBRATION =================
void runAndSaveCalibration() {
    neopixelWrite(RGB_PIN, 50, 50, 0); // 🟨 Yellow: HANDS OFF! (Reading physical center)
    delay(1500); 
    
    LX_MIN = 4095; LX_MAX = 0; LY_MIN = 4095; LY_MAX = 0;
    RX_MIN = 4095; RX_MAX = 0; RY_MIN = 4095; RY_MAX = 0;
    L2_MIN = 4095; L2_MAX = 0; R2_MIN = 4095; R2_MAX = 0;

    LX_CENTER = avgAnalog(PIN_STICK_LX); emaValues[AXIS_LX] = LX_CENTER;
    LY_CENTER = avgAnalog(PIN_STICK_LY); emaValues[AXIS_LY] = LY_CENTER;
    RX_CENTER = avgAnalog(PIN_STICK_RX); emaValues[AXIS_RX] = RX_CENTER;
    RY_CENTER = avgAnalog(PIN_STICK_RY); emaValues[AXIS_RY] = RY_CENTER;
    L2_RELEASE = avgAnalog(PIN_L2);      emaValues[AXIS_L2] = L2_RELEASE;
    R2_RELEASE = avgAnalog(PIN_R2);      emaValues[AXIS_R2] = R2_RELEASE;

    neopixelWrite(RGB_PIN, 50, 0, 50); // 🟪 Purple: Roll Sticks on Outer Edge
    unsigned long startTime = millis();
    while(millis() - startTime < 8000) {
        int lx = analogRead(PIN_STICK_LX); int ly = analogRead(PIN_STICK_LY);
        int rx = analogRead(PIN_STICK_RX); int ry = analogRead(PIN_STICK_RY);
        if(lx < LX_MIN) LX_MIN = lx; if(lx > LX_MAX) LX_MAX = lx;
        if(ly < LY_MIN) LY_MIN = ly; if(ly > LY_MAX) LY_MAX = ly;
        if(rx < RX_MIN) RX_MIN = rx; if(rx > RX_MAX) RX_MAX = rx;
        if(ry < RY_MIN) RY_MIN = ry; if(ry > RY_MAX) RY_MAX = ry;
        delay(1);
    }

    neopixelWrite(RGB_PIN, 0, 50, 50); // 🟦 Cyan: Pull Triggers to Bottom
    startTime = millis();
    while(millis() - startTime < 8000) {
        int l2 = analogRead(PIN_L2); int r2 = analogRead(PIN_R2);
        if(l2 < L2_MIN) L2_MIN = l2; if(l2 > L2_MAX) L2_MAX = l2;
        if(r2 < R2_MIN) R2_MIN = r2; if(r2 > R2_MAX) R2_MAX = r2;
        delay(1);
    }

    prefs.putInt("LXC", LX_CENTER); prefs.putInt("LYC", LY_CENTER); prefs.putInt("RXC", RX_CENTER); prefs.putInt("RYC", RY_CENTER);
    prefs.putInt("L2R", L2_RELEASE); prefs.putInt("R2R", R2_RELEASE);
    prefs.putInt("LXMIN", LX_MIN); prefs.putInt("LXMAX", LX_MAX); prefs.putInt("LYMIN", LY_MIN); prefs.putInt("LYMAX", LY_MAX);
    prefs.putInt("RXMIN", RX_MIN); prefs.putInt("RXMAX", RX_MAX); prefs.putInt("RYMIN", RY_MIN); prefs.putInt("RYMAX", RY_MAX);
    prefs.putInt("L2MIN", L2_MIN); prefs.putInt("L2MAX", L2_MAX); prefs.putInt("R2MIN", R2_MIN); prefs.putInt("R2MAX", R2_MAX);
    prefs.putBool("isCal", true);
    
    for(int i=0; i<6; i++) emaValues[i] = 2048; 
    setModeLED(); 
}

void loadCalibration() {
    LX_CENTER = prefs.getInt("LXC", 2048); LY_CENTER = prefs.getInt("LYC", 2048); RX_CENTER = prefs.getInt("RXC", 2048); RY_CENTER = prefs.getInt("RYC", 2048);
    L2_RELEASE = prefs.getInt("L2R", 0);   R2_RELEASE = prefs.getInt("R2R", 0);
    LX_MIN = prefs.getInt("LXMIN", 0); LX_MAX = prefs.getInt("LXMAX", 4095); LY_MIN = prefs.getInt("LYMIN", 0); LY_MAX = prefs.getInt("LYMAX", 4095);
    RX_MIN = prefs.getInt("RXMIN", 0); RX_MAX = prefs.getInt("RXMAX", 4095); RY_MIN = prefs.getInt("RYMIN", 0); RY_MAX = prefs.getInt("RYMAX", 4095);
    L2_MIN = prefs.getInt("L2MIN", 0); L2_MAX = prefs.getInt("L2MAX", 4095); R2_MIN = prefs.getInt("R2MIN", 0); R2_MAX = prefs.getInt("R2MAX", 4095);
    for(int i=0; i<6; i++) emaValues[i] = 2048; 
    setModeLED();
}

void processCircularStick(int pinX, int pinY, AnalogAxis axX, AnalogAxis axY, int centerX, int centerY, int minX, int maxX, int minY, int maxY, uint8_t &outX, uint8_t &outY, bool invertY) {
    emaValues[axX] = (STICK_ALPHA * analogRead(pinX)) + ((1.0 - STICK_ALPHA) * emaValues[axX]);
    emaValues[axY] = (STICK_ALPHA * analogRead(pinY)) + ((1.0 - STICK_ALPHA) * emaValues[axY]);
    
    float diffX = emaValues[axX] - centerX;
    float diffY = emaValues[axY] - centerY;

    float normX = 0;
    if (diffX > 0 && maxX > centerX) normX = diffX / (float)(maxX - centerX);
    else if (diffX < 0 && centerX > minX) normX = diffX / (float)(centerX - minX);

    float normY = 0;
    if (diffY > 0 && maxY > centerY) normY = diffY / (float)(maxY - centerY);
    else if (diffY < 0 && centerY > minY) normY = diffY / (float)(centerY - minY);

    float r = sqrt((normX * normX) + (normY * normY));
    
    if (r < STICK_DEADZONE_PCT) { outX = 128; outY = 128; return; }
    
    float scaledR = (r - STICK_DEADZONE_PCT) / (1.0 - STICK_DEADZONE_PCT);
    scaledR *= STICK_OVERSHOOT;
    if (scaledR > 1.0) scaledR = 1.0;

    normX = (normX / r) * scaledR;
    normY = (normY / r) * scaledR;
    if (invertY) normY = -normY;

    outX = (uint8_t)constrain(round((normX * 127.0) + 128.0), 0, 255);
    outY = (uint8_t)constrain(round((normY * 127.0) + 128.0), 0, 255);
}

uint8_t getGoldenTrigger8(int pin, AnalogAxis axis, int rel, int minV, int maxV) {
    int raw = analogRead(pin);
    emaValues[axis] = (TRIGGER_ALPHA * raw) + ((1.0 - TRIGGER_ALPHA) * emaValues[axis]);
    int filtered = (int)emaValues[axis];
    
    int maxPull = (abs(maxV - rel) > abs(rel - minV)) ? maxV : minV;
    int maxTravel = abs(maxPull - rel);
    
    if (maxTravel == 0) return 0; 
    if ((maxPull > rel && filtered < rel) || (maxPull < rel && filtered > rel)) return 0;

    float travelPct = (float)abs(filtered - rel) / (float)maxTravel;
    if (travelPct < TRIGGER_DEADZONE_PCT) return 0;

    float scaledTravel = (travelPct - TRIGGER_DEADZONE_PCT) / (1.0 - TRIGGER_DEADZONE_PCT);
    scaledTravel *= TRIGGER_OVERSHOOT; 
    if (scaledTravel > 1.0) scaledTravel = 1.0;

    return (uint8_t)(scaledTravel * 255.0);
}

// ================= SETUP =================
void setup() {
    Serial.begin(115200); 
    analogReadResolution(12);
    
    int digitalInputs[] = { PIN_L1, PIN_R1, PIN_L3, PIN_R3, PIN_DPAD_UP, PIN_DPAD_DOWN, PIN_DPAD_LEFT, PIN_DPAD_RIGHT, PIN_CROSS, PIN_CIRCLE, PIN_SQUARE, PIN_TRIANGLE, PIN_SHARE, PIN_OPTIONS, PIN_PS_HOME };
    for (int pin : digitalInputs) pinMode(pin, INPUT_PULLUP);
    
    prefs.begin("ctrl_cal", false); 
    
    // Default to UART if no mode is saved
    currentMode = (ControllerMode)prefs.getInt("sysMode", MODE_UART);

    if (currentMode == MODE_UART) {
        Serial1.begin(UART_BAUD, SERIAL_8N1, ESP_RX_PIN, ESP_TX_PIN);
    } 
    else if (currentMode == MODE_ESPNOW) {
        initESPNOW();
    }
    else if (currentMode == MODE_BLE) {
        bleConfig.setAutoReport(false); bleConfig.setAxesMin(0); bleConfig.setAxesMax(32767); bleConfig.setControllerType(CONTROLLER_TYPE_GAMEPAD);
        bleGamepad.begin(&bleConfig);
        bleStarted = true;
    }
    // Drop the transmit power from +20dBm down to +8.5dBm. 
    // This gives us plenty of room-scale range, but stops the chip from melting at 1000Hz.
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    // Initialize FreeRTOS Queue and Task for true 1000Hz decoupling
    packetQueue = xQueueCreate(1, sizeof(UARTPacket));
    xTaskCreatePinnedToCore(transmissionTask, "CommTask", 4096, NULL, 1, NULL, 0);

    if (!prefs.getBool("isCal", false)) { delay(2000); runAndSaveCalibration(); }
    else { loadCalibration(); }
}

// ================= MAIN LOOP =================
void loop() {
    bool homePressed = !digitalRead(PIN_PS_HOME);
    bool sharePressed = !digitalRead(PIN_SHARE);
    bool optionsPressed = !digitalRead(PIN_OPTIONS);
    bool trianglePressed = !digitalRead(PIN_TRIANGLE);
    bool l1Pressed = !digitalRead(PIN_L1); // NEW: Replaced L3
    bool r1Pressed = !digitalRead(PIN_R1); // NEW: Replaced R3
    
    static unsigned long modeHoldStart = 0;
    static unsigned long calibHoldStart = 0;

    // 🛠️ LIVE RECALIBRATION COMBO (SHARE + L1 + R1)
    if (sharePressed && l1Pressed && r1Pressed) {
        if (calibHoldStart == 0) calibHoldStart = millis();
        if (millis() - calibHoldStart > 2000) { runAndSaveCalibration(); calibHoldStart = 0; }
    } else { calibHoldStart = 0; }

    // 🛠️ TRI-MODE SWITCHING (Hold HOME + Face Button for 1.5s)
    // Checking !l1Pressed and !r1Pressed prevents accidental mode switches while calibrating
    if (homePressed && !l1Pressed && !r1Pressed) {
        if (modeHoldStart == 0) modeHoldStart = millis();
        if (millis() - modeHoldStart > 1500) {
            if (optionsPressed)       switchControllerMode(MODE_UART);
            else if (sharePressed)    switchControllerMode(MODE_ESPNOW);
            else if (trianglePressed) switchControllerMode(MODE_BLE);
        }
    } else { modeHoldStart = 0; }

    uint8_t lx8, ly8, rx8, ry8;
    processCircularStick(PIN_STICK_LX, PIN_STICK_LY, AXIS_LX, AXIS_LY, LX_CENTER, LY_CENTER, LX_MIN, LX_MAX, LY_MIN, LY_MAX, lx8, ly8, true);
    processCircularStick(PIN_STICK_RX, PIN_STICK_RY, AXIS_RX, AXIS_RY, RX_CENTER, RY_CENTER, RX_MIN, RX_MAX, RY_MIN, RY_MAX, rx8, ry8, true);

    uint8_t lt8 = getGoldenTrigger8(PIN_L2, AXIS_L2, L2_RELEASE, L2_MIN, L2_MAX);
    uint8_t rt8 = getGoldenTrigger8(PIN_R2, AXIS_R2, R2_RELEASE, R2_MIN, R2_MAX);

    // ================= DATA PREPARATION =================
    if (currentMode == MODE_UART || currentMode == MODE_ESPNOW) {
        packet.buttons = 0;
        if (!digitalRead(PIN_SHARE) && !digitalRead(PIN_CROSS)) { packet.buttons |= (1 << 13); } 
        else {
            if(!digitalRead(PIN_CROSS)) packet.buttons |= (1 << 0);
            if(!digitalRead(PIN_SHARE)) packet.buttons |= (1 << 8);
        }

        if(!digitalRead(PIN_CIRCLE))   packet.buttons |= (1 << 1);
        if(!digitalRead(PIN_SQUARE))   packet.buttons |= (1 << 2);
        if(!digitalRead(PIN_TRIANGLE)) packet.buttons |= (1 << 3);
        if(!digitalRead(PIN_L1))       packet.buttons |= (1 << 4);
        if(!digitalRead(PIN_R1))       packet.buttons |= (1 << 5);
        if(!digitalRead(PIN_OPTIONS))  packet.buttons |= (1 << 9);
        if(!digitalRead(PIN_L3))       packet.buttons |= (1 << 10);
        if(!digitalRead(PIN_R3))       packet.buttons |= (1 << 11);
        if(!digitalRead(PIN_PS_HOME))  packet.buttons |= (1 << 12);
        
        packet.dpad = 0;
        if(!digitalRead(PIN_DPAD_UP))    packet.dpad |= 0x01;
        if(!digitalRead(PIN_DPAD_DOWN))  packet.dpad |= 0x02;
        if(!digitalRead(PIN_DPAD_LEFT))  packet.dpad |= 0x04;
        if(!digitalRead(PIN_DPAD_RIGHT)) packet.dpad |= 0x08;

        packet.lx = lx8; packet.ly = ly8; packet.rx = rx8; packet.ry = ry8; packet.lt = lt8; packet.rt = rt8;

        // 🔒 V1.11 Checksum (Calculates exactly to index 14 for the 16-Byte Packet)
        uint8_t crc = 0; uint8_t* raw = (uint8_t*)&packet;
        for(int i = 2; i < 15; i++) crc ^= raw[i];
        packet.checksum = crc;

        // Push to Core 0 for instant background transmission
        xQueueOverwrite(packetQueue, &packet); 
    }
    
    // ================= BLE MODE =================
    else if (currentMode == MODE_BLE && bleGamepad.isConnected()) {
        bleGamepad.setAxes(map(lx8, 0, 255, 0, 32767), map(ly8, 0, 255, 0, 32767), map(rx8, 0, 255, 0, 32767), map(ry8, 0, 255, 0, 32767), map(lt8, 0, 255, 0, 32767), map(rt8, 0, 255, 0, 32767), map(lt8, 0, 255, 0, 32767), map(rt8, 0, 255, 0, 32767));
        
        if (!digitalRead(PIN_SHARE) && !digitalRead(PIN_CROSS)) {
            bleGamepad.press(BUTTON_14); bleGamepad.release(BUTTON_1); bleGamepad.release(BUTTON_9);
        } else {
            bleGamepad.release(BUTTON_14);
            if(!digitalRead(PIN_CROSS)) bleGamepad.press(BUTTON_1); else bleGamepad.release(BUTTON_1);
            if(!digitalRead(PIN_SHARE)) bleGamepad.press(BUTTON_9); else bleGamepad.release(BUTTON_9);
        }

        if(!digitalRead(PIN_CIRCLE))   bleGamepad.press(BUTTON_2);  else bleGamepad.release(BUTTON_2);
        if(!digitalRead(PIN_SQUARE))   bleGamepad.press(BUTTON_3);  else bleGamepad.release(BUTTON_3);
        if(!digitalRead(PIN_TRIANGLE)) bleGamepad.press(BUTTON_4);  else bleGamepad.release(BUTTON_4);
        if(!digitalRead(PIN_L1))       bleGamepad.press(BUTTON_5);  else bleGamepad.release(BUTTON_5);
        if(!digitalRead(PIN_R1))       bleGamepad.press(BUTTON_6);  else bleGamepad.release(BUTTON_6);
        if(!digitalRead(PIN_OPTIONS))  bleGamepad.press(BUTTON_10); else bleGamepad.release(BUTTON_10);
        if(!digitalRead(PIN_L3))       bleGamepad.press(BUTTON_12); else bleGamepad.release(BUTTON_12);
        if(!digitalRead(PIN_R3))       bleGamepad.press(BUTTON_11); else bleGamepad.release(BUTTON_11);
        if(!digitalRead(PIN_PS_HOME))  bleGamepad.press(BUTTON_13); else bleGamepad.release(BUTTON_13);
        
        int hat = DPAD_CENTERED; 
        bool u = !digitalRead(PIN_DPAD_UP); bool d = !digitalRead(PIN_DPAD_DOWN); bool l = !digitalRead(PIN_DPAD_LEFT); bool r = !digitalRead(PIN_DPAD_RIGHT);
        if (u && r) hat = DPAD_UP_RIGHT; else if (r && d) hat = DPAD_DOWN_RIGHT; else if (d && l) hat = DPAD_DOWN_LEFT; else if (l && u) hat = DPAD_UP_LEFT; else if (u) hat = DPAD_UP; else if (r) hat = DPAD_RIGHT; else if (d) hat = DPAD_DOWN; else if (l) hat = DPAD_LEFT;
        bleGamepad.setHat1(hat);

        bleGamepad.sendReport();
    } else if (currentMode == MODE_BLE && !bleGamepad.isConnected()) {
        static unsigned long t = 0;
        if (millis() - t > 150) { t = millis(); static bool toggle = false; toggle = !toggle; neopixelWrite(RGB_PIN, 0, 0, toggle ? 50 : 0); }
    }
    
    // ⚡ 1000Hz SPEED FIX: Feeds the Watchdog timer via FreeRTOS instead of standard Arduino delay
    vTaskDelay(pdMS_TO_TICKS(1)); 
}