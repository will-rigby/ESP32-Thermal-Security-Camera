#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <freertos/semphr.h>
#include <cJSON.h>
#include "Detector.h"
#include "Palette.h"
namespace thermal {
struct Config {
  char ssid[33]{}, wifiPassword[65]{};
  char broker[128]{}, mqttUser[65]{}, mqttPassword[129]{}, topic[129]{};
  uint16_t port = 1883;
  bool flipHorizontal = false;
  Palette palette = Palette::WhiteHot;
  bool haDiscovery = true;
  DetectionSettings detection;
  uint32_t revision = 1;
};
class ConfigStore {
 public:
  bool begin(const char* deviceId);
  Config get();
  String json(bool secrets = false);
  bool update(const char* json, String& error);
  void setWriteGuard(bool (*before)(),void (*after)()) { beforeWrite_=before;afterWrite_=after; }
  bool loaded() const { return loaded_; }
  const char* loadError() const { return loadError_.c_str(); }
 private:
  Config value_;
  SemaphoreHandle_t mutex_ = nullptr;
  SemaphoreHandle_t updateMutex_ = nullptr;
  bool (*beforeWrite_)() = nullptr;
  void (*afterWrite_)() = nullptr;
  Preferences prefs_;
  bool ready_ = false;
  bool loaded_ = false;
  String loadError_ = "no saved configuration";
};
String configJson(const Config& config, bool secrets);
bool parseConfig(const char* json, Config& config, String& error);
}
