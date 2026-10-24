#include "../firmware/ThermalSecurityCamera/src/Application.h"
#include "../firmware/ThermalSecurityCamera/src/HaDiscovery.h"
#include <cassert>
#include <iostream>
namespace thermal { char deviceId[20]="aabbccddeeff"; }
using namespace thermal;
struct Json {
  cJSON* root;
  explicit Json(const String& s):root(cJSON_Parse(s.c_str())) { assert(root); }
  ~Json() { cJSON_Delete(root); }
  cJSON* get(const char* name) const { return cJSON_GetObjectItemCaseSensitive(root,name); }
  std::string text(const char* name) const { auto p=get(name);assert(cJSON_IsString(p));return p->valuestring; }
  double number(const char* name) const { auto p=get(name);assert(cJSON_IsNumber(p));return p->valuedouble; }
};
int main() {
  Config cfg;strlcpy(cfg.topic,"thermal/test",sizeof(cfg.topic));String error;
  assert(parseConfig("{\"delta_c\":4}",cfg,error));
  assert(!cfg.fire.enabled&&cfg.fire.thresholdC==80&&cfg.fire.activateMs==10000&&cfg.fire.clearMs==5000);
  assert(parseConfig("{\"fire_enabled\":true,\"fire_threshold_c\":85.5,\"fire_activate_ms\":15000,\"fire_clear_ms\":2500}",cfg,error));
  assert(parseConfig("{\"palette\":\"ironbow\"}",cfg,error));
  assert(cfg.fire.enabled&&cfg.fire.thresholdC==85.5f&&cfg.fire.activateMs==15000&&cfg.fire.clearMs==2500);
  for(const char* invalid:{"{\"fire_enabled\":1}","{\"fire_enabled\":null}","{\"fire_enabled\":\"true\"}",
      "{\"fire_threshold_c\":-1}","{\"fire_threshold_c\":1001}","{\"fire_threshold_c\":1e400}","{\"fire_threshold_c\":null}",
      "{\"fire_activate_ms\":0}","{\"fire_activate_ms\":600001}","{\"fire_activate_ms\":1.5}",
      "{\"fire_clear_ms\":-1}","{\"fire_clear_ms\":600001}","{\"fire_clear_ms\":\"5000\"}",
      "{\"fire_enabled\":false,\"fire_threshold_c\":-1}"}) {
    const String before=configJson(cfg,true);
    assert(!parseConfig(invalid,cfg,error)&&error.length());assert(configJson(cfg,true)==before);
  }
  Config restored;assert(parseConfig(configJson(cfg,true).c_str(),restored,error));
  assert(restored.fire.enabled&&restored.fire.thresholdC==85.5f&&restored.fire.activateMs==15000&&restored.fire.clearMs==2500);
  assert(parseConfig("{\"fire_threshold_c\":0,\"fire_activate_ms\":600000,\"fire_clear_ms\":0}",restored,error));
  assert(parseConfig("{\"fire_threshold_c\":1000,\"fire_activate_ms\":1,\"fire_clear_ms\":600000}",restored,error));
  {
    ConfigStore store;assert(store.begin(deviceId));
    assert(store.update(configJson(cfg,true).c_str(),error));
    ConfigStore rebooted;assert(rebooted.begin(deviceId)&&rebooted.loaded());
    assert(rebooted.get().fire.thresholdC==85.5f&&rebooted.get().fire.enabled);
    assert(!rebooted.update("{\"fire_enabled\":\"yes\"}",error));
    ConfigStore again;assert(again.begin(deviceId)&&again.get().fire.enabled);
  }
  Status status;status.bootId=0x12345678;status.fire.settings=cfg.fire;
  Json disabled(fireJson(status,false));assert(disabled.text("state")=="disabled"&&cJSON_IsNull(disabled.get("peak_c")));
  status.fire.state=FireState::Detected;status.fire.peakC=90;status.fire.sequence=7;status.fire.eventMs=120;status.fire.frameMs=150;
  Json state(fireJson(status,false)),event(fireJson(status,true));
  assert(state.text("state")=="detected"&&state.text("device_id")==deviceId&&state.number("version")==1);
  assert(state.number("uptime_ms")==150&&state.number("peak_c")==90&&state.number("threshold_c")==85.5);
  assert(state.number("activate_ms")==15000&&state.number("clear_ms")==2500&&!state.get("event_id"));
  assert(event.text("event_id")=="aabbccddeeff-12345678-7-fire"&&event.text("transition")=="detected"&&event.number("uptime_ms")==120);
  status.fire.state=FireState::Clear;status.fire.sequence++;
  Json cleared(fireJson(status,true));assert(cleared.text("transition")=="clear"&&cleared.text("event_id")!=event.text("event_id"));
  status.bootId++;Json rebootEvent(fireJson(status,true));assert(rebootEvent.text("event_id")!=cleared.text("event_id"));
  status.fire.state=FireState::Unavailable;status.fire.peakC=NAN;Json invalid(fireJson(status,false));assert(cJSON_IsNull(invalid.get("peak_c")));
  assert(fireDiscoveryTopic(deviceId)=="homeassistant/binary_sensor/thermal_aabbccddeeff/fire/config");
  Json discovery(fireDiscoveryJson(cfg,deviceId));
  assert(discovery.text("unique_id")=="thermal_aabbccddeeff_fire"&&discovery.text("name")=="Fire detection");
  assert(discovery.text("device_class")=="heat"&&discovery.text("state_topic")=="thermal/test/fire/state");
  assert(discovery.text("payload_on")=="detected"&&discovery.text("payload_off")=="clear"&&discovery.number("qos")==1);
  assert(discovery.text("availability_mode")=="all");
  auto availability=discovery.get("availability");assert(cJSON_GetArraySize(availability)==2);
  const auto templateValue=cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(availability,1),"value_template");
  assert(std::string(templateValue->valuestring)=="{{ 'online' if value_json.state in ['detected', 'clear'] else 'offline' }}");
  for(auto c:{ObjectClass::Any,ObjectClass::Small,ObjectClass::Large}) {
    Json occupancy(discoveryJson(cfg,deviceId,c));assert(occupancy.text("device_class")=="occupancy"&&occupancy.text("payload_on")=="occupied");
    assert(occupancy.text("unique_id")!=discovery.text("unique_id"));
  }
  cfg.haDiscovery=false;assert(fireDiscoveryJson(cfg,deviceId).empty());
  cfg.haDiscovery=true;std::memset(cfg.topic,'t',128);cfg.topic[128]=0;
  const size_t payloadBytes=fireDiscoveryJson(cfg,deviceId).length();
  // MQTT permits fragmentation beyond the 1024-byte transfer buffer. Staggered
  // discovery starts only below 1024 queued bytes; this must fit the 4096 outbox.
  assert(1024+payloadBytes+fireDiscoveryTopic(deviceId).length()+16<4096);
  assert(fireJson(status,true).length()+128+std::strlen("/fire/event")+16<1024);
  std::cout<<"PASS: production config validation/persistence, fire JSON, discovery/tombstones and MQTT size budgets (max-prefix discovery "<<payloadBytes<<" bytes)\n";
}
