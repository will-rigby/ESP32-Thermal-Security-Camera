#pragma once
#include <Arduino.h>
class WaveshareSenxor {
 public:
  static constexpr int Width = 80, Height = 62, Pixels = Width * Height;
  bool begin();                 // Call from the acquisition task on core 1.
  bool read(float* celsius);    // False means no new calibrated frame.
  void pause();                // Core 1 only, before the application writes NVS.
  void resume();               // Discards the interrupted frame; keeps calibration.
  void requestRecovery();      // Core 1 only; manual diagnostic, no flash writes.
  const char* error() const;
  uint32_t errors() const;
  uint32_t recoveries() const;
  uint32_t lastErrorCode() const;
  bool recovering() const;
};
