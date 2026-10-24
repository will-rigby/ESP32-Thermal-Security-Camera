#include "../firmware/ThermalSecurityCamera/src/Network.cpp"
#include <cassert>
#include <iostream>
namespace thermal {
ConfigStore configStore;
char deviceId[20]="aabbccddeeff";
std::atomic<bool> mqttConnected{false};
Status hostStatus;
Status getStatus() { return hostStatus; }
// Occupancy serialization is not under test; retain the network's real loops.
String detectionJson(const Status&,bool,ObjectClass) { return "{\"state\":\"clear\"}"; }
}
using namespace thermal;
struct Stop {};
int main() {
  assert(configStore.begin(deviceId));String error;
  assert(configStore.update("{\"broker\":\"test-broker\",\"fire_enabled\":true}",error));
  hostStatus.fire.settings=configStore.get().fire;hostStatus.fire.state=FireState::Clear;
  hostTick=[] {
    if(hostNow==300) {
      hostStatus.fire.state=FireState::Detected;hostStatus.fire.sequence=1;hostStatus.fire.eventMs=hostNow;hostStatus.fire.eventValid=true;
    }
    if(hostNow==400&&hostMqttEvent) { HostMqttEvent birth;hostMqttEvent(nullptr,nullptr,MQTT_EVENT_DATA,&birth); }
    if(hostNow==700&&hostMqttEvent) hostMqttEvent(nullptr,nullptr,MQTT_EVENT_DISCONNECTED,nullptr);
    if(hostNow==1000) { hostStatus.fire.state=FireState::Clear;hostStatus.fire.sequence=2;hostStatus.fire.eventMs=hostNow; }
    if(hostNow==6000) { hostStatus.fire.state=FireState::Detected;hostStatus.fire.sequence=3;hostStatus.fire.eventMs=hostNow; }
    if(hostNow==6200) { hostStatus.fire.state=FireState::Unavailable;hostStatus.fire.eventValid=false; }
    if(hostNow==6300) hostStatus.fire.state=FireState::Clear;
    if(hostNow==37000) { String err;assert(configStore.update("{\"ha_discovery\":false}",err)); }
    if(hostNow>=37500) throw Stop{};
  };
  try { startNetwork(); } catch(const Stop&) {}
  if(THERMAL_TEST_PATTERN) {
    assert(hostConnections==0&&hostPublications.empty());
    std::cout<<"PASS: synthetic firmware never initializes or publishes MQTT\n";return 0;
  }
  assert(hostMqttConfig.outbox.limit==4096&&hostMqttConfig.buffer.out_size==1024);
  assert(hostMqttConfig.session.last_will.qos==1&&hostMqttConfig.session.last_will.retain);
  const std::string base="thermal/aabbccddeeff";
  unsigned events=0,discoveries=0,tombstones=0,states=0;bool offlineClear=false,refreshed=false,birthState=false;
  for(const auto& p:hostPublications) {
    assert(p.qos==1);
    if(p.topic==base+"/fire/event") {
      ++events;assert(!p.retain);assert(p.time==300||p.time==6000);
      assert(p.payload.find("\"transition\":\"detected\"")!=std::string::npos);
    }
    if(p.topic==base+"/fire/state") {
      ++states;assert(p.retain);
      if(p.time==400) birthState=true;
      if(p.time>=5700&&p.time<6000) offlineClear=p.payload.find("\"state\":\"clear\"")!=std::string::npos;
      if(p.time==36300) refreshed=true;
    }
    if(p.topic=="homeassistant/binary_sensor/thermal_aabbccddeeff/fire/config") {
      assert(p.retain);if(p.payload.empty()) ++tombstones;else ++discoveries;
    }
  }
  assert(events==2&&states>=7&&birthState&&offlineClear&&refreshed);
  assert(discoveries==3&&tombstones==1&&hostConnections==3&&hostDestroyed==2);
  std::cout<<"PASS: production MQTT loop fire QoS/retain, transitions, reconnect without replay, birth/periodic refresh and discovery cleanup\n";
}
