#pragma once
#include <cstdint>
#include <initializer_list>
namespace thermal {
inline uint32_t videoPeriodForFps(double fps) {
  for(uint32_t period : {50u,100u,200u,500u,1000u,2000u,5000u,10000u})
    if(fps==1000.0/period)return period;
  return 0;
}
inline bool heldFrameFresh(uint32_t now,uint32_t frameMs,uint32_t period) {
  return uint32_t(now-frameMs)<=period+1000;
}
struct VideoTiming {
  uint32_t period=50,last=0,window=0,count=0;
  bool haveFrame=false;
  float fps=0;
  void reset() { haveFrame=false;count=0;fps=0; }
  void setPeriod(uint32_t value) { if(period!=value){period=value;reset();} }
  bool due(uint32_t now) const { return !haveFrame || uint32_t(now-last)>=period; }
  void published(uint32_t now) {
    if(!haveFrame){window=now;count=0;haveFrame=true;}
    else if(++count && uint32_t(now-window)>= (period>1000?period:1000)) {
      fps=count*1000.f/uint32_t(now-window);window=now;count=0;
    }
    last=now;
  }
};
}
