#pragma once
#include <stdint.h>
namespace thermal {
// No offline queue: a connection begins at the current detector sequence.
class EventCursor {
 public:
  void connect(uint32_t sequence) { consumed_=sequence; }
  bool consume(uint32_t sequence,bool transition,uint32_t frameMs,uint32_t now) {
    if(sequence==consumed_) return false;
    consumed_=sequence;
    return transition && uint32_t(now-frameMs)<=1000;
  }
 private:
  uint32_t consumed_=0;
};
}
