/* ESP32-WROOM + AD8232 + SSD1306 128x64 I2C OLED.
   Educational waveform / approximate BPM monitor, NOT an arrhythmia classifier.
   During USB-powered tests, keep electrodes disconnected from people.
   Human-connected testing requires a reviewed, isolated battery-powered setup,
   disconnected from USB, charging and mains-connected instruments.
   The MPU6050 may remain on I2C but is not read in this first monitor sketch.
*/
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "ECGProcessor.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

constexpr int ECG_PIN = 34;
constexpr int LO_PLUS_PIN = 32;
constexpr int LO_MINUS_PIN = 33;
constexpr int LED_PIN = 25; // Brief heartbeat blink, NOT an abnormality alert.
constexpr int SDA_PIN = 21;
constexpr int SCL_PIN = 22;
constexpr int SAMPLE_MS = 4;
constexpr int HISTORY = 768; // 3.072 s at nominal 250 Hz
constexpr int SCREEN_H = 64;
// Fixed display scale in ADC counts per vertical pixel, not calibrated ECG mV.
constexpr float COUNTS_PER_PIXEL = 35.0f;

Adafruit_SSD1306 display(128, SCREEN_H, &Wire, -1, 100000, 100000);



struct MonitorSnapshot {
  int16_t trace[HISTORY];
  unsigned head, filled;
  float bpm;
  ECGProcessor::State state;
  uint32_t timingGaps;
};
MonitorSnapshot sharedData = {};
MonitorSnapshot viewData = {};
portMUX_TYPE dataLock = portMUX_INITIALIZER_UNLOCKED;

bool responds(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

void sampleECG(void *) {
  ECGProcessor processor;
  TickType_t wake = xTaskGetTickCount();
  uint32_t previous = millis(), ledUntil = 0, gaps = 0;
  bool first = true;
  for (;;) {
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(SAMPLE_MS));
    const uint32_t now = millis();
    const uint32_t elapsed = now - previous;
    previous = now;
    if (!first && (elapsed < 2 || elapsed > 8)) {
      // Do not infer intervals over a sampling discontinuity or fabricate catch-up data.
      gaps++; processor.invalidate(ECGProcessor::SETTLING);
      wake = xTaskGetTickCount();
    }
    first = false;
    const bool off = digitalRead(LO_PLUS_PIN) || digitalRead(LO_MINUS_PIN);
    const int raw = analogRead(ECG_PIN);
    processor.process(raw, off, now);
    if (processor.beat) ledUntil = now + 80;
    const bool light = processor.state == ECGProcessor::TRACKING && (int32_t)(ledUntil - now) > 0;
    digitalWrite(LED_PIN, light ? HIGH : LOW);

    portENTER_CRITICAL(&dataLock);
    sharedData.trace[sharedData.head] = (int16_t)processor.filtered;
    sharedData.head = (sharedData.head + 1) % HISTORY;
    if (sharedData.filled < HISTORY) sharedData.filled++;
    sharedData.bpm = processor.bpm; sharedData.state = processor.state;
    sharedData.timingGaps = gaps;
    portEXIT_CRITICAL(&dataLock);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, LOW);
  pinMode(LO_PLUS_PIN, INPUT); pinMode(LO_MINUS_PIN, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(ECG_PIN, ADC_11db);
  delay(500);
  if (!Wire.begin(SDA_PIN, SCL_PIN, 100000)) {
    Serial.println("I2C initialization failed."); while (true) delay(1000);
  }
  Wire.setTimeOut(50);
  const uint8_t address = responds(0x3C) ? 0x3C : (responds(0x3D) ? 0x3D : 0);
  if (!address || !display.begin(SSD1306_SWITCHCAPVCC, address, true, false)) {
    Serial.println("OLED unavailable. Run OLED_Test and check connections.");
    while (true) delay(1000);
  }
  display.clearDisplay(); display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1); display.setCursor(0, 0);
  display.println("ECG waveform monitor"); display.println("Starting..."); display.display();
  if (pdMS_TO_TICKS(SAMPLE_MS) == 0 || 1000 % configTICK_RATE_HZ != 0 ||
      (pdMS_TO_TICKS(SAMPLE_MS) * 1000 / configTICK_RATE_HZ) != SAMPLE_MS) {
    Serial.println("RTOS tick cannot represent a 4ms sampling period.");
    while (true) delay(1000);
  }
  if (xTaskCreatePinnedToCore(sampleECG, "ECG sample", 4096, nullptr, 3, nullptr, 0) != pdPASS) {
    Serial.println("ECG sampling task could not start."); while (true) delay(1000);
  }
  Serial.println("Real ADC mode. GPIO34 ECG; GPIO32/33 lead-off; GPIO25 beat indicator.");
  Serial.println("No rhythm diagnosis or abnormal-beat alarm in this sketch.");
}

void loop() {
  // Short shared-memory copy; OLED I2C work happens outside the sampling lock.
  portENTER_CRITICAL(&dataLock);
  memcpy(&viewData, &sharedData, sizeof(viewData));
  portEXIT_CRITICAL(&dataLock);
  display.clearDisplay(); display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0); display.print("ECG  BPM:");
  if (viewData.bpm > 0) display.print((int)lroundf(viewData.bpm)); else display.print("--");
  display.setCursor(0, 11);
  switch (viewData.state) {
    case ECGProcessor::LEADS_OFF: display.print("CHECK ELECTRODES"); break;
    case ECGProcessor::SETTLING: display.print("SETTLING... KEEP STILL"); break;
    case ECGProcessor::ADC_LIMIT: display.print("ADC LIMIT / CHECK IN"); break;
    case ECGProcessor::NO_BEATS: display.print("NO RELIABLE BPM"); break;
    default: display.print("ESTIMATE / NOT DIAG"); break;
  }
  display.drawFastHLine(0, 22, 128, SSD1306_WHITE);
  if (viewData.state != ECGProcessor::LEADS_OFF && viewData.state != ECGProcessor::ADC_LIMIT) {
    const unsigned oldest = (viewData.head + HISTORY - viewData.filled) % HISTORY;
    // Six samples per column, plotting the min/max to retain narrow peaks.
    for (unsigned x = 0; x < 128; x++) {
      const unsigned begin = x * 6;
      if (begin >= viewData.filled) break;
      int low = 32767, high = -32768;
      for (unsigned j = begin; j < begin + 6 && j < viewData.filled; j++) {
        int value = viewData.trace[(oldest + j) % HISTORY];
        if (value < low) low = value; if (value > high) high = value;
      }
      int yTop = constrain((int)lroundf(43 - high / COUNTS_PER_PIXEL), 24, 63);
      int yBottom = constrain((int)lroundf(43 - low / COUNTS_PER_PIXEL), 24, 63);
      display.drawFastVLine(x, yTop, yBottom - yTop + 1, SSD1306_WHITE);
    }
  }
  display.display();
  static uint32_t lastReport = 0;
  if (millis() - lastReport >= 2000) {
    lastReport = millis();
    Serial.printf("Estimated BPM=%.1f | state=%d | timing gaps=%lu\n", viewData.bpm,
                  (int)viewData.state, (unsigned long)viewData.timingGaps);
  }
  delay(25); // OLED updates are slower than sampling, which runs in its own task.
}
