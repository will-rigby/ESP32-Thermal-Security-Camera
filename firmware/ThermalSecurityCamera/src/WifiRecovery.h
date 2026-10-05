#pragma once
#include <stdint.h>
namespace thermal {
enum class WifiAction { None, Connect, StopAttempt };
class WifiRecovery {
 public:
  void started(uint32_t now) { attempting_=true; waiting_=false; since_=now; }
  void reset() { attempting_=waiting_=false; }
  bool waiting() const { return waiting_; }
  WifiAction tick(uint32_t now,bool configured,bool connected,bool setupClient) {
    if(connected || !configured) { reset(); return WifiAction::None; }
    if(attempting_) {
      if(uint32_t(now-since_)<30000) return WifiAction::None;
      attempting_=false; waiting_=true; since_=now; return WifiAction::StopAttempt;
    }
    // Keep the setup AP quiet while a phone is using it. Only an explicit
    // settings save may start another connection attempt with clients present.
    if(setupClient || (waiting_ && uint32_t(now-since_)<60000)) return WifiAction::None;
    started(now); return WifiAction::Connect;
  }
 private:
  uint32_t since_=0;
  bool attempting_=false,waiting_=false;
};
}
