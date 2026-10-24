#pragma once
#include "Arduino.h"
#include <vector>
using esp_event_base_t=void*;
using esp_mqtt_client_handle_t=void*;
constexpr int MQTT_EVENT_CONNECTED=1,MQTT_EVENT_DISCONNECTED=2,MQTT_EVENT_DATA=3,MQTT_EVENT_ANY=4,MQTT_TRANSPORT_OVER_TCP=1;
struct HostMqttEvent {
  int current_data_offset=0,total_data_len=6,data_len=6,topic_len=20;
  const char* topic="homeassistant/status";
  const char* data="online";
};
using esp_mqtt_event_handle_t=HostMqttEvent*;
struct esp_mqtt_client_config_t {
  struct { struct { const char* hostname;unsigned port;int transport; } address; } broker;
  struct { const char* client_id;const char* username;struct { const char* password; } authentication; } credentials;
  struct { struct { const char* topic;const char* msg;int qos;bool retain; } last_will;int keepalive; } session;
  struct { bool disable_auto_reconnect;int timeout_ms; } network;
  struct { int limit; } outbox;
  struct { int size,out_size; } buffer;
};
struct HostPublication { std::string topic,payload;int qos;bool retain;uint32_t time; };
inline std::vector<HostPublication> hostPublications;
inline void (*hostMqttEvent)(void*,esp_event_base_t,int32_t,void*)=nullptr;
inline int hostConnections=0,hostDestroyed=0,hostOutbox=0;
inline esp_mqtt_client_config_t hostMqttConfig{};
inline esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t* cfg) { hostMqttConfig=*cfg;return reinterpret_cast<void*>(1); }
inline int esp_mqtt_client_register_event(esp_mqtt_client_handle_t,int,decltype(hostMqttEvent) fn,void*) { hostMqttEvent=fn;return ESP_OK; }
inline int esp_mqtt_client_start(esp_mqtt_client_handle_t) { ++hostConnections;hostMqttEvent(nullptr,nullptr,MQTT_EVENT_CONNECTED,nullptr);return ESP_OK; }
inline void esp_mqtt_client_stop(esp_mqtt_client_handle_t) {}
inline void esp_mqtt_client_destroy(esp_mqtt_client_handle_t) { ++hostDestroyed; }
inline int esp_mqtt_client_subscribe(esp_mqtt_client_handle_t,const char*,int) { return 1; }
inline int esp_mqtt_client_get_outbox_size(esp_mqtt_client_handle_t) { return hostOutbox; }
inline int esp_mqtt_client_publish(esp_mqtt_client_handle_t,const char* topic,const char* data,int length,int qos,bool retain) {
  hostPublications.push_back({topic,std::string(data,length?size_t(length):std::strlen(data)),qos,retain,hostNow});return 1;
}
