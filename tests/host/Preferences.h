#pragma once
#include "Arduino.h"
#include <map>
class Preferences {
 public:
  inline static std::map<std::string,String> values;
  bool begin(const char*,bool) { return true; }
  String getString(const char* key,const char* fallback) { auto it=values.find(key);return it==values.end()?String(fallback):it->second; }
  size_t putString(const char* key,const String& value) { values[key]=value;return value.length(); }
};
