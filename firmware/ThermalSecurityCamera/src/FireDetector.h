#pragma once
#include "Detector.h"
#include <cmath>

namespace thermal {
struct FireSettings {
  bool enabled = false;
  float thresholdC = 80.f;
  uint32_t activateMs = 10000, clearMs = 5000;
};
inline bool validSettings(const FireSettings& s) {
  return std::isfinite(s.thresholdC) && s.thresholdC >= 0 && s.thresholdC <= 1000 &&
    s.activateMs >= 1 && s.activateMs <= 600000 && s.clearMs <= 600000;
}
enum class FireState : uint8_t { Disabled, Unavailable, Clear, Detected };
inline const char* stateName(FireState s) {
  switch(s) {
    case FireState::Disabled: return "disabled";
    case FireState::Clear: return "clear";
    case FireState::Detected: return "detected";
    default: return "unavailable";
  }
}
struct FireDetection {
  FireState state = FireState::Disabled;
  FireSettings settings;
  uint32_t sequence = 0, frameMs = 0, eventMs = 0;
  float peakC = NAN;
  bool transition = false;
  bool eventValid = false;
};
// Independent of occupancy/background learning and the rendered image.
class FireDetector {
 public:
  void configure(const FireSettings& settings) {
    result_.settings=settings;
    unavailable(result_.frameMs);
  }
  FireDetection unavailable(uint32_t now) {
    result_.state=result_.settings.enabled?FireState::Unavailable:FireState::Disabled;
    result_.peakC=NAN;result_.frameMs=now;result_.transition=false;result_.eventValid=false;
    pending_=false;haveFrame_=false;
    return result_;
  }
  FireDetection process(const float* frame,uint32_t now,bool warming=false) {
    if(!result_.settings.enabled || warming || !frame || !validSettings(result_.settings)) return unavailable(now);
    float peak=-100.f;
    for(int i=0;i<PixelCount;++i) {
      if(!std::isfinite(frame[i]) || frame[i]<-100.f || frame[i]>1000.f) return unavailable(now);
      if(frame[i]>peak) peak=frame[i];
    }
    if(haveFrame_ && uint32_t(now-result_.frameMs)>1000) return unavailable(now);
    haveFrame_=true;result_.frameMs=now;result_.peakC=peak;result_.transition=false;
    if(result_.state==FireState::Unavailable) result_.state=FireState::Clear;
    const bool hot=peak>result_.settings.thresholdC;
    if(hot==(result_.state==FireState::Detected)) pending_=false;
    else {
      if(!pending_) { pending_=true;since_=now; }
      const auto delay=hot?result_.settings.activateMs:result_.settings.clearMs;
      if(uint32_t(now-since_)>=delay) {
        result_.state=hot?FireState::Detected:FireState::Clear;
        result_.transition=true;result_.eventValid=true;result_.eventMs=now;++result_.sequence;pending_=false;
      }
    }
    return result_;
  }
 private:
  FireDetection result_;
  bool pending_=false,haveFrame_=false;
  uint32_t since_=0;
};
}
