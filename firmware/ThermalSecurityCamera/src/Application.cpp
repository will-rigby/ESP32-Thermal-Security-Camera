#include "Application.h"
#include "BuildOptions.h"
#include "Video.h"
#include "CapturePause.h"
#include <WaveshareSenxor.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <freertos/idf_additions.h>
#include <new>
#include <cmath>
namespace thermal {
ConfigStore configStore;
char deviceId[20];
std::atomic<bool> mqttConnected{false}, usbStreaming{false};
std::atomic<uint32_t> relearnGeneration{0};
std::atomic<bool> sensorRecoveryRequested{false};
void startNetwork();
void startWeb();
void addNetworkDiagnostics(cJSON* root);
namespace {
std::atomic<uint32_t> allocFailures{0},lastAllocSize{0},lastAllocCaps{0};
void allocationFailed(size_t size,uint32_t caps,const char*) {
  ++allocFailures;lastAllocSize=size;lastAllocCaps=caps;
}
SemaphoreHandle_t frameMutex, statusMutex;
Frame* latestFrame;
Status currentStatus;
CapturePause configPause;
bool beforeConfigWrite() {
  const uint32_t start=millis();
  while(!configPause.request()) {
    if(uint32_t(millis()-start)>3000) return false;
    delay(1);
  }
  while(!configPause.paused()) {
    if(uint32_t(millis()-start)>3000) { configPause.release();return false; }
    delay(1);
  }
  return true;
}
void afterConfigWrite() { configPause.release(); }
void acquisitionTask(void*) {
  auto dmem=ps_malloc(sizeof(Detector)); auto fmem=ps_malloc(sizeof(Frame));
  Detector* detector=dmem?new(dmem) Detector{}:nullptr;
  Frame* frame=fmem?new(fmem) Frame{}:nullptr;
  WaveshareSenxor sensor;
  bool ready=detector && frame && (THERMAL_TEST_PATTERN || sensor.begin());
  Config cfg=configStore.get(); uint32_t revision=cfg.revision, relearn=relearnGeneration.load();
  if(detector) detector->configure(cfg.detection);
  uint32_t frames=0,lastValid=0,fpsStart=millis(),fpsFrames=0; float fps=0;
  uint32_t firstFrameMs=0,warmupRemaining=THERMAL_TEST_PATTERN?0:THERMAL_SENSOR_WARMUP_MS;
  uint32_t recoveries=0;
  bool warmupStarted=false;
  for (;;) {
    if(configPause.claim()) {
      if(ready && !THERMAL_TEST_PATTERN) sensor.pause();
      xSemaphoreTake(statusMutex,portMAX_DELAY);
      currentStatus.detection=detector?detector->unavailable(millis()):Detection{};
      currentStatus.sensorReady=false;
      ++currentStatus.sensorConfigPauses;
      strlcpy(currentStatus.sensorError,"saving settings",sizeof(currentStatus.sensorError));
      xSemaphoreGive(statusMutex);
      configPause.acknowledge();
    }
    if(configPause.paused()) { vTaskDelay(1);continue; }
    if(configPause.resumeRequested()) {
      if(ready && !THERMAL_TEST_PATTERN) sensor.resume();
      configPause.resumed();
    }
    cfg=configStore.get();
    if (detector && (revision!=cfg.revision || relearn!=relearnGeneration.load())) {
      detector->configure(cfg.detection); revision=cfg.revision; relearn=relearnGeneration.load();
    }
    uint32_t now=millis();
    bool fresh=false;
    if(sensorRecoveryRequested.exchange(false) && ready && !THERMAL_TEST_PATTERN) {
      sensor.requestRecovery();
      xSemaphoreTake(statusMutex,portMAX_DELAY);
      currentStatus.detection=detector->unavailable(now);
      currentStatus.sensorReady=false;currentStatus.sensorRecovering=true;
      strlcpy(currentStatus.sensorError,sensor.error(),sizeof(currentStatus.sensorError));
      xSemaphoreGive(statusMutex);
    }
    if(ready) {
      if(THERMAL_TEST_PATTERN) {
        if(uint32_t(now-lastValid)>=100) {
          for(int y=0;y<SensorHeight;++y) for(int x=0;x<SensorWidth;++x) {
            const int bx=int((now/120)%100)-10;
            frame->pixels[y*SensorWidth+x]=20.f+float(y)/SensorHeight +
              ((now>15000 && x>=bx && x<bx+8 && y>=20 && y<42)?12.f:0.f);
          }
          fresh=true;
        }
      } else fresh=sensor.read(frame->pixels);
    }
    now=millis();  // Sensor reinitialization can take time.
    if(recoveries!=sensor.recoveries()) {
      recoveries=sensor.recoveries();
      warmupStarted=false; warmupRemaining=THERMAL_SENSOR_WARMUP_MS;
      detector->unavailable(now);
    }
    const bool recovering=!THERMAL_TEST_PATTERN && sensor.recovering();
    if(fresh) {
      if(!warmupStarted) { firstFrameMs=now; warmupStarted=true; }
      const uint32_t warmingFor=uint32_t(now-firstFrameMs);
      warmupRemaining=!THERMAL_TEST_PATTERN && warmingFor<THERMAL_SENSOR_WARMUP_MS?THERMAL_SENSOR_WARMUP_MS-warmingFor:0;
      frame->detection=detector->process(frame->pixels,now,warmupRemaining>0);
      if(frame->detection.state==Occupancy::Unavailable) fresh=false;
    }
    if(fresh) {
      frame->roi=cfg.detection.roi;
      frame->flipHorizontal=cfg.flipHorizontal;
      frame->palette=cfg.palette;
      memcpy(frame->mask,detector->mask(),PixelCount);
      frame->serial=++frames; lastValid=now; ++fpsFrames;
      if(now-fpsStart>=1000) { fps=fpsFrames*1000.f/(now-fpsStart); fpsStart=now; fpsFrames=0; }
      xSemaphoreTake(frameMutex,portMAX_DELAY); *latestFrame=*frame; xSemaphoreGive(frameMutex);
    }
    const bool healthy=ready && !recovering && frames && uint32_t(now-lastValid)<=1000;
    xSemaphoreTake(statusMutex,portMAX_DELAY);
    if(fresh) currentStatus.detection=frame->detection;
    if(!healthy) {
      currentStatus.detection=detector?detector->unavailable(now):Detection{};
      currentStatus.detection.state=Occupancy::Unavailable;
    }
    currentStatus.roi=cfg.detection.roi; currentStatus.frameSerial=frames;
    currentStatus.flipHorizontal=cfg.flipHorizontal;
    currentStatus.palette=cfg.palette;
    currentStatus.lastFrameMs=lastValid; currentStatus.sensorReady=healthy;
    currentStatus.sensorErrors=sensor.errors(); currentStatus.fps=healthy?fps:0;
    currentStatus.sensorRecoveries=sensor.recoveries();
    currentStatus.sensorLastErrorCode=sensor.lastErrorCode();
    currentStatus.sensorRecovering=recovering;
    currentStatus.sensorWarmupMs=warmupRemaining;
    strlcpy(currentStatus.sensorError,!ready?(detector&&frame?sensor.error():"PSRAM allocation failed"):
      recovering?sensor.error():healthy?(THERMAL_TEST_PATTERN?"TEST PATTERN - MQTT disabled":"none"):"waiting for a valid sensor frame",sizeof(currentStatus.sensorError));
    xSemaphoreGive(statusMutex);
    vTaskDelay(1);
  }
}
}
Status getStatus() {
  xSemaphoreTake(statusMutex,portMAX_DELAY); Status s=currentStatus; xSemaphoreGive(statusMutex); return s;
}
bool copyFrame(Frame& target,uint32_t afterSerial) {
  if(xSemaphoreTake(frameMutex,pdMS_TO_TICKS(5))!=pdTRUE) return false;
  const bool fresh=latestFrame->serial!=afterSerial;
  if(fresh) target=*latestFrame;
  xSemaphoreGive(frameMutex); return fresh;
}
String detectionJson(const Status& s,bool event,ObjectClass objectClass) {
  const auto& detection=classDetection(s.detection,objectClass);
  cJSON* root=cJSON_CreateObject();
  cJSON_AddNumberToObject(root,"version",1); cJSON_AddStringToObject(root,"device_id",deviceId);
  cJSON_AddStringToObject(root,"roi","roi-1"); cJSON_AddStringToObject(root,"state",stateName(detection.state));
  cJSON_AddStringToObject(root,"object_class",className(objectClass));
  if(event) {
    char id[80]; snprintf(id,sizeof(id),"%s-%08lx-%lu-%s",deviceId,(unsigned long)s.bootId,(unsigned long)detection.sequence,className(objectClass));
    cJSON_AddStringToObject(root,"event_id",id);
    cJSON_AddStringToObject(root,"transition",stateName(detection.state));
  }
  cJSON_AddNumberToObject(root,"uptime_ms",event?detection.eventMs:detection.frameMs);
  const bool known=detection.state==Occupancy::Clear || detection.state==Occupancy::Occupied;
  if(known && (objectClass==ObjectClass::Any || detection.pixels)) cJSON_AddNumberToObject(root,"peak_c",detection.peakC); else cJSON_AddNullToObject(root,"peak_c");
  cJSON_AddNumberToObject(root,"pixels",detection.pixels);
  cJSON* roi=cJSON_AddObjectToObject(root,"roi_bounds");
  cJSON_AddNumberToObject(roi,"x",s.roi.x); cJSON_AddNumberToObject(roi,"y",s.roi.y);
  cJSON_AddNumberToObject(roi,"width",s.roi.width); cJSON_AddNumberToObject(roi,"height",s.roi.height);
  cJSON* b=cJSON_AddObjectToObject(root,"bounds");
  cJSON_AddNumberToObject(b,"x",detection.bounds.x); cJSON_AddNumberToObject(b,"y",detection.bounds.y);
  cJSON_AddNumberToObject(b,"width",detection.bounds.width); cJSON_AddNumberToObject(b,"height",detection.bounds.height);
  char* text=cJSON_PrintUnformatted(root); String result=text?text:"{}";
  cJSON_free(text); cJSON_Delete(root); return result;
}
String statusJson() {
  Status s=getStatus(); cJSON* root=cJSON_Parse(detectionJson(s,false).c_str());
  if(!root) return "{}";
  // Keep status compact: do not duplicate device/ROI metadata or construct and
  // parse two complete MQTT payloads on this memory-constrained board.
  for(auto objectClass:{ObjectClass::Small,ObjectClass::Large}) {
    const auto& d=classDetection(s.detection,objectClass);
    cJSON* channel=cJSON_AddObjectToObject(root,className(objectClass));
    cJSON_AddStringToObject(channel,"state",stateName(d.state));
    cJSON_AddNumberToObject(channel,"pixels",d.pixels);
    const bool known=d.state==Occupancy::Clear || d.state==Occupancy::Occupied;
    if(known && d.pixels) cJSON_AddNumberToObject(channel,"peak_c",d.peakC);else cJSON_AddNullToObject(channel,"peak_c");
    cJSON* bounds=cJSON_AddObjectToObject(channel,"bounds");
    cJSON_AddNumberToObject(bounds,"x",d.bounds.x);cJSON_AddNumberToObject(bounds,"y",d.bounds.y);
    cJSON_AddNumberToObject(bounds,"width",d.bounds.width);cJSON_AddNumberToObject(bounds,"height",d.bounds.height);
  }
  cJSON_AddNumberToObject(root,"fps",s.fps); cJSON_AddNumberToObject(root,"frames",s.frameSerial);
  cJSON_AddNumberToObject(root,"frame_age_ms",uint32_t(millis()-s.lastFrameMs));
  cJSON_AddBoolToObject(root,"sensor_ready",s.sensorReady);
  cJSON_AddStringToObject(root,"sensor_error",s.sensorError);
  cJSON_AddNumberToObject(root,"sensor_errors",s.sensorErrors);
  cJSON_AddNumberToObject(root,"sensor_recoveries",s.sensorRecoveries);
  cJSON_AddNumberToObject(root,"sensor_config_pauses",s.sensorConfigPauses);
  cJSON_AddNumberToObject(root,"sensor_last_error_code",s.sensorLastErrorCode);
  cJSON_AddBoolToObject(root,"sensor_recovering",s.sensorRecovering);
  cJSON_AddNumberToObject(root,"sensor_warmup_ms",s.sensorWarmupMs);
  cJSON_AddBoolToObject(root,"flip_horizontal",s.flipHorizontal);
  cJSON_AddStringToObject(root,"palette",paletteName(s.palette));
  cJSON_AddNumberToObject(root,"min_c",s.detection.minC); cJSON_AddNumberToObject(root,"max_c",s.detection.maxC);
  cJSON_AddNumberToObject(root,"free_heap",ESP.getFreeHeap()); cJSON_AddNumberToObject(root,"free_psram",ESP.getFreePsram());
  cJSON_AddNumberToObject(root,"free_internal_heap",heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
  cJSON_AddNumberToObject(root,"min_internal_heap",heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
  cJSON_AddNumberToObject(root,"largest_internal_block",heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
  const auto video=videoDiagnostics();
  cJSON_AddNumberToObject(root,"video_fps",video.fps);
  cJSON_AddNumberToObject(root,"video_frames",video.frames);
  cJSON_AddNumberToObject(root,"jpeg_bytes",video.jpegBytes);
  cJSON_AddNumberToObject(root,"render_ms",video.renderMs);
  cJSON_AddBoolToObject(root,"video_enabled",videoEnabled());
  cJSON_AddBoolToObject(root,"config_loaded",configStore.loaded());
  cJSON_AddStringToObject(root,"config_load_error",configStore.loadError());
  cJSON_AddNumberToObject(root,"heap_alloc_failures",allocFailures.load());
  cJSON_AddNumberToObject(root,"last_failed_alloc_size",lastAllocSize.load());
  cJSON_AddNumberToObject(root,"last_failed_alloc_caps",lastAllocCaps.load());
  addNetworkDiagnostics(root);
  cJSON_AddStringToObject(root,"firmware","0.2.2"); cJSON_AddStringToObject(root,"arduino","3.3.12");
  cJSON_AddStringToObject(root,"idf",esp_get_idf_version());
  cJSON_AddNumberToObject(root,"reset_reason",int(esp_reset_reason()));
  cJSON_AddBoolToObject(root,"mqtt_connected",mqttConnected.load());
  cJSON_AddBoolToObject(root,"mqtt_configured",configStore.get().broker[0]!=0);
  cJSON_AddBoolToObject(root,"wifi_connected",WiFi.status()==WL_CONNECTED);
  cJSON_AddBoolToObject(root,"test_pattern",THERMAL_TEST_PATTERN);
  cJSON_AddStringToObject(root,"usb",usbStatus());
  cJSON_AddStringToObject(root,"ip",WiFi.localIP().toString().c_str());
  char* text=cJSON_PrintUnformatted(root); String result=text?text:"{}";
  cJSON_free(text); cJSON_Delete(root); return result;
}
void startApplication() {
  Serial.begin(115200);  // Never wait for a USB/serial host.
  // Reserve scarce internal RAM for DMA, Wi-Fi and tasks that write NVS.
  // The pinned core defaults to keeping mallocs up to 4096 bytes internal.
  if(psramFound()) heap_caps_malloc_extmem_enable(256);
  heap_caps_register_failed_alloc_callback(allocationFailed);
  uint8_t mac[6]; esp_read_mac(mac,ESP_MAC_WIFI_STA);
  snprintf(deviceId,sizeof(deviceId),"%02x%02x%02x%02x%02x%02x",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
  frameMutex=xSemaphoreCreateMutex(); statusMutex=xSemaphoreCreateMutex();
  void* memory=ps_malloc(sizeof(Frame)); latestFrame=memory?new(memory) Frame{}:nullptr;
  if(!frameMutex||!statusMutex||!latestFrame||!configStore.begin(deviceId)) {
    Serial.println("Startup failed: check OPI PSRAM and NVS settings"); return;
  }
  currentStatus.bootId=esp_random();
  configStore.setWriteGuard(beforeConfigWrite,afterConfigWrite);
  // This task only reads the sensor's separate SPI calibration flash. It must
  // never write the ESP32's program/NVS flash from its external-RAM stack.
  if(xTaskCreatePinnedToCoreWithCaps(acquisitionTask,"thermal-sensor",8192,nullptr,5,nullptr,1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)!=pdPASS)
    Serial.println("Sensor task creation failed");
  WiFi.persistent(false); WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_STA); WiFi.setSleep(false);
  startNetwork(); startWeb();
  if(!startVideo()) Serial.println("Video allocation/task failed");
  startUsb();
}
}
