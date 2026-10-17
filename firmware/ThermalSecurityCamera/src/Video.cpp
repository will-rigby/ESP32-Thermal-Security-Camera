#include "Video.h"
#include <esp_heap_caps.h>
#include <freertos/idf_additions.h>
#include <new>
#include <driver/temperature_sensor.h>
namespace thermal {
namespace {
SemaphoreHandle_t videoMutex;
uint8_t *latest,*rendered;
Frame* renderFrame;
// Single owner: the video task. Keep LUT workspace off its 4 KB stack.
ToneMapper toneMapper;
uint32_t latestSerial=0,latestFrameMs=0;
uint32_t cachedPeriod=50,temperatureMs=0;
bool valid=false;
VideoDiagnostics diagnostics;
std::atomic<bool> enabled{true};
void videoTask(void*) {
  uint32_t last=0,lastTemperature=millis()-2000;
  temperature_sensor_handle_t thermometer=nullptr;
  VideoTiming timing;
  for(;;) {
    const TickType_t started=xTaskGetTickCount();
    // Use the same clock for scheduling and task delay. A task preemption
    // between reading ticks and millis must not skip a 20 FPS time slot.
    const uint32_t cycleMs=uint32_t(started)*portTICK_PERIOD_MS;
    if(uint32_t(cycleMs-lastTemperature)>=2000) {
      lastTemperature=cycleMs;
      if(!thermometer) {
        temperature_sensor_config_t config=TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10,80);
        if(temperature_sensor_install(&config,&thermometer)==ESP_OK &&
           temperature_sensor_enable(thermometer)!=ESP_OK) {
          temperature_sensor_uninstall(thermometer);thermometer=nullptr;
        }
      }
      float temperature=NAN;
      if(!thermometer || temperature_sensor_get_celsius(thermometer,&temperature)!=ESP_OK ||
         !std::isfinite(temperature) || temperature< -10 || temperature>80)temperature=NAN;
      xSemaphoreTake(videoMutex,portMAX_DELAY);
      diagnostics.temperatureC=temperature;temperatureMs=millis();
      xSemaphoreGive(videoMutex);
    }
    timing.setPeriod(configStore.get().videoPeriodMs);
    const auto health=getStatus();
    if(!enabled.load() || !health.sensorReady || uint32_t(millis()-health.lastFrameMs)>1000) {
      xSemaphoreTake(videoMutex,portMAX_DELAY);valid=false;diagnostics.fps=0;xSemaphoreGive(videoMutex);
      timing.reset();last=0;toneMapper.reset();
    } else if(timing.due(cycleMs) && copyFrame(*renderFrame,last) && renderFrame->detection.state!=Occupancy::Unavailable &&
              uint32_t(millis()-renderFrame->detection.frameMs)<=1000) {
      const uint32_t renderStart=micros();
      toneMapper.update(renderFrame->pixels,PixelCount,renderFrame->detection.minC,renderFrame->detection.maxC,
                        renderFrame->toneMapping,renderFrame->detection.frameMs);
      renderVideoPacket(renderFrame->pixels,renderFrame->detection,renderFrame->roi,
                        renderFrame->flipHorizontal,renderFrame->palette,renderFrame->serial,rendered,
                        &toneMapper,renderFrame->toneMapping);
      last=renderFrame->serial;
      const uint32_t elapsed=micros()-renderStart;
      xSemaphoreTake(videoMutex,portMAX_DELAY);
      memcpy(latest,rendered,VideoPacketBytes);latestSerial=last;
      latestFrameMs=renderFrame->detection.frameMs;cachedPeriod=timing.period;valid=true;
      timing.published(cycleMs);
      ++diagnostics.frames;diagnostics.renderUs=elapsed;diagnostics.fps=timing.fps;
      xSemaphoreGive(videoMutex);
    }
    // Always yield, including overruns, to avoid starving core 0's idle task.
    const TickType_t elapsed=xTaskGetTickCount()-started,period=pdMS_TO_TICKS(VideoPeriodMs);
    vTaskDelay(elapsed<period?period-elapsed:1);
  }
}
}
bool startVideo() {
  videoMutex=xSemaphoreCreateMutex();
  latest=static_cast<uint8_t*>(ps_malloc(VideoPacketBytes));
  rendered=static_cast<uint8_t*>(ps_malloc(VideoPacketBytes));
  void* memory=ps_malloc(sizeof(Frame));renderFrame=memory?new(memory) Frame{}:nullptr;
  if(!videoMutex||!latest||!rendered||!renderFrame)return false;
  return xTaskCreatePinnedToCoreWithCaps(videoTask,"thermal-video",4096,nullptr,1,nullptr,0,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)==pdPASS;
}
VideoDiagnostics videoDiagnostics() {
  if(!videoMutex)return {};
  const auto health=getStatus();
  xSemaphoreTake(videoMutex,portMAX_DELAY);auto result=diagnostics;
  if(!valid || !enabled.load() || !health.sensorReady || uint32_t(millis()-health.lastFrameMs)>1000 ||
     !heldFrameFresh(millis(),latestFrameMs,cachedPeriod))result.fps=0;
  if(uint32_t(millis()-temperatureMs)>5000)result.temperatureC=NAN;
  xSemaphoreGive(videoMutex);return result;
}
void setVideoEnabled(bool value) {
  enabled=value;
  if(!value && videoMutex){xSemaphoreTake(videoMutex,portMAX_DELAY);valid=false;xSemaphoreGive(videoMutex);}
}
bool videoEnabled() { return enabled.load(); }
bool videoFrameUsable(uint32_t frameMs) {
  if(!videoMutex || !enabled.load())return false;
  const auto health=getStatus();
  if(!health.sensorReady || uint32_t(millis()-health.lastFrameMs)>1000)return false;
  if(xSemaphoreTake(videoMutex,pdMS_TO_TICKS(5))!=pdTRUE)return false;
  const bool ok=valid && latestFrameMs==frameMs && heldFrameFresh(millis(),frameMs,cachedPeriod);
  xSemaphoreGive(videoMutex);return ok;
}
bool copyVideo(uint8_t* target,size_t capacity,uint32_t afterSerial) {
  if(!videoMutex || !enabled.load() || capacity<VideoPacketBytes)return false;
  const auto health=getStatus();
  if(!health.sensorReady || uint32_t(millis()-health.lastFrameMs)>1000)return false;
  if(xSemaphoreTake(videoMutex,pdMS_TO_TICKS(5))!=pdTRUE)return false;
  const bool ok=valid && latestSerial!=afterSerial && heldFrameFresh(millis(),latestFrameMs,cachedPeriod);
  if(ok)memcpy(target,latest,VideoPacketBytes);
  xSemaphoreGive(videoMutex);return ok;
}
}
