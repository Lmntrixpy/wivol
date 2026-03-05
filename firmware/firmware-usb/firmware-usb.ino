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

#define NUM_ENCODERS 4

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

const int ENC_DIVIDER = 2;   // 4 = typisch pro Rastung, ggf. 2 oder 1 je nach Encoder
long encOut[NUM_ENCODERS] = {0};

static long offset[NUM_ENCODERS] = {0};
static int  startVol[NUM_ENCODERS] = {50, 50, 50, 50};

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

void handleLine(const String& line) {
  if (!line.startsWith("V=")) return;

  int v[NUM_ENCODERS] = {50, 50, 50, 50};
  int idx = 0;
  int last = 2;

  for (int i = 2; i <= (int)line.length() && idx < NUM_ENCODERS; i++) {
    if (i == (int)line.length() || line[i] == ',') {
      v[idx++] = line.substring(last, i).toInt();
      last = i + 1;
    }
  }

  noInterrupts();
  for (int i = 0; i < NUM_ENCODERS; i++) {
    startVol[i] = constrain(v[i], 0, 100);
    const long targetRaw = (long)startVol[i] * ENC_DIVIDER;
    offset[i] = targetRaw - encoderValue[i];
  }
  interrupts();
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

  Serial.println("HELLO");
}

void loop() {
  // use data from pc
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) handleLine(line);
  }

  // Button edge detect (latched)
  for (int i = 0; i < NUM_ENCODERS; i++) {
    bool cur = digitalRead(pinSW[i]);
    if (cur == LOW && lastBtnRead[i] == HIGH) btnLatched[i] = true;
    lastBtnRead[i] = cur;
  }

  long e[NUM_ENCODERS];
  bool b[NUM_ENCODERS];

  noInterrupts();
  for (int i = 0; i < NUM_ENCODERS; i++) {
    long raw = encoderValue[i] + offset[i];
    e[i] = raw / ENC_DIVIDER;
    b[i] = btnLatched[i];
    btnLatched[i] = false;
  }
  interrupts();

  Serial.printf("E=%ld,%ld,%ld,%ld|B=%d,%d,%d,%d\n",
                e[0], e[1], e[2], e[3],
                b[0] ? 1 : 0, b[1] ? 1 : 0, b[2] ? 1 : 0, b[3] ? 1 : 0);

  delay(10);
}