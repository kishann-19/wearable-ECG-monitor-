#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

// PROCESSOR_BEGIN: independent of Arduino, exercised by host tests.
struct ECGProcessor {
  enum State { LEADS_OFF, SETTLING, TRACKING, NO_BEATS, ADC_LIMIT };
  State state = LEADS_OFF;
  float baseline = 0, fir[5] = {}, filtered = 0, level = 0;
  float candidate = 0, bpm = 0;
  uint32_t warmStart = 0, candidateTime = 0, lastPeak = 0;
  uint32_t lastSeenPeak = 0;
  int firPos = 0, rrCount = 0, rrPos = 0;
  float rr[4] = {};
  bool active = false, tracking = false, havePeak = false, beat = false;

  void invalidate(State reason) {
    active = false; bpm = 0; filtered = 0; beat = false;
    havePeak = false; tracking = false; state = reason;
  }
  void process(int raw, bool disconnected, uint32_t now) {
    beat = false;
    if (disconnected) { invalidate(LEADS_OFF); return; }
    if (raw < 20 || raw > 4075) { invalidate(ADC_LIMIT); return; }
    if (!active) {
      baseline = raw; memset(fir, 0, sizeof(fir)); firPos = 0;
      level = 0; warmStart = now; bpm = 0; rrCount = 0; rrPos = 0;
      tracking = false; havePeak = false; active = true;
    }
    baseline += (raw - baseline) / 128.0f;
    fir[firPos] = raw - baseline; firPos = (firPos + 1) % 5;
    filtered = 0; for (float v : fir) filtered += v;
    filtered /= 5.0f;
    const float magnitude = fabsf(filtered);
    if (now - warmStart < 3000) {
      level = fmaxf(level, magnitude); state = SETTLING; return;
    }
    state = TRACKING;
    if (havePeak && now - lastSeenPeak > 3000) {
      bpm = 0; havePeak = false; rrCount = 0; rrPos = 0; tracking = false;
    }
    if (!havePeak || bpm == 0) state = NO_BEATS;
    // Slowly recover after a large artifact; this is not a validated quality gate.
    level = fmaxf(120.0f, level * 0.9995f);
    const float threshold = fmaxf(60.0f, level * 0.5f);
    if (!tracking) {
      if (magnitude > threshold && (!havePeak || now - lastPeak >= 250)) {
        tracking = true; candidate = magnitude; candidateTime = now;
      }
      return;
    }
    if (magnitude > candidate) { candidate = magnitude; candidateTime = now; }
    if (now - candidateTime < 100) return;
    tracking = false; level = .875f * level + .125f * candidate;
    if (havePeak) {
      uint32_t interval = candidateTime - lastPeak;
      if (interval >= 250 && interval <= 2400) {
        rr[rrPos] = interval; rrPos = (rrPos + 1) % 4;
        if (rrCount < 4) rrCount++;
        float mean = 0; for (int i = 0; i < rrCount; i++) mean += rr[i];
        bpm = 60000.0f / (mean / rrCount);
      } else { bpm = 0; rrCount = 0; rrPos = 0; }
    }
    lastPeak = candidateTime; lastSeenPeak = now; havePeak = true; beat = true;
    state = bpm > 0 ? TRACKING : NO_BEATS;
  }
};
// PROCESSOR_END
