#include "../firmware/ThermalSecurityCamera/src/FireDetector.h"
#include "../firmware/ThermalSecurityCamera/src/EventCursor.h"
#include <array>
#include <cassert>
#include <iostream>
#include <limits>
using namespace thermal;
struct Scene {
  std::array<float,PixelCount> pixels;
  FireSettings settings;
  FireDetector detector;
  uint32_t now=0;
  Scene() { pixels.fill(20);settings.enabled=true;settings.activateMs=1000;settings.clearMs=500;detector.configure(settings); }
  FireDetection step(uint32_t ms=100,bool warming=false) { now+=ms;return detector.process(pixels.data(),now,warming); }
  FireDetection hold(unsigned steps) { FireDetection d;while(steps--) d=step();return d; }
};
int main() {
  {
    FireSettings s;assert(!s.enabled&&s.thresholdC==80&&s.activateMs==10000&&s.clearMs==5000&&validSettings(s));
    s.thresholdC=NAN;assert(!validSettings(s));s.thresholdC=1001;assert(!validSettings(s));
    s.thresholdC=-1;assert(!validSettings(s));s.thresholdC=0;assert(validSettings(s));
    s.activateMs=0;assert(!validSettings(s));s.activateMs=600001;assert(!validSettings(s));
    s.activateMs=600000;s.clearMs=600001;assert(!validSettings(s));s.clearMs=0;assert(validSettings(s));
    FireDetector d;assert(d.process(nullptr,0).state==FireState::Disabled);
  }
  {
    Scene s;s.pixels[0]=80;assert(s.hold(30).state==FireState::Clear); // Equality is not hot.
    s.pixels[0]=81;assert(s.hold(10).sequence==0);
    s.pixels[0]=80;assert(s.step().sequence==0); // Activation interrupted.
    s.pixels[0]=81;assert(s.hold(10).state==FireState::Clear);
    auto d=s.step();assert(d.state==FireState::Detected&&d.transition&&d.sequence==1&&d.eventMs==s.now);
    d=s.hold(30);assert(d.state==FireState::Detected&&!d.transition&&d.sequence==1);
    s.pixels[0]=80;assert(s.hold(5).state==FireState::Detected);
    s.pixels[0]=81;assert(s.step().state==FireState::Detected); // Clear interrupted.
    s.pixels[0]=80;assert(s.hold(5).state==FireState::Detected);
    d=s.step();assert(d.state==FireState::Clear&&d.transition&&d.sequence==2&&d.peakC==80);
    s.pixels[0]=81;d=s.hold(11);assert(d.state==FireState::Detected&&d.sequence==3);
  }
  {
    Scene s;s.settings.clearMs=0;s.detector.configure(s.settings);
    s.pixels[0]=100;assert(s.hold(11).state==FireState::Detected);
    s.pixels[0]=20;assert(s.step().state==FireState::Clear);
  }
  {
    Scene s;s.pixels[0]=90;s.hold(5);
    s.pixels[0]=20;s.pixels[PixelCount-1]=95;auto d=s.hold(6);
    assert(d.state==FireState::Detected&&d.peakC==95); // Hottest pixel may move anywhere.
    Detector occupancy;DetectionSettings ds;ds.roi={20,20,4,4};occupancy.configure(ds);
    for(int i=0;i<20;++i) {
      occupancy.reset();assert(occupancy.process(s.pixels.data(),s.now).state==Occupancy::Learning);
      assert(s.step().state==FireState::Detected); // ROI and relearning do not touch fire.
    }
  }
  {
    Scene s;s.pixels.fill(150);
    for(int i=0;i<1200;++i) { auto d=s.step(100,true);assert(d.state==FireState::Unavailable&&d.sequence==0&&std::isnan(d.peakC)); }
    assert(s.hold(10).state==FireState::Clear);assert(s.step().state==FireState::Detected);
    assert(s.step(100,true).state==FireState::Unavailable);
    assert(s.hold(10).sequence==1);assert(s.step().sequence==2);
  }
  {
    Scene s;s.pixels[0]=90;assert(s.hold(11).state==FireState::Detected);
    auto d=s.step(1001);assert(d.state==FireState::Unavailable&&!d.eventValid&&d.sequence==1);
    assert(s.hold(10).state==FireState::Clear);assert(s.step().sequence==2);
    for(float bad:{NAN,std::numeric_limits<float>::infinity(),-101.f,1001.f}) {
      s.pixels[1]=bad;d=s.step();assert(d.state==FireState::Unavailable&&!d.transition&&!d.eventValid&&std::isnan(d.peakC));
      s.pixels[1]=20;assert(s.hold(10).state==FireState::Clear);assert(s.step().state==FireState::Detected);
    }
    assert(s.detector.process(nullptr,s.now).state==FireState::Unavailable);
  }
  {
    Scene s;s.pixels[0]=90;s.hold(11);
    auto sequence=s.detector.unavailable(s.now).sequence;
    s.settings.enabled=false;s.detector.configure(s.settings);
    auto d=s.step();assert(d.state==FireState::Disabled&&d.sequence==sequence&&!d.eventValid);
    s.settings.enabled=true;s.detector.configure(s.settings);
    assert(s.hold(10).state==FireState::Clear);assert(s.step().sequence==sequence+1);
    s.settings.thresholdC=95;s.detector.configure(s.settings);
    d=s.step();assert(d.state==FireState::Clear&&!d.eventValid&&d.sequence==sequence+1);
  }
  {
    Scene s;s.now=UINT32_MAX-500;s.pixels[0]=90;
    assert(s.hold(10).state==FireState::Clear);assert(s.step().state==FireState::Detected);
    s.pixels[0]=20;assert(s.hold(6).state==FireState::Clear);
  }
  {
    Scene s;EventCursor events;events.connect(0);s.pixels[0]=90;auto d=s.hold(11);
    assert(events.consume(d.sequence,d.eventValid,d.eventMs,s.now));
    d=s.step();assert(!events.consume(d.sequence,d.eventValid,d.eventMs,s.now));
    s.pixels[0]=20;d=s.hold(6);events.connect(d.sequence); // Offline edge is not replayed.
    assert(!events.consume(d.sequence,d.eventValid,d.eventMs,s.now));
    s.pixels[0]=90;s.hold(11);s.detector.unavailable(s.now);d=s.step();
    assert(!events.consume(d.sequence,d.eventValid,d.eventMs,s.now)); // No invented clear after a fault.
    d=s.hold(10);assert(events.consume(d.sequence,d.eventValid,d.eventMs,s.now));
    s.pixels[0]=20;d=s.hold(6);assert(!events.consume(d.sequence,d.eventValid,d.eventMs,s.now+1001));
  }
  std::cout<<"PASS: fire thresholds, timers, warm-up, faults, configuration, independent occupancy and event cursors\n";
}
