#pragma once
// Minimal host adapters; production JSON and configuration code is compiled unchanged.
#include <string>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <functional>
class String : public std::string {
 public:
  using std::string::string;
  String(const std::string& s):std::string(s) {}
  String substring(size_t start) const { return substr(start); }
};
inline size_t strlcpy(char* dest,const char* src,size_t size) {
  const size_t length=std::strlen(src);
  if(size) { const size_t n=length<size?length:size-1;std::memcpy(dest,src,n);dest[n]=0; }
  return length;
}
inline uint32_t hostNow=0;
inline std::function<void()> hostTick;
inline uint32_t millis() { return hostNow; }
inline unsigned pdMS_TO_TICKS(unsigned value) { return value; }
inline void vTaskDelay(unsigned ms) { hostNow+=ms;if(hostTick) hostTick(); }
constexpr int pdPASS=1,ESP_OK=0;
inline int xTaskCreatePinnedToCore(void (*fn)(void*),const char*,unsigned,void* arg,unsigned,void*,unsigned) { fn(arg);return pdPASS; }
struct HostSerial { template<typename... T> void printf(const char*,T...) {} void println(const char*) {} };
inline HostSerial Serial;
