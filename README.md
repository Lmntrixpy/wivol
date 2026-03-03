

# WiVol – WiFi Volume Controller

WiVol is a DIY WiFi-based volume controller built with an ESP32 and rotary encoders.  
It sends encoder data over UDP to a PC, where a Python client receives and processes the values.

The goal of this project is to control application volume levels on a Windows PC using physical rotary encoders connected to an ESP32.

---

## Disclaimer
This software was partially created using Ai.

---

## Features

- 4 rotary encoders (EC11 style, incremental)
- Push buttons integrated in each encoder
- UDP communication over WiFi
- Real-time data transmission (~30 Hz)
- Python-based PC receiver
- Easy to extend for Windows volume control integration

---

## Hardware

- ESP32 (DevKit / WROOM)
- 4× EC11 rotary encoders with push button
- Optional: 10k pull-up resistors (recommended for signal stability)
- Optional: capacitors (10–100 nF) for hardware debouncing
- Custom PCB or perfboard

### Encoder Wiring

Each encoder:

- A → GPIO (INPUT_PULLUP)
- B → GPIO (INPUT_PULLUP)
- C (Common) → GND
- SW1 → GPIO (INPUT_PULLUP)
- SW2 → GND

All encoder commons must share the same ground as the ESP32.

---

## Firmware (ESP32)

The ESP32:

1. Connects to WiFi
2. Reads encoder values using interrupts
3. Sends data via UDP to the PC

Data format sent over UDP:

E=val1,val2,val3,val4|B=btn1,btn2,btn3,btn4

Example:

E=10,0,-3,5|B=0,1,0,0

---

## PC Client (Python)

The Python script:

- Listens on UDP port 4210
- Parses incoming encoder data
- Prints formatted output to the console

### Setup

Create a virtual environment:

python -m venv venv

Activate it:

Windows:
venv\Scripts\activate

macOS/Linux:
source venv/bin/activate

Run the script:

python volumemeter.py

---

## Next Steps

- Integrate Windows Core Audio API (e.g. pycaw)
- Add per-application volume mapping
- Implement master volume mode
- Add OLED display or LED feedback
- Design a custom PCB

---

## License

This project is for educational and personal use.
