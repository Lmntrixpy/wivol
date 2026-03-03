/*
  ESP32 -> PC via WiFi (UDP)
  Sends 4 encoder values + 4 button events as one line: E1,E2,E3,E4|B1,B2,B3,B4
  Encoder 5 disabled.

  Wiring (example, adjust if you changed):
  Enc1: A=16 B=17 SW=18
  Enc2: A=19 B=21 SW=22
  Enc3: A=23 B=25 SW=26
  Enc4: A=27 B=32 SW=33

  All encoder commons (C) and one side of each button to GND.

  const int pinA[NUM_ENCODERS]  = {16, 19, 23, 27, 34};
  const int pinB[NUM_ENCODERS]  = {17, 21, 25, 32, 35};
  const int pinSW[NUM_ENCODERS] = {13, 33, 26, 22, 18};

*/

#include <WiFi.h>
#include <WiFiUdp.h>

#define NUM_ENCODERS 4

// ====== WiFi / UDP CONFIG ======
#include "wifi_secrets.h"

// Set this to your PC's IP address on your LAN (e.g. 192.168.178.50)
IPAddress PC_IP(192, 168, 178, 171);
const uint16_t PC_PORT = 4210;

// Optional: local port (any)
const uint16_t LOCAL_PORT = 4211;

WiFiUDP udp;

// ====== PIN MAPPING (Encoder 5 removed) ======
const int pinA[NUM_ENCODERS]  = {16, 19, 23, 27};
const int pinB[NUM_ENCODERS]  = {17, 21, 25, 32};
const int pinSW[NUM_ENCODERS] = {13, 33, 26, 22};

// ====== ENCODER STATE ======
volatile long encoderValue[NUM_ENCODERS] = {0};
volatile uint8_t lastState[NUM_ENCODERS] = {0};
volatile uint32_t lastUs[NUM_ENCODERS] = {0};

// Button latch (sent as 1 once, then cleared)
bool btnLatched[NUM_ENCODERS] = {0};
bool lastBtnRead[NUM_ENCODERS] = {0};

// Robust quadrature table
static const int8_t qdecTable[16] = {
  0, -1, +1, 0,
  +1, 0, 0, -1,
  -1, 0, 0, +1,
  0, +1, -1, 0
};

void IRAM_ATTR updateEncoder(int i);

// ISR wrappers
void IRAM_ATTR isr0() { updateEncoder(0); }
void IRAM_ATTR isr1() { updateEncoder(1); }
void IRAM_ATTR isr2() { updateEncoder(2); }
void IRAM_ATTR isr3() { updateEncoder(3); }

void IRAM_ATTR updateEncoder(int i) {
  uint32_t now = micros();
  // time filter against bounce/noise (no capacitors)
  if (now - lastUs[i] < 800) return;  // adjust 500..2000 if needed
  lastUs[i] = now;

  uint8_t a = (uint8_t)digitalRead(pinA[i]);
  uint8_t b = (uint8_t)digitalRead(pinB[i]);
  uint8_t state = (a << 1) | b;

  uint8_t idx = (lastState[i] << 2) | state;
  int8_t delta = qdecTable[idx];

  if (delta != 0) encoderValue[i] += delta;
  lastState[i] = state;
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());
}

void setup() {
  Serial.begin(115200);
  delay(200);

  for (int i = 0; i < NUM_ENCODERS; i++) {
    pinMode(pinA[i], INPUT_PULLUP);
    pinMode(pinB[i], INPUT_PULLUP);
    pinMode(pinSW[i], INPUT_PULLUP);

    lastState[i] = ((uint8_t)digitalRead(pinA[i]) << 1) | (uint8_t)digitalRead(pinB[i]);
    lastUs[i] = micros();
    lastBtnRead[i] = digitalRead(pinSW[i]);
  }

  attachInterrupt(digitalPinToInterrupt(pinA[0]), isr0, CHANGE);
  attachInterrupt(digitalPinToInterrupt(pinB[0]), isr0, CHANGE);

  attachInterrupt(digitalPinToInterrupt(pinA[1]), isr1, CHANGE);
  attachInterrupt(digitalPinToInterrupt(pinB[1]), isr1, CHANGE);

  attachInterrupt(digitalPinToInterrupt(pinA[2]), isr2, CHANGE);
  attachInterrupt(digitalPinToInterrupt(pinB[2]), isr2, CHANGE);

  attachInterrupt(digitalPinToInterrupt(pinA[3]), isr3, CHANGE);
  attachInterrupt(digitalPinToInterrupt(pinB[3]), isr3, CHANGE);

  connectWiFi();
  udp.begin(LOCAL_PORT);

  Serial.print("Sending UDP to ");
  Serial.print(PC_IP);
  Serial.print(":");
  Serial.println(PC_PORT);
}

void loop() {
  // Button edge detect (latched)
  for (int i = 0; i < NUM_ENCODERS; i++) {
    bool cur = digitalRead(pinSW[i]);
    if (cur == LOW && lastBtnRead[i] == HIGH) btnLatched[i] = true;
    lastBtnRead[i] = cur;
  }

  // Send packet at fixed rate
  static uint32_t lastSendMs = 0;
  if (millis() - lastSendMs >= 30) {  // ~33 Hz
    lastSendMs = millis();

    // Build one compact line
    // Example: E=10,0,-3,5|B=0,1,0,0
    char buf[128];
    snprintf(buf, sizeof(buf),
             "E=%ld,%ld,%ld,%ld|B=%d,%d,%d,%d",
             encoderValue[0], encoderValue[1], encoderValue[2], encoderValue[3],
             btnLatched[0] ? 1 : 0, btnLatched[1] ? 1 : 0, btnLatched[2] ? 1 : 0, btnLatched[3] ? 1 : 0);

    udp.beginPacket(PC_IP, PC_PORT);
    udp.write((const uint8_t*)buf, strlen(buf));
    udp.endPacket();

    // clear button latches after sending
    for (int i = 0; i < NUM_ENCODERS; i++) btnLatched[i] = false;
  }
}