#include "Application.h"
namespace thermal {
String fireJson(const Status& s,bool event) {
  const auto& d=s.fire;
  cJSON* root=cJSON_CreateObject();
  if(!root) return "{}";
  cJSON_AddNumberToObject(root,"version",1);
  cJSON_AddStringToObject(root,"device_id",deviceId);
  cJSON_AddStringToObject(root,"state",stateName(d.state));
  cJSON_AddNumberToObject(root,"uptime_ms",event?d.eventMs:d.frameMs);
  if(std::isfinite(d.peakC)) cJSON_AddNumberToObject(root,"peak_c",d.peakC);
  else cJSON_AddNullToObject(root,"peak_c");
  cJSON_AddNumberToObject(root,"threshold_c",d.settings.thresholdC);
  cJSON_AddNumberToObject(root,"activate_ms",d.settings.activateMs);
  cJSON_AddNumberToObject(root,"clear_ms",d.settings.clearMs);
  if(event) {
    char id[80];snprintf(id,sizeof(id),"%s-%08lx-%lu-fire",deviceId,(unsigned long)s.bootId,(unsigned long)d.sequence);
    cJSON_AddStringToObject(root,"event_id",id);
    cJSON_AddStringToObject(root,"transition",stateName(d.state));
  }
  char* encoded=cJSON_PrintUnformatted(root);String result=encoded?encoded:"{}";
  cJSON_free(encoded);cJSON_Delete(root);return result;
}
}
