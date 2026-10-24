#pragma once
#include "Arduino.h"
using arduino_event_id_t=int;
struct arduino_event_info_t { struct { int reason; } wifi_sta_disconnected; };
constexpr int ARDUINO_EVENT_WIFI_STA_DISCONNECTED=1,WL_CONNECTED=3;
struct HostIP { String toString() const { return "192.0.2.1"; } };
struct HostWifi {
  template<typename F> void onEvent(F,int) {}
  int status() const { return WL_CONNECTED; }
  void disconnect() {}
  void begin(const char*,const char*) {}
  int softAPgetStationNum() const { return 0; }
  bool softAP(const char*,const char*) { return true; }
  void softAPdisconnect(bool) {}
  HostIP softAPIP() const { return {}; }
};
inline HostWifi WiFi;
