#pragma once
#include <esp_arduino_version.h>
#if ESP_ARDUINO_VERSION != ESP_ARDUINO_VERSION_VAL(3,3,12)
#error Install esp32 by Espressif Systems version 3.3.12 for this firmware.
#endif
// Synthetic mode NEVER connects to MQTT and never accesses the sensor.
#ifndef THERMAL_TEST_PATTERN
#define THERMAL_TEST_PATTERN 0
#endif
#ifndef THERMAL_ENABLE_USB
#define THERMAL_ENABLE_USB 1
#endif
// Commissioning showed large startup temperature drift. Continue displaying
// frames but hold detection in learning, then learn the settled empty scene.
#ifndef THERMAL_SENSOR_WARMUP_MS
#define THERMAL_SENSOR_WARMUP_MS 120000UL
#endif
