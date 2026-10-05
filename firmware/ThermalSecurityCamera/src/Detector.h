#pragma once
#include <stdint.h>
#include <stddef.h>
namespace thermal {
constexpr int SensorWidth = 80, SensorHeight = 62, PixelCount = 4960;
struct Roi { int x = 0, y = 0, width = 80, height = 62; };
struct DetectionSettings {
  Roi roi;
  float deltaC = 3.0f;
  uint16_t minPixels = 4;
  uint16_t largeMinPixels = 16;
  uint32_t activateMs = 500, clearMs = 2000, learnMs = 10000;
};
enum class Occupancy : uint8_t { Unavailable, Learning, Clear, Occupied };
const char* stateName(Occupancy state);
bool validSettings(const DetectionSettings& settings);
struct RegionDetection {
  Occupancy state = Occupancy::Unavailable;
  bool transition = false;
  uint32_t sequence = 0, frameMs = 0, eventMs = 0;
  float peakC = 0;
  uint16_t pixels = 0;
  Roi bounds{0, 0, 0, 0};
};
struct Detection : RegionDetection {
  float minC = 0, maxC = 0;
  RegionDetection small,large;
};
enum class ObjectClass : uint8_t { Any, Small, Large };
inline const char* className(ObjectClass c) { return c==ObjectClass::Small?"small":c==ObjectClass::Large?"large":"any"; }
inline const RegionDetection& classDetection(const Detection& d,ObjectClass c) {
  return c==ObjectClass::Small?d.small:c==ObjectClass::Large?d.large:static_cast<const RegionDetection&>(d);
}
// Pure C++: host tests run this exact production algorithm.
class Detector {
 public:
  void configure(const DetectionSettings& settings);
  void reset();
  Detection process(const float* frame, uint32_t now, bool holdLearning=false);
  Detection unavailable(uint32_t now);
  const uint8_t* mask() const { return mask_; }
 private:
  DetectionSettings settings_;
  Detection result_;
  float background_[PixelCount]{};
  uint8_t mask_[PixelCount]{};
  uint16_t work_[PixelCount]{};
  struct Debounce { uint32_t since=0; bool pending=false; };
  Debounce pending_[3];
  void update(RegionDetection& channel,Debounce& timer,bool present,uint32_t now);
  uint32_t learnStart_ = 0, lastFrame_ = 0;
  bool initialized_ = false;
};
}
