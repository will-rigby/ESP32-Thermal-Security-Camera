#include "../firmware/ThermalSecurityCamera/src/Detector.h"
#include "../firmware/ThermalSecurityCamera/src/CapturePause.h"
#include "../firmware/ThermalSecurityCamera/src/EventCursor.h"
#include "../firmware/ThermalSecurityCamera/src/Orientation.h"
#include "../firmware/ThermalSecurityCamera/src/WifiRecovery.h"
#include "../firmware/ThermalSecurityCamera/src/Palette.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>
using namespace thermal;
struct Scene {
  Detector detector;
  DetectionSettings settings;
  std::vector<float> pixels=std::vector<float>(PixelCount,20.f);
  uint32_t now=0;
  Scene(){settings.learnMs=1000;detector.configure(settings);}
  Detection step(uint32_t ms=100){now+=ms;return detector.process(pixels.data(),now);}
  void learn(){for(int i=0;i<12;++i)step();}
  void blob(int x,int y,int w,int h,float c=30.f){for(int j=y;j<y+h;++j)for(int i=x;i<x+w;++i)pixels[j*80+i]=c;}
  void clear(){std::fill(pixels.begin(),pixels.end(),20.f);}
};
int main(){
  {
    CapturePause p;
    assert(!p.paused()&&!p.resumeRequested());
    assert(p.request()&&!p.request());assert(p.claim());p.acknowledge();
    assert(p.paused()&&!p.request());p.release();assert(p.resumeRequested());
    p.resumed();assert(p.request());
    // Timeout before acquisition sees the request.
    p.release();assert(!p.claim());assert(p.request());assert(p.claim());
    // Timeout during hardware pause must resume, not leave a stale ACK.
    p.release();p.acknowledge();assert(!p.paused()&&p.resumeRequested());
    assert(!p.request());p.resumed();assert(p.request());assert(p.claim());
    p.acknowledge();p.release();p.release();assert(p.resumeRequested());p.resumed();
    assert(p.request());p.release();assert(p.request());p.release();
  }
  {
    for(unsigned i=0;i<5;++i) {
      Palette p=Palette::Fire;assert(parsePalette(paletteName(Palette(i)),p)&&p==Palette(i));
      for(unsigned level=0;level<256;++level) {
        const auto c=paletteColour(p,uint8_t(level));
        if(p==Palette::WhiteHot) assert(c.r==level&&c.g==level&&c.b==level);
        if(p==Palette::BlackHot) assert(c.r==255-level&&c.g==255-level&&c.b==255-level);
      }
    }
    Palette p=Palette::Fire;assert(!parsePalette("unknown",p));
    assert(paletteColour(Palette::Fire,0).r==0&&paletteColour(Palette::Fire,255).b==255);
    assert(paletteColour(Palette::Rainbow,0).b>0&&paletteColour(Palette::Rainbow,255).r==255);
  }
  {
    Scene s;s.learn();s.blob(10,10,3,5); // 15 pixels: below the large cutoff.
    for(int i=0;i<5;++i) assert(s.step().small.state==Occupancy::Clear);
    auto d=s.step();assert(d.small.state==Occupancy::Occupied&&d.small.pixels==15);
    assert(d.large.state==Occupancy::Clear&&d.small.sequence==1);
    s.blob(40,40,4,4,40.f); // Separate 16-pixel region: both channels present.
    for(int i=0;i<6;++i)d=s.step();
    assert(d.small.state==Occupancy::Occupied&&d.large.state==Occupancy::Occupied);
    assert(d.small.pixels==15&&d.large.pixels==16&&d.small.peakC==30.f&&d.large.peakC==40.f);
    for(int i=0;i<6000;++i) { d=s.step();assert(d.small.state==Occupancy::Occupied&&d.large.state==Occupancy::Occupied); }
    s.blob(10,10,3,5,20.f);
    for(int i=0;i<20;++i) assert(s.step().small.state==Occupancy::Occupied);
    d=s.step();assert(d.small.state==Occupancy::Clear&&d.small.transition&&d.small.sequence==2);
    assert(d.state==Occupancy::Occupied&&d.large.state==Occupancy::Occupied&&d.large.sequence==1);
    d=s.detector.unavailable(s.now);assert(d.small.state==Occupancy::Unavailable&&d.large.state==Occupancy::Unavailable);
    s.clear();d=s.step();assert(d.small.state==Occupancy::Learning&&!d.small.transition);
  }
  {
    Scene s;s.learn();s.blob(10,10,2,2);for(int i=0;i<6;++i)s.step();
    s.blob(10,10,4,4); // Growth across the cutoff: independent hysteresis.
    Detection d;for(int i=0;i<6;++i)d=s.step();
    assert(d.small.state==Occupancy::Occupied&&d.large.state==Occupancy::Occupied);
    for(int i=0;i<15;++i)d=s.step();
    assert(d.small.state==Occupancy::Clear&&d.large.state==Occupancy::Occupied&&d.sequence==1);
    s.detector.reset();d=s.step();assert(d.small.state==Occupancy::Learning&&d.large.state==Occupancy::Learning);
  }
  {
    Scene s;s.settings.largeMinPixels=8;s.settings.activateMs=0;s.detector.configure(s.settings);s.learn();
    s.blob(10,10,2,2);s.blob(12,12,2,2); // Diagonally touching regions merge (8-connectivity).
    auto d=s.step();assert(d.large.pixels==8&&d.large.state==Occupancy::Occupied&&d.small.state==Occupancy::Clear);
    s.settings.largeMinPixels=PixelCount+1;assert(!validSettings(s.settings));
  }
  {
    WifiRecovery wifi;
    assert(wifi.tick(0,true,false,false)==WifiAction::Connect);
    assert(wifi.tick(29999,true,false,false)==WifiAction::None);
    assert(wifi.tick(30000,true,false,false)==WifiAction::StopAttempt&&wifi.waiting());
    assert(wifi.tick(89999,true,false,false)==WifiAction::None);
    assert(wifi.tick(90000,true,false,true)==WifiAction::None); // A phone keeps the AP quiet.
    assert(wifi.tick(120000,true,false,true)==WifiAction::None);
    assert(wifi.tick(120000,true,false,false)==WifiAction::Connect);
    assert(wifi.tick(120001,true,true,false)==WifiAction::None);
    assert(wifi.tick(120002,true,false,false)==WifiAction::Connect); // Lost connection.
    wifi.started(UINT32_MAX-10000);
    assert(wifi.tick(20000,true,false,false)==WifiAction::StopAttempt); // Clock wraps.
    assert(wifi.tick(20001,false,false,false)==WifiAction::None&&!wifi.waiting());
    wifi.started(30000);assert(wifi.tick(30001,true,false,true)==WifiAction::None); // Explicit save may connect.
  }
  {
    const Roi monitored{7,11,13,9};
    for(bool flip:{false,true}) {
      const Roi displayed=orientedRoi(monitored,flip);
      assert(displayed.y==11&&displayed.width==13&&displayed.height==9);
      assert(displayed.x==(flip?60:7));
      const Roi restored=orientedRoi(displayed,flip);assert(restored.x==monitored.x);
      // Every rendered pixel must select the same native region as the ROI
      // dragged over it, including scaled column boundaries and both edges.
      for(int pixel=0;pixel<310;++pixel) {
        const int displayColumn=pixel*SensorWidth/310;
        const int nativeColumn=orientedColumn(displayColumn,flip);
        const bool nativeInside=nativeColumn>=monitored.x&&nativeColumn<monitored.x+monitored.width;
        const bool displayInside=displayColumn>=displayed.x&&displayColumn<displayed.x+displayed.width;
        assert(nativeInside==displayInside);
      }
    }
    assert(orientedColumn(0,true)==79&&orientedColumn(79,true)==0);
    const Roi full=orientedRoi({0,0,80,62},true);assert(full.x==0&&full.width==80);
  }
  {
    Scene s; assert(s.step().state==Occupancy::Learning); s.learn();assert(s.step().state==Occupancy::Clear);
    s.blob(20,20,2,2);for(int i=0;i<5;++i)assert(s.step().state==Occupancy::Clear);
    auto d=s.step();assert(d.state==Occupancy::Occupied&&d.transition&&d.sequence==1&&d.pixels==4);
    const uint32_t entryMs=s.now;assert(d.eventMs==entryMs);
    for(int i=0;i<6000;++i){d=s.step();assert(d.state==Occupancy::Occupied&&!d.transition);}
    assert(d.eventMs==entryMs);
    s.clear();for(int i=0;i<20;++i)assert(s.step().state==Occupancy::Occupied);
    d=s.step();assert(d.state==Occupancy::Clear&&d.transition&&d.sequence==2);
    assert(d.eventMs==s.now);
  }
  {
    Scene s;s.learn();s.blob(10,10,1,1);s.blob(15,15,1,1);s.blob(30,30,1,1);s.blob(40,40,1,1);
    for(int i=0;i<30;++i)assert(s.step().state==Occupancy::Clear);
    s.clear();s.blob(10,10,4,4);for(int i=0;i<3;++i)s.step();s.clear();
    for(int i=0;i<30;++i)assert(s.step().state==Occupancy::Clear);
  }
  {
    Scene s;s.settings.roi={20,20,10,10};s.detector.configure(s.settings);s.learn();s.blob(0,0,15,15);
    for(int i=0;i<20;++i)assert(s.step().state==Occupancy::Clear);
    s.blob(28,28,4,4);for(int i=0;i<7;++i)s.step();auto d=s.step();
    assert(d.state==Occupancy::Occupied&&d.pixels==4&&d.bounds.width==2&&d.bounds.height==2);
  }
  {
    Scene s;s.learn();s.blob(10,10,4,4);s.blob(30,30,4,4);for(int i=0;i<7;++i)s.step();
    s.blob(10,10,4,4,20.f);for(int i=0;i<40;++i)assert(s.step().state==Occupancy::Occupied);
  }
  {
    Scene s;s.learn();s.blob(10,10,4,4);s.blob(12,12,4,4);
    for(int i=0;i<7;++i)s.step();auto d=s.step();
    assert(d.state==Occupancy::Occupied&&d.pixels==28&&d.sequence==1);
    s.clear();s.blob(12,12,4,4);
    for(int i=0;i<30;++i){d=s.step();assert(d.state==Occupancy::Occupied&&d.sequence==1);}
    s.clear();for(int i=0;i<21;++i)d=s.step();assert(d.state==Occupancy::Clear&&d.sequence==2);
  }
  {
    Scene s;s.learn();s.blob(10,10,8,8);for(int i=0;i<7;++i)s.step();
    assert(s.detector.unavailable(s.now).state==Occupancy::Unavailable);
    s.clear();assert(s.step().state==Occupancy::Learning);s.learn();assert(!s.step().transition);
    s.pixels[7]=std::numeric_limits<float>::quiet_NaN();assert(s.step().state==Occupancy::Unavailable);
  }
  {
    // A receive error must invalidate every channel, then suppress alerts while
    // reinitialization temperatures settle. Never replay the previous entry.
    Scene s;s.learn();s.blob(10,10,2,2);s.blob(30,30,5,5);
    for(int i=0;i<8;++i)s.step();
    const auto before=s.step();
    assert(before.small.state==Occupancy::Occupied&&before.large.state==Occupancy::Occupied);
    const auto failed=s.detector.unavailable(s.now);
    for(auto c:{ObjectClass::Any,ObjectClass::Small,ObjectClass::Large}) {
      assert(classDetection(failed,c).state==Occupancy::Unavailable);
      assert(!classDetection(failed,c).transition);
    }
    for(int i=0;i<1200;++i) {
      std::fill(s.pixels.begin(),s.pixels.end(),180.f-float(i)/10);
      s.now+=100; const auto d=s.detector.process(s.pixels.data(),s.now,true);
      for(auto c:{ObjectClass::Any,ObjectClass::Small,ObjectClass::Large}) {
        const auto& channel=classDetection(d,c);
        assert(channel.state==Occupancy::Learning&&!channel.transition);
        assert(channel.sequence==classDetection(before,c).sequence);
      }
    }
    s.clear();s.learn();const auto after=s.step();
    for(auto c:{ObjectClass::Any,ObjectClass::Small,ObjectClass::Large})
      assert(classDetection(after,c).state==Occupancy::Clear&&!classDetection(after,c).transition);
  }
  {
    Scene s;s.learn();assert(s.step(2000).state==Occupancy::Learning);
    s.detector.reset();assert(s.step().state==Occupancy::Learning);
  }
  {
    Scene s;s.now=UINT32_MAX-1600;s.learn();s.blob(3,3,4,4);
    for(int i=0;i<7;++i)s.step();assert(s.step().state==Occupancy::Occupied);
  }
  {
    Scene s;s.learn();for(int i=0;i<4000;++i){for(auto& p:s.pixels)p+=0.001f;assert(s.step().state==Occupancy::Clear);}
  }
  {
    Scene s;
    // Startup drift and moving warm regions must not become occupancy events.
    for(int i=0;i<1200;++i) {
      const float temperature=20.f+150.f*std::exp(-float(i)/100.f);
      std::fill(s.pixels.begin(),s.pixels.end(),temperature);
      if(i>50 && i<100) s.blob(10,10,4,4,temperature+10.f);
      s.now+=100;
      const auto d=s.detector.process(s.pixels.data(),s.now,true);
      assert(d.state==Occupancy::Learning&&!d.transition&&d.sequence==0);
    }
    s.clear();
    for(int i=0;i<9;++i) assert(s.step().state==Occupancy::Learning);
    assert(s.step().state==Occupancy::Clear);
    s.blob(10,10,4,4);for(int i=0;i<7;++i)s.step();
    assert(s.step().state==Occupancy::Occupied);
    s.pixels[0]=NAN;s.now+=100;
    assert(s.detector.process(s.pixels.data(),s.now,true).state==Occupancy::Unavailable);
  }
  {
    Scene s;s.settings.activateMs=0;s.detector.configure(s.settings);s.learn();s.blob(0,0,80,62);
    auto d=s.step();assert(d.pixels==PixelCount&&d.state==Occupancy::Occupied); // Worst-case flood fill.
  }
  {
    DetectionSettings s;assert(validSettings(s));s.roi={79,61,2,1};assert(!validSettings(s));
    s=DetectionSettings{};s.deltaC=NAN;assert(!validSettings(s));
    s=DetectionSettings{};s.minPixels=0;assert(!validSettings(s));
    s=DetectionSettings{};s.roi={0,0,INT32_MAX,62};assert(!validSettings(s));
  }
  {
    EventCursor c;c.connect(4);assert(!c.consume(4,true,0,0));
    assert(c.consume(5,true,100,200));assert(!c.consume(5,true,100,300));
    c.connect(9);assert(!c.consume(9,true,100,200)); // Offline transitions discarded.
    assert(!c.consume(10,true,100,5000));
    assert(c.consume(11,true,UINT32_MAX-20,20));
  }
  puts("PASS: mirrored ROI mapping, startup warm-up, learning, persistence, stationary occupancy, clearing, noise, ROI bounds, overlapping occupants, sensor failure, frame gaps, clock wrap, slow drift, full-frame flood fill and MQTT event cursor");
}
