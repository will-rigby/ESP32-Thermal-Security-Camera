#pragma once
#include "Config.h"
namespace thermal {
String discoveryTopic(const char* id,ObjectClass objectClass);
String discoveryJson(const Config& config,const char* id,ObjectClass objectClass);
}
