#pragma once
#include <atomic>
namespace thermal {
// One serialized configuration writer and one acquisition task. A timed-out
// request cannot leave a stale acknowledgement authorizing the next write.
class CapturePause {
 public:
  bool request() { auto s=State::Idle;return state_.compare_exchange_strong(s,State::Requested); }
  bool claim() { auto s=State::Requested;return state_.compare_exchange_strong(s,State::Pausing); }
  void acknowledge() { auto s=State::Pausing;state_.compare_exchange_strong(s,State::Paused); }
  bool paused() const { return state_.load()==State::Paused; }
  void release() {
    auto s=state_.load();
    while(s!=State::Idle && s!=State::Resume) {
      const auto next=s==State::Requested?State::Idle:State::Resume;
      if(state_.compare_exchange_weak(s,next)) return;
    }
  }
  bool resumeRequested() const { return state_.load()==State::Resume; }
  void resumed() { state_=State::Idle; }
 private:
  enum class State { Idle,Requested,Pausing,Paused,Resume };
  std::atomic<State> state_{State::Idle};
};
}
