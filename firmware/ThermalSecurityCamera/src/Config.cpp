#include "Config.h"
#include <cmath>
#include <cstring>
namespace thermal {
namespace {
bool textField(cJSON* obj, const char* key, char* dest, size_t size, String& err) {
  auto v = cJSON_GetObjectItemCaseSensitive(obj, key);
  if (!v) return true;
  if (!cJSON_IsString(v) || strlen(v->valuestring) >= size) { err = String("Invalid ") + key; return false; }
  for (const char* p = v->valuestring; *p; ++p) if (uint8_t(*p) < 32) { err = String("Control character in ") + key; return false; }
  strlcpy(dest, v->valuestring, size); return true;
}
bool number(cJSON* obj, const char* key, double& out, double lo, double hi, bool integer, String& err) {
  auto v = cJSON_GetObjectItemCaseSensitive(obj, key);
  if (!v) return true;
  if (!cJSON_IsNumber(v) || !std::isfinite(v->valuedouble) || v->valuedouble < lo ||
      v->valuedouble > hi || (integer && std::floor(v->valuedouble) != v->valuedouble)) {
    err = String("Invalid ") + key; return false;
  }
  out = v->valuedouble; return true;
}
}
String configJson(const Config& c, bool secrets) {
  cJSON* root = cJSON_CreateObject();
  if (!root) return "{}";
  cJSON_AddNumberToObject(root, "version", 1);
  cJSON_AddStringToObject(root, "ssid", c.ssid);
  cJSON_AddStringToObject(root, "broker", c.broker);
  cJSON_AddNumberToObject(root, "port", c.port);
  cJSON_AddBoolToObject(root, "flip_horizontal", c.flipHorizontal);
  cJSON_AddStringToObject(root, "palette", paletteName(c.palette));
  cJSON_AddBoolToObject(root, "ha_discovery", c.haDiscovery);
  cJSON_AddStringToObject(root, "mqtt_user", c.mqttUser);
  cJSON_AddStringToObject(root, "topic", c.topic);
  if (secrets) {
    cJSON_AddStringToObject(root, "wifi_password", c.wifiPassword);
    cJSON_AddStringToObject(root, "mqtt_password", c.mqttPassword);
  } else {
    cJSON_AddBoolToObject(root, "has_wifi_password", c.wifiPassword[0] != 0);
    cJSON_AddBoolToObject(root, "has_mqtt_password", c.mqttPassword[0] != 0);
  }
  cJSON* r = cJSON_AddObjectToObject(root, "roi");
  cJSON_AddNumberToObject(r, "x", c.detection.roi.x); cJSON_AddNumberToObject(r, "y", c.detection.roi.y);
  cJSON_AddNumberToObject(r, "width", c.detection.roi.width); cJSON_AddNumberToObject(r, "height", c.detection.roi.height);
  cJSON_AddNumberToObject(root, "delta_c", c.detection.deltaC);
  cJSON_AddNumberToObject(root, "min_pixels", c.detection.minPixels);
  cJSON_AddNumberToObject(root, "large_min_pixels", c.detection.largeMinPixels);
  cJSON_AddNumberToObject(root, "activate_ms", c.detection.activateMs);
  cJSON_AddNumberToObject(root, "clear_ms", c.detection.clearMs);
  cJSON_AddNumberToObject(root, "learn_ms", c.detection.learnMs);
  char* p = cJSON_PrintUnformatted(root); String result = p ? p : "{}";
  cJSON_free(p); cJSON_Delete(root); return result;
}
bool parseConfig(const char* json, Config& c, String& error) {
  cJSON* root = cJSON_ParseWithOpts(json, nullptr, true);
  if (!cJSON_IsObject(root)) { cJSON_Delete(root); error = "Expected one JSON object"; return false; }
  Config next = c;
  bool ok = textField(root,"ssid",next.ssid,sizeof(next.ssid),error) &&
    textField(root,"wifi_password",next.wifiPassword,sizeof(next.wifiPassword),error) &&
    textField(root,"broker",next.broker,sizeof(next.broker),error) &&
    textField(root,"mqtt_user",next.mqttUser,sizeof(next.mqttUser),error) &&
    textField(root,"mqtt_password",next.mqttPassword,sizeof(next.mqttPassword),error) &&
    textField(root,"topic",next.topic,sizeof(next.topic),error);
  double n = next.port; ok = ok && number(root,"port",n,1,65535,true,error); next.port = uint16_t(n);
  n = next.detection.deltaC; ok = ok && number(root,"delta_c",n,.2,100,false,error); next.detection.deltaC = n;
  n = next.detection.minPixels; ok = ok && number(root,"min_pixels",n,1,PixelCount,true,error); next.detection.minPixels = uint16_t(n);
  n = next.detection.largeMinPixels; ok = ok && number(root,"large_min_pixels",n,1,PixelCount,true,error); next.detection.largeMinPixels = uint16_t(n);
  n = next.detection.activateMs; ok = ok && number(root,"activate_ms",n,0,60000,true,error); next.detection.activateMs = uint32_t(n);
  n = next.detection.clearMs; ok = ok && number(root,"clear_ms",n,0,60000,true,error); next.detection.clearMs = uint32_t(n);
  n = next.detection.learnMs; ok = ok && number(root,"learn_ms",n,1000,120000,true,error); next.detection.learnMs = uint32_t(n);
  if(auto flip=cJSON_GetObjectItemCaseSensitive(root,"flip_horizontal")) {
    if(!cJSON_IsBool(flip)) { ok=false; error="Invalid flip_horizontal: expected boolean"; }
    else next.flipHorizontal=cJSON_IsTrue(flip);
  }
  if(auto palette=cJSON_GetObjectItemCaseSensitive(root,"palette")) {
    if(!cJSON_IsString(palette) || !parsePalette(palette->valuestring,next.palette)) {
      ok=false;error="Invalid palette: use fire, ironbow, rainbow, white_hot or black_hot";
    }
  }
  if(auto discovery=cJSON_GetObjectItemCaseSensitive(root,"ha_discovery")) {
    if(!cJSON_IsBool(discovery)) { ok=false;error="Invalid ha_discovery: expected boolean"; }
    else next.haDiscovery=cJSON_IsTrue(discovery);
  }
  if (auto r = cJSON_GetObjectItemCaseSensitive(root, "roi")) {
    if (!cJSON_IsObject(r)) { ok = false; error = "Invalid ROI"; }
    else {
      n = next.detection.roi.x; ok = ok && number(r,"x",n,0,79,true,error); next.detection.roi.x = int(n);
      n = next.detection.roi.y; ok = ok && number(r,"y",n,0,61,true,error); next.detection.roi.y = int(n);
      n = next.detection.roi.width; ok = ok && number(r,"width",n,1,80,true,error); next.detection.roi.width = int(n);
      n = next.detection.roi.height; ok = ok && number(r,"height",n,1,62,true,error); next.detection.roi.height = int(n);
    }
  }
  cJSON_Delete(root);
  if (!ok) return false;
  if (!validSettings(next.detection)) { error = "ROI or minimum object size is out of bounds"; return false; }
  const size_t pw = strlen(next.wifiPassword);
  if (pw && (pw < 8 || pw > 63)) { error = "Wi-Fi password must be empty or 8-63 characters"; return false; }
  if (!next.topic[0] || next.topic[0]=='$' || strchr(next.topic,'#') || strchr(next.topic,'+') || next.topic[strlen(next.topic)-1]=='/') {
    error = "Topic must be a nonempty prefix without wildcards or a trailing slash"; return false;
  }
  if (strchr(next.broker,'/') || strchr(next.broker,':') || strchr(next.broker,' ')) {
    error = "Broker must be a hostname or IPv4 address; set port separately"; return false;
  }
  c = next; return true;
}
bool ConfigStore::begin(const char* deviceId) {
  mutex_ = xSemaphoreCreateMutex();
  updateMutex_ = xSemaphoreCreateMutex();
  if (!mutex_ || !updateMutex_) return false;
  snprintf(value_.topic, sizeof(value_.topic), "thermal/%s", deviceId);
  ready_ = prefs_.begin("thermal", false);
  if (!ready_) return false;
  String saved = prefs_.getString("config", ""), error;
  if (saved.length()) {
    loaded_=parseConfig(saved.c_str(), value_, error);
    loadError_=loaded_?"none":error;
  }
  return true;
}
Config ConfigStore::get() {
  xSemaphoreTake(mutex_, portMAX_DELAY); Config result = value_; xSemaphoreGive(mutex_); return result;
}
String ConfigStore::json(bool secrets) { return configJson(get(), secrets); }
bool ConfigStore::update(const char* text, String& error) {
  xSemaphoreTake(updateMutex_, portMAX_DELAY);
  // The sensor task reads this configuration. Never hold its read mutex while
  // waiting for that task to acknowledge a pause before an NVS flash write.
  Config next = get();
  bool ok = parseConfig(text, next, error);
  if (ok) {
    const String serialized = configJson(next, true);
    ok = ready_ && serialized != "{}";
    if (!ok) error = "Could not serialize configuration";
    else if (beforeWrite_ && !beforeWrite_()) { ok=false;error="Sensor is busy; retry saving in a few seconds"; }
    else {
      ok = prefs_.putString("config", serialized) == serialized.length();
      if (ok) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        next.revision = value_.revision + 1; value_ = next;
        xSemaphoreGive(mutex_);
      } else error = "Could not persist configuration";
      if (afterWrite_) afterWrite_();
    }
  }
  xSemaphoreGive(updateMutex_); return ok;
}
}
