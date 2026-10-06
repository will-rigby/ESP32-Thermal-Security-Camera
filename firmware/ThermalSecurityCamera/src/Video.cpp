#include "Video.h"
#include <esp_heap_caps.h>
#include <freertos/idf_additions.h>
#include <new>
namespace thermal {
namespace {
SemaphoreHandle_t videoMutex;
uint8_t *latest,*rendered;
Frame* renderFrame;
uint32_t latestSerial=0,latestFrameMs=0;
bool valid=false;
VideoDiagnostics diagnostics;
std::atomic<bool> enabled{true};
void videoTask(void*) {
  uint32_t last=0,fpsStart=millis(),fpsFrames=0;
  for(;;) {
    const TickType_t started=xTaskGetTickCount();
    const auto health=getStatus();
    if(!enabled.load() || !health.sensorReady) {
      xSemaphoreTake(videoMutex,portMAX_DELAY);valid=false;diagnostics.fps=0;xSemaphoreGive(videoMutex);
      fpsStart=millis();fpsFrames=0;
    } else if(copyFrame(*renderFrame,last) && renderFrame->detection.state!=Occupancy::Unavailable &&
              uint32_t(millis()-renderFrame->detection.frameMs)<=1000) {
      const uint32_t renderStart=micros();
      renderVideoPacket(renderFrame->pixels,renderFrame->detection,renderFrame->roi,
                        renderFrame->flipHorizontal,renderFrame->palette,renderFrame->serial,rendered);
      last=renderFrame->serial;
      const uint32_t elapsed=micros()-renderStart,now=millis();
      xSemaphoreTake(videoMutex,portMAX_DELAY);
      memcpy(latest,rendered,VideoPacketBytes);latestSerial=last;
      latestFrameMs=renderFrame->detection.frameMs;valid=true;
      ++diagnostics.frames;++fpsFrames;diagnostics.renderUs=elapsed;
      if(uint32_t(now-fpsStart)>=1000) {
        diagnostics.fps=fpsFrames*1000.f/uint32_t(now-fpsStart);fpsFrames=0;fpsStart=now;
      }
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
  xSemaphoreTake(videoMutex,portMAX_DELAY);auto result=diagnostics;
  if(!valid || !enabled.load() || uint32_t(millis()-latestFrameMs)>1000)result.fps=0;
  xSemaphoreGive(videoMutex);return result;
}
void setVideoEnabled(bool value) { enabled=value; }
bool videoEnabled() { return enabled.load(); }
bool copyVideo(uint8_t* target,size_t capacity,uint32_t afterSerial) {
  if(!videoMutex || !enabled.load() || capacity<VideoPacketBytes)return false;
  const auto health=getStatus();
  if(!health.sensorReady || uint32_t(millis()-health.lastFrameMs)>1000)return false;
  if(xSemaphoreTake(videoMutex,pdMS_TO_TICKS(5))!=pdTRUE)return false;
  const bool ok=valid && latestSerial!=afterSerial && uint32_t(millis()-latestFrameMs)<=1000;
  if(ok)memcpy(target,latest,VideoPacketBytes);
  xSemaphoreGive(videoMutex);return ok;
}
}
