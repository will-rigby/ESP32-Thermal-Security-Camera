#include "UsbDiagnostics.h"
#include "Application.h"
#include "BuildOptions.h"
#include "Video.h"
#include "HaDiscovery.h"
#include <algorithm>
#if THERMAL_ENABLE_USB
#include <USBCDC.h>
#include <esp32-hal-tinyusb.h>
#include <esp_system.h>
namespace {
USBCDC console;
String command;
bool overlong=false;
void reply(const String& value) {
  // Yield between endpoint-sized chunks so long JSON replies cannot starve
  // USB servicing or be silently truncated by USBCDC's whole-write timeout.
  for(size_t offset=0;offset<value.length();) {
    const size_t count=std::min(size_t(64),value.length()-offset);
    const size_t sent=console.write(reinterpret_cast<const uint8_t*>(value.c_str()+offset),count);
    if(!sent) return;
    offset+=sent;console.flush();delay(1);
  }
  console.println();
}
void dispatch() {
  command.trim();
  if(command=="STATUS") reply(thermal::statusJson());
  else if(command=="CONFIG") reply(thermal::configStore.json()); // Always redacted.
  else if(command=="DISCOVERY") {
    cJSON* items=cJSON_CreateArray();const auto config=thermal::configStore.get();
    for(unsigned i=0;i<3;++i) {
      const auto objectClass=thermal::ObjectClass(i);
      cJSON* item=cJSON_CreateObject();cJSON_AddItemToArray(items,item);
      cJSON_AddStringToObject(item,"topic",thermal::discoveryTopic(thermal::deviceId,objectClass).c_str());
      const String payload=thermal::discoveryJson(config,thermal::deviceId,objectClass);
      cJSON_AddItemToObject(item,"payload",payload.length()?cJSON_Parse(payload.c_str()):cJSON_CreateString(""));
    }
    char* text=cJSON_PrintUnformatted(items);reply(text?text:"[]");cJSON_free(text);cJSON_Delete(items);
  }
  else if(command.startsWith("SET ")) {
    String error;
    if(thermal::configStore.update(command.c_str()+4,error)) reply("{\"saved\":true}");
    else {
      cJSON* root=cJSON_CreateObject();cJSON_AddStringToObject(root,"error",error.c_str());
      char* text=cJSON_PrintUnformatted(root);reply(text?text:"{\"error\":\"No memory\"}");
      cJSON_free(text);cJSON_Delete(root);
    }
  } else if(command=="VIDEO OFF" || command=="VIDEO ON") {
    thermal::setVideoEnabled(command=="VIDEO ON");reply("{\"accepted\":true}");
  } else if(command=="SENSOR RECOVER") {
    thermal::sensorRecoveryRequested=true;reply("{\"accepted\":true}");
  } else if(command=="REBOOT" || command=="BOOTLOADER") {
    const bool bootloader=command=="BOOTLOADER";
    reply("{\"restarting\":true}");console.flush();delay(100);
    if(bootloader) usb_persist_restart(RESTART_BOOTLOADER); else ESP.restart();
  } else if(command.length()) reply("{\"commands\":[\"STATUS\",\"CONFIG\",\"DISCOVERY\",\"SET {json}\",\"VIDEO OFF\",\"VIDEO ON\",\"SENSOR RECOVER\",\"REBOOT\",\"BOOTLOADER\"]}");
}
}
#endif
namespace thermal {
void startUsbDiagnostics() {
#if THERMAL_ENABLE_USB
  command.reserve(4096);
  // Windows may toggle RTS/DTR while opening a port. Recovery is explicit
  // through BOOTLOADER, avoiding accidental line-sequence resets.
  console.enableReboot(false);
  console.setRxBufferSize(4096);
  console.setTxTimeoutMs(250);
  console.begin(115200);
#endif
}
void serviceUsbDiagnostics() {
#if THERMAL_ENABLE_USB
  // Runs on Arduino's internal-RAM loop stack: SET may write Preferences.
  if(!console) { command="";overlong=false;return; }
  for(unsigned budget=0;budget<512 && console.available()>0;++budget) {
    const char c=char(console.read());
    if(c=='\n') {
      if(overlong) reply("{\"error\":\"Command too long\"}");else dispatch();
      command="";overlong=false;
    } else if(!overlong && c!='\r') {
      if(command.length()>=4095) { command="";overlong=true; }else command+=c;
    }
  }
#endif
}
}
