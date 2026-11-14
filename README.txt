ECG + OLED monitor for ESP32 Dev Module

1. Extract the ZIP. Keep ECG_OLED_Monitor.ino and ECGProcessor.h together in the ECG_OLED_Monitor folder.
2. Open that .ino file in Arduino IDE as a new sketch. Do not paste it into your old sketch.
3. Install Adafruit SSD1306 and Adafruit GFX Library using Library Manager, including dependencies.
4. Select ESP32 Dev Module and your board port, then click Verify and Upload.

Wiring:
AD8232: 3.3V -> 3V3; GND -> GND; OUTPUT -> GPIO34; LO+ -> GPIO32; LO- -> GPIO33; SDN -> 3V3.
OLED: VCC -> 3V3; GND -> GND; SDA -> GPIO21; SCL/SCK -> GPIO22.
Optional LED: GPIO25 -> 330 ohm resistor -> anode; cathode -> GND.
Buzzer not required. OLED assumed SSD1306 128x64; address detected at 0x3C or 0x3D.

Keep electrodes disconnected from people during USB upload/testing. Human-connected testing requires a reviewed isolated battery setup, disconnected from USB, charging and mains-connected instruments.

Shows waveform and experimental estimated BPM. This is not a diagnostic or validated abnormal-heartbeat detector. With electrodes unplugged, CHECK ELECTRODES is expected.
