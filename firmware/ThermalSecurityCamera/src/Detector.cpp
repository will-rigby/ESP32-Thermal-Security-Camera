#include "Detector.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace thermal {
const char* stateName(Occupancy state) {
  switch (state) {
    case Occupancy::Learning: return "learning";
    case Occupancy::Clear: return "clear";
    case Occupancy::Occupied: return "occupied";
    default: return "unavailable";
  }
}
bool validSettings(const DetectionSettings& s) {
  return s.roi.x >= 0 && s.roi.y >= 0 && s.roi.width > 0 && s.roi.height > 0 &&
    s.roi.x < SensorWidth && s.roi.y < SensorHeight &&
    s.roi.width <= SensorWidth - s.roi.x && s.roi.height <= SensorHeight - s.roi.y &&
    std::isfinite(s.deltaC) && s.deltaC >= 0.2f && s.deltaC <= 100.f &&
    s.minPixels >= 1 && s.minPixels <= s.roi.width * s.roi.height &&
    s.largeMinPixels >= 1 && s.largeMinPixels <= PixelCount &&
    s.activateMs <= 60000 && s.clearMs <= 60000 && s.learnMs >= 1000 && s.learnMs <= 120000;
}
void Detector::configure(const DetectionSettings& s) { settings_ = s; reset(); }
void Detector::reset() {
  initialized_ = false;
  for(auto& timer:pending_) timer={};
  for(auto channel:{static_cast<RegionDetection*>(&result_),&result_.small,&result_.large}) {
    channel->state=Occupancy::Learning;channel->transition=false;
    channel->pixels=0;channel->bounds={0,0,0,0};channel->peakC=0;
  }
  std::memset(mask_, 0, sizeof(mask_));
}
Detection Detector::unavailable(uint32_t now) {
  reset();
  for(auto channel:{static_cast<RegionDetection*>(&result_),&result_.small,&result_.large}) {
    channel->state=Occupancy::Unavailable;channel->frameMs=now;
  }
  return result_;
}
Detection Detector::process(const float* f, uint32_t now, bool holdLearning) {
  if (!f || !validSettings(settings_)) return unavailable(now);
  for (int i = 0; i < PixelCount; ++i)
    if (!std::isfinite(f[i]) || f[i] < -100.f || f[i] > 1000.f) return unavailable(now);
  if (initialized_ && uint32_t(now - lastFrame_) > 1000) reset();
  const float dt = initialized_ ? std::min(uint32_t(now - lastFrame_), uint32_t(1000)) / 1000.f : 0.f;
  lastFrame_ = now;
  result_.transition = false; result_.frameMs = now;
  result_.minC = *std::min_element(f, f + PixelCount);
  result_.maxC = *std::max_element(f, f + PixelCount);
  result_.peakC = -100.f; result_.pixels = 0; result_.bounds = {0, 0, 0, 0};
  for(auto channel:{&result_.small,&result_.large}) {
    channel->transition=false;channel->frameMs=now;channel->pixels=0;
    channel->bounds={0,0,0,0};channel->peakC=-100.f;
  }
  std::memset(mask_, 0, sizeof(mask_));
  if (!initialized_) {
    std::memcpy(background_, f, sizeof(background_));
    initialized_ = true; learnStart_ = now;
    result_.state=result_.small.state=result_.large.state=Occupancy::Learning;
  }
  if (holdLearning) {
    learnStart_=now;
    result_.state=result_.small.state=result_.large.state=Occupancy::Learning;
    for(auto& timer:pending_) timer={};
  }
  if (holdLearning || uint32_t(now - learnStart_) < settings_.learnMs) {
    const float alpha = std::min(1.f, dt / 2.f);
    for (int i = 0; i < PixelCount; ++i) background_[i] += alpha * (f[i] - background_[i]);
    return result_;
  }
  if (result_.state == Occupancy::Learning) result_.state = Occupancy::Clear;
  if (result_.small.state == Occupancy::Learning) result_.small.state = Occupancy::Clear;
  if (result_.large.state == Occupancy::Learning) result_.large.state = Occupancy::Clear;
  const Roi& r = settings_.roi;
  const float alpha = std::min(1.f, dt / 60.f);
  for (int y = 0; y < SensorHeight; ++y) for (int x = 0; x < SensorWidth; ++x) {
    const int i = y * SensorWidth + x;
    const bool inside = x >= r.x && x < r.x+r.width && y >= r.y && y < r.y+r.height;
    if (inside) result_.peakC = std::max(result_.peakC, f[i]);
    if (inside && f[i] - background_[i] >= settings_.deltaC) mask_[i] = 1;
    else background_[i] += alpha * (f[i] - background_[i]);
  }
  // Eight-connected components; mark on enqueue to bound the queue to PixelCount.
  for (int start = 0; start < PixelCount; ++start) {
    if (mask_[start] != 1) continue;
    size_t head = 0, tail = 1;
    work_[0] = uint16_t(start); mask_[start] = 2;
    int x0 = start % SensorWidth, x1 = x0, y0 = start / SensorWidth, y1 = y0;
    float peak=-100.f;
    while (head < tail) {
      const int i = work_[head++], x = i % SensorWidth, y = i / SensorWidth;
      peak=std::max(peak,f[i]);
      x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
      for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
        const int nx = x + dx, ny = y + dy;
        if (nx < r.x || nx >= r.x+r.width || ny < r.y || ny >= r.y+r.height) continue;
        const int ni = ny * SensorWidth + nx;
        if (mask_[ni] == 1) { mask_[ni] = 2; work_[tail++] = uint16_t(ni); }
      }
    }
    if (tail >= settings_.minPixels) {
      auto& channel=tail>=settings_.largeMinPixels?result_.large:result_.small;
      channel.peakC=std::max(channel.peakC,peak);
      if(tail>channel.pixels) {
        channel.pixels=uint16_t(tail);channel.bounds={x0,y0,x1-x0+1,y1-y0+1};
      }
      if (tail > result_.pixels) {
        result_.pixels = uint16_t(tail); result_.bounds = {x0, y0, x1-x0+1, y1-y0+1};
      }
      for (size_t k = 0; k < tail; ++k) mask_[work_[k]] = 3;
    }
  }
  update(result_,pending_[0],result_.pixels>=settings_.minPixels,now);
  update(result_.small,pending_[1],result_.small.pixels>0,now);
  update(result_.large,pending_[2],result_.large.pixels>0,now);
  return result_;
}
void Detector::update(RegionDetection& channel,Debounce& timer,bool present,uint32_t now) {
  const bool occupied = channel.state == Occupancy::Occupied;
  if (present == occupied) timer.pending = false;
  else {
    if (!timer.pending) { timer.pending = true; timer.since = now; }
    if (uint32_t(now - timer.since) >= (present ? settings_.activateMs : settings_.clearMs)) {
      channel.state = present ? Occupancy::Occupied : Occupancy::Clear;
      channel.transition = true; channel.eventMs = now; ++channel.sequence; timer.pending = false;
    }
  }
}
}
