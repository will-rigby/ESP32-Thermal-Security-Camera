#include "../firmware/ThermalSecurityCamera/src/VideoTiming.h"
#include <cassert>
#include <limits>
#include <cstdio>
using namespace thermal;
int main() {
  for(double rate : {20.,10.,5.,2.,1.,.5,.2,.1}) {
    uint32_t period=videoPeriodForFps(rate);assert(period && 1000.0/period==rate);
    VideoTiming clock;clock.setPeriod(period);
    uint32_t start=UINT32_MAX-500;
    assert(clock.due(start));clock.published(start);
    assert(!clock.due(start+period-1));assert(clock.due(start+period));
    for(uint32_t i=1;i<=20;++i)clock.published(start+i*period);
    assert(clock.fps>rate*.999 && clock.fps<rate*1.001);
    assert(heldFrameFresh(start+period+1000,start,period));
    assert(!heldFrameFresh(start+period+1001,start,period));
    // Late publication resets the schedule: no backlog/catch-up burst.
    clock.published(start+30*period);assert(!clock.due(start+30*period+1));
    clock.reset();assert(clock.due(start+30*period+1));assert(clock.fps==0);
  }
  for(double invalid : {0.,-1.,.01,.3,3.,21.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
    assert(videoPeriodForFps(invalid)==0);
  VideoTiming clock;clock.setPeriod(10000);clock.published(100);
  clock.setPeriod(50);assert(clock.due(101));clock.published(101);
  clock.setPeriod(10000);assert(clock.due(102));
  puts("PASS: all video rates, invalid values, wraparound, held-frame expiry, cadence and immediate rate changes");
}
