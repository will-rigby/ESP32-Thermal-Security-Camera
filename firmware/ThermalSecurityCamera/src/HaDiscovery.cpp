#include "HaDiscovery.h"
namespace thermal {
String discoveryTopic(const char* id,ObjectClass objectClass) {
  return String("homeassistant/binary_sensor/thermal_")+id+"/"+className(objectClass)+"/config";
}
String discoveryJson(const Config& cfg,const char* id,ObjectClass objectClass) {
  if(!cfg.haDiscovery) return ""; // Retained tombstone removes an existing entity.
  cJSON* root=cJSON_CreateObject();
  if(!root) return "{}";
  const String base=cfg.topic;
  const String state=base+(objectClass==ObjectClass::Any?String(""):String("/")+className(objectClass))+"/state";
  const String unique=String("thermal_")+id+"_"+className(objectClass);
  cJSON_AddStringToObject(root,"unique_id",unique.c_str());
  cJSON_AddStringToObject(root,"name",objectClass==ObjectClass::Any?"Occupancy":objectClass==ObjectClass::Small?"Small objects":"Large objects");
  cJSON_AddStringToObject(root,"device_class","occupancy");
  cJSON_AddStringToObject(root,"state_topic",state.c_str());
  cJSON_AddStringToObject(root,"value_template","{{ value_json.state }}");
  cJSON_AddStringToObject(root,"payload_on","occupied");
  cJSON_AddStringToObject(root,"payload_off","clear");
  cJSON_AddNumberToObject(root,"qos",1);
  cJSON_AddStringToObject(root,"availability_mode","all");
  cJSON* available=cJSON_AddArrayToObject(root,"availability");
  cJSON* deviceOnline=cJSON_CreateObject();cJSON_AddItemToArray(available,deviceOnline);
  cJSON_AddStringToObject(deviceOnline,"topic",(base+"/availability").c_str());
  cJSON* valid=cJSON_CreateObject();cJSON_AddItemToArray(available,valid);
  cJSON_AddStringToObject(valid,"topic",state.c_str());
  cJSON_AddStringToObject(valid,"value_template","{{ 'online' if value_json.state in ['occupied', 'clear'] else 'offline' }}");
  cJSON* device=cJSON_AddObjectToObject(root,"device");
  cJSON* identifiers=cJSON_AddArrayToObject(device,"identifiers");
  cJSON_AddItemToArray(identifiers,cJSON_CreateString((String("thermal_")+id).c_str()));
  cJSON_AddStringToObject(device,"name",(String("Thermal camera ")+id).c_str());
  cJSON_AddStringToObject(device,"manufacturer","Waveshare");
  cJSON_AddStringToObject(device,"model","Thermal-45-Camera ESP32-S3");
  cJSON_AddStringToObject(device,"sw_version","0.3.0");
  cJSON_AddStringToObject(device,"configuration_url",(String("http://thermal-")+id+".local/settings").c_str());
  char* encoded=cJSON_PrintUnformatted(root);String result=encoded?encoded:"{}";
  cJSON_free(encoded);cJSON_Delete(root);return result;
}
}
