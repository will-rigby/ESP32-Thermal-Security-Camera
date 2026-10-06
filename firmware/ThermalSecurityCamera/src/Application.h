#pragma once
#include "Config.h"
#include <atomic>
namespace thermal {
struct Frame {
  float pixels[PixelCount];
  uint8_t mask[PixelCount];
  Detection detection;
  Roi roi;
  uint32_t serial = 0;
  bool flipHorizontal = false;
  Palette palette = Palette::WhiteHot;
};
struct Status {
  Detection detection;
  Roi roi;
  uint32_t frameSerial = 0, sensorErrors = 0, lastFrameMs = 0, bootId = 0;
  uint32_t sensorWarmupMs = 0;
  uint32_t sensorRecoveries = 0, sensorLastErrorCode = 0, sensorConfigPauses = 0;
  bool sensorRecovering = false;
  float fps = 0;
  bool sensorReady = false;
  bool flipHorizontal = false;
  Palette palette = Palette::WhiteHot;
  char sensorError[96] = "starting";
};
extern ConfigStore configStore;
extern char deviceId[20];
extern std::atomic<bool> mqttConnected, usbStreaming;
extern std::atomic<uint32_t> relearnGeneration;
extern std::atomic<bool> sensorRecoveryRequested;
Status getStatus();
bool copyFrame(Frame& target, uint32_t afterSerial);
String statusJson();
String detectionJson(const Status& status, bool event,ObjectClass objectClass=ObjectClass::Any);
void startApplication();
}
