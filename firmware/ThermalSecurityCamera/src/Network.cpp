#include "Application.h"
#include "BuildOptions.h"
#include "EventCursor.h"
#include "WifiRecovery.h"
#include "HaDiscovery.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <mqtt_client.h>
namespace thermal {
namespace {
std::atomic<bool> brokerUp{false}, brokerLost{false};
std::atomic<bool> haRefresh{false};
std::atomic<bool> setupActive{false},networkRunning{false};
std::atomic<uint32_t> wifiDisconnects{0},wifiReason{0};
void wifiEvent(arduino_event_id_t event,arduino_event_info_t info) {
  if(event==ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    ++wifiDisconnects;wifiReason=info.wifi_sta_disconnected.reason;
  }
}
void mqttEvent(void*,esp_event_base_t,int32_t id,void* data) {
  if(id==MQTT_EVENT_CONNECTED) brokerUp=true;
  if(id==MQTT_EVENT_DISCONNECTED) { brokerUp=false; brokerLost=true; }
  if(id==MQTT_EVENT_DATA) {
    const auto event=static_cast<esp_mqtt_event_handle_t>(data);
    // Only accept the complete, exact birth message from our one subscription.
    if(event->current_data_offset==0 && event->total_data_len==6 && event->data_len==6 &&
       event->topic_len==20 && !memcmp(event->topic,"homeassistant/status",20) && !memcmp(event->data,"online",6)) haRefresh=true;
  }
}
void networkTask(void*) {
  networkRunning=true;
  Config cfg=configStore.get(); uint32_t revision=0,retry=0;
  WifiRecovery recovery;
  uint32_t publishedSequence[4]{},lastState[4]{};
  uint8_t publishedState[4]{};
  unsigned discoveryIndex=4;
  bool refreshState=false,subscribed=false;
  bool ap=false, announced=false, mdns=false;
  esp_mqtt_client_handle_t client=nullptr;
  EventCursor events[4];
  String base,availability;
  for(;;) {
    const uint32_t now=millis(); Config next=configStore.get();
    const bool changed=next.revision!=revision;
    if(changed) {
      const bool wifiChanged=strcmp(next.ssid,cfg.ssid)||strcmp(next.wifiPassword,cfg.wifiPassword)||revision==0;
      cfg=next; revision=cfg.revision;
      if(client) {
        if(brokerUp) esp_mqtt_client_publish(client,availability.c_str(),"offline",0,1,true);
        esp_mqtt_client_stop(client); esp_mqtt_client_destroy(client); client=nullptr;
      }
      brokerUp=false; brokerLost=false; mqttConnected=false; announced=false;subscribed=false;
      base=cfg.topic; availability=base+"/availability";
      if(wifiChanged || WiFi.status()!=WL_CONNECTED) {
        WiFi.disconnect();
        recovery.reset();
        if(cfg.ssid[0]) { WiFi.begin(cfg.ssid,cfg.wifiPassword);recovery.started(now); }
      }
      retry=now;
    }
    const bool connected=WiFi.status()==WL_CONNECTED;
    if(!connected && mdns) { MDNS.end(); mdns=false; }
    const auto action=recovery.tick(now,cfg.ssid[0]!=0,connected,ap && WiFi.softAPgetStationNum()>0);
    if(action==WifiAction::StopAttempt) WiFi.disconnect();
    else if(action==WifiAction::Connect) WiFi.begin(cfg.ssid,cfg.wifiPassword);
    if(!connected && (!cfg.ssid[0] || recovery.waiting())) {
      if(!ap) {
        String name=String("Thermal-")+deviceId;
        String password=String("thermal-")+String(deviceId).substring(6);
        ap=WiFi.softAP(name.c_str(),password.c_str());
        setupActive=ap;
        Serial.printf("Setup Wi-Fi %s password %s, http://192.168.4.1\n",name.c_str(),password.c_str());
      }
    }
    if(connected) {
      if(ap) { WiFi.softAPdisconnect(true); ap=false;setupActive=false; }
      if(!mdns) { String host=String("thermal-")+deviceId; mdns=MDNS.begin(host.c_str()); }
    }
    // Destroy on disconnect to discard QoS retransmission outbox entries. A new
    // clean-session client publishes only the latest state after reconnecting.
    if(client && (brokerLost.exchange(false)||!connected)) {
      esp_mqtt_client_stop(client); esp_mqtt_client_destroy(client); client=nullptr;
      brokerUp=false; mqttConnected=false; announced=false;subscribed=false; retry=now+5000;
    }
    if(!THERMAL_TEST_PATTERN && connected && cfg.broker[0] && !client && int32_t(now-retry)>=0) {
      esp_mqtt_client_config_t c{};
      c.broker.address.hostname=cfg.broker; c.broker.address.port=cfg.port;
      c.broker.address.transport=MQTT_TRANSPORT_OVER_TCP;
      c.credentials.client_id=deviceId;
      c.credentials.username=cfg.mqttUser[0]?cfg.mqttUser:nullptr;
      c.credentials.authentication.password=cfg.mqttPassword[0]?cfg.mqttPassword:nullptr;
      c.session.last_will.topic=availability.c_str(); c.session.last_will.msg="offline";
      c.session.last_will.qos=1; c.session.last_will.retain=true; c.session.keepalive=30;
      c.network.disable_auto_reconnect=true; c.network.timeout_ms=2000;
      c.outbox.limit=4096;
      c.buffer.size=1024; c.buffer.out_size=1024;
      client=esp_mqtt_client_init(&c);
      if(client && (esp_mqtt_client_register_event(client,MQTT_EVENT_ANY,mqttEvent,nullptr)!=ESP_OK ||
                    esp_mqtt_client_start(client)!=ESP_OK)) {
        esp_mqtt_client_destroy(client); client=nullptr;
      }
      retry=now+5000;
    }
    if(client && brokerUp) {
      Status s=getStatus();
      if(!subscribed) subscribed=esp_mqtt_client_subscribe(client,"homeassistant/status",1)>=0;
      if(!announced) {
        bool sent=true;
        for(unsigned i=0;i<4;++i) {
          const bool fire=i==3;
          const auto objectClass=fire?ObjectClass::Any:ObjectClass(i);
          const auto& d=classDetection(s.detection,objectClass);
          const uint32_t sequence=fire?s.fire.sequence:d.sequence;
          const uint8_t state=fire?uint8_t(s.fire.state):uint8_t(d.state);
          events[i].connect(sequence);
          const String path=base+(fire?String("/fire"):i?String("/")+className(objectClass):String(""));
          const String payload=fire?fireJson(s,false):detectionJson(s,false,objectClass);
          if(esp_mqtt_client_publish(client,(path+"/state").c_str(),payload.c_str(),payload.length(),1,true)<0) sent=false;
          else { lastState[i]=now;publishedSequence[i]=sequence;publishedState[i]=state; }
        }
        if(sent && esp_mqtt_client_publish(client,availability.c_str(),"online",0,1,true)>=0) {
          announced=true; mqttConnected=true;discoveryIndex=0;
        }
      } else {
        if(haRefresh.exchange(false)) { discoveryIndex=0;refreshState=true; }
        // Sequence identifies an actual edge even if the one-frame transition
        // flag has cleared before the network task gets scheduled.
        bool refreshed=true;
        for(unsigned i=0;i<4;++i) {
          const bool fire=i==3;
          const auto objectClass=fire?ObjectClass::Any:ObjectClass(i);const auto& d=classDetection(s.detection,objectClass);
          const String path=base+(fire?String("/fire"):i?String("/")+className(objectClass):String(""));
          const uint32_t sequence=fire?s.fire.sequence:d.sequence;
          const uint8_t state=fire?uint8_t(s.fire.state):uint8_t(d.state);
          const bool edge=fire?s.fire.eventValid:(d.state==Occupancy::Clear || d.state==Occupancy::Occupied);
          if(events[i].consume(sequence,edge,fire?s.fire.eventMs:d.eventMs,now)) {
            const String payload=fire?fireJson(s,true):detectionJson(s,true,objectClass);
            esp_mqtt_client_publish(client,(path+"/event").c_str(),payload.c_str(),payload.length(),1,false);
          }
          if(refreshState || state!=publishedState[i] || sequence!=publishedSequence[i] || now-lastState[i]>=30000) {
            const String payload=fire?fireJson(s,false):detectionJson(s,false,objectClass);
            if(esp_mqtt_client_publish(client,(path+"/state").c_str(),payload.c_str(),payload.length(),1,true)>=0) {
              lastState[i]=now; publishedState[i]=state; publishedSequence[i]=sequence;
            } else refreshed=false;
          }
        }
        if(refreshed) refreshState=false;
        // Stagger discovery to fit the bounded QoS outbox and scarce RAM.
        if(discoveryIndex<4 && esp_mqtt_client_get_outbox_size(client)<1024) {
          const bool fire=discoveryIndex==3;
          const auto objectClass=fire?ObjectClass::Any:ObjectClass(discoveryIndex);
          const String payload=fire?fireDiscoveryJson(cfg,deviceId):discoveryJson(cfg,deviceId,objectClass);
          const String topic=fire?fireDiscoveryTopic(deviceId):discoveryTopic(deviceId,objectClass);
          if(payload!="{}" && esp_mqtt_client_publish(client,topic.c_str(),payload.c_str(),payload.length(),1,true)>=0) ++discoveryIndex;
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
}
void startNetwork() {
  WiFi.onEvent(wifiEvent,ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  if(xTaskCreatePinnedToCore(networkTask,"thermal-network",6144,nullptr,1,nullptr,0)!=pdPASS)
    Serial.println("Network task creation failed");
}
void addNetworkDiagnostics(cJSON* root) {
  cJSON_AddBoolToObject(root,"network_task_running",networkRunning.load());
  cJSON_AddNumberToObject(root,"wifi_status",int(WiFi.status()));
  cJSON_AddNumberToObject(root,"wifi_disconnect_reason",wifiReason.load());
  cJSON_AddNumberToObject(root,"wifi_disconnects",wifiDisconnects.load());
  cJSON_AddBoolToObject(root,"ap_active",setupActive.load());
  cJSON_AddNumberToObject(root,"ap_clients",setupActive?WiFi.softAPgetStationNum():0);
  cJSON_AddStringToObject(root,"ap_ip",WiFi.softAPIP().toString().c_str());
}
}
