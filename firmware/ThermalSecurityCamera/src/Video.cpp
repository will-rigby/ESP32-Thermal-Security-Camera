#include "Video.h"
#include "BuildOptions.h"
#include "Orientation.h"
#include <img_converters.h>
#include <esp_heap_caps.h>
#include <freertos/idf_additions.h>
#include <new>
#include <algorithm>
namespace thermal {
namespace {
SemaphoreHandle_t jpegMutex;
uint8_t *latest, *rgb, *encoded, *nativeRgb;
size_t latestLength = 0;
uint32_t latestSerial = 0;
uint32_t latestFrameMs = 0;
Frame* renderFrame;
VideoDiagnostics diagnostics;
std::atomic<bool> enabled{true};
struct JpegOutput { size_t length=0; bool overflow=false; };
size_t writeJpeg(void* context,size_t index,const void* data,size_t length) {
  auto& output=*static_cast<JpegOutput*>(context);
  if(index>JpegCapacity || length>JpegCapacity-index) { output.overflow=true;return 0; }
  if(length) memcpy(encoded+index,data,length);
  output.length=std::max(output.length,index+length);
  return length;
}
void pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
  if (x < 0 || y < 0 || x >= VideoWidth || y >= VideoHeight) return;
  auto p = rgb + (y * VideoWidth + x) * 3;
  // esp32-camera's RGB888 converter expects B, G, R byte order.
  p[0] = b; p[1] = g; p[2] = r;
}
void box(const Roi& r, uint8_t red, uint8_t green, uint8_t blue) {
  if (r.width <= 0 || r.height <= 0) return;
  const int x0 = ImageX+r.x*ImageWidth/SensorWidth, x1 = ImageX+(r.x+r.width)*ImageWidth/SensorWidth-1;
  const int y0 = r.y*VideoHeight/SensorHeight, y1 = (r.y+r.height)*VideoHeight/SensorHeight-1;
  for (int x=x0; x<=x1; ++x) { pixel(x,y0,red,green,blue); pixel(x,y1,red,green,blue); }
  for (int y=y0; y<=y1; ++y) { pixel(x0,y,red,green,blue); pixel(x1,y,red,green,blue); }
}
void render(const Frame& frame) {
  memset(rgb, 0, VideoWidth*VideoHeight*3);
  float low = frame.detection.minC, range = std::max(1.f,frame.detection.maxC-low);
  // Calculate the palette at sensor resolution, then scale its byte values.
  // Repeating floating-point palette work for all 76,800 output pixels can
  // exceed the frame budget on this board, particularly while USB is active.
  for (int i=0; i<PixelCount; ++i) {
    const float v=std::max(0.f,std::min(1.f,(frame.pixels[i]-low)/range));
    const auto colour=paletteColour(frame.palette,uint8_t(v*255));
    nativeRgb[i*3]=colour.b; nativeRgb[i*3+1]=colour.g; nativeRgb[i*3+2]=colour.r;
  }
  for (int y=0; y<VideoHeight; ++y) for (int x=0; x<ImageWidth; ++x) {
    const int sourceX=orientedColumn(x*SensorWidth/ImageWidth,frame.flipHorizontal);
    const int i=((y*SensorHeight/VideoHeight)*SensorWidth+sourceX)*3;
    auto p=rgb+(y*VideoWidth+x+ImageX)*3;
    p[0]=nativeRgb[i]; p[1]=nativeRgb[i+1]; p[2]=nativeRgb[i+2];
  }
  box(orientedRoi(frame.roi,frame.flipHorizontal),0,220,255);
  box(orientedRoi(frame.detection.bounds,frame.flipHorizontal),80,255,80);
  // State stripe is visible in USB clients as well as the browser.
  const auto s=frame.detection.state;
  for(int y=0;y<3;++y) for(int x=0;x<VideoWidth;++x)
    pixel(x,y,s==Occupancy::Occupied?255:40,s==Occupancy::Clear?220:80,s==Occupancy::Learning?255:40);
  if (THERMAL_TEST_PATTERN) for(int x=0;x<VideoWidth;x+=8) for(int y=VideoHeight-8;y<VideoHeight;++y)
    for(int dx=0;dx<4;++dx) pixel(x+dx,y,255,0,255);
}
void videoTask(void*) {
  uint32_t last=0;
  uint32_t fpsStart=millis(),fpsFrames=0;
  for (;;) {
    if(!enabled.load()) {
      xSemaphoreTake(jpegMutex,portMAX_DELAY);latestLength=0;diagnostics.fps=0;xSemaphoreGive(jpegMutex);
      fpsStart=millis();fpsFrames=0;vTaskDelay(pdMS_TO_TICKS(100));continue;
    }
    const TickType_t started=xTaskGetTickCount();
    const uint32_t renderStart=millis();
    if (copyFrame(*renderFrame,last)) {
      last=renderFrame->serial; render(*renderFrame);
      // The optimized encoder reserved ~35 KB of internal RAM on this SDK,
      // starving Wi-Fi. This converter follows the PSRAM malloc policy.
      JpegOutput output;
      if (fmt2jpg_cb(rgb,VideoWidth*VideoHeight*3,VideoWidth,VideoHeight,PIXFORMAT_RGB888,60,writeJpeg,&output)) {
        const size_t length=output.length;
        if (!output.overflow && length>0 && length<=JpegCapacity) {
          xSemaphoreTake(jpegMutex,portMAX_DELAY);
          memcpy(latest,encoded,length); latestLength=length; latestSerial=last;
          latestFrameMs=renderFrame->detection.frameMs;
          ++diagnostics.frames; ++fpsFrames;
          diagnostics.jpegBytes=length;
          diagnostics.renderMs=millis()-renderStart;
          const uint32_t now=millis();
          if(uint32_t(now-fpsStart)>=1000) {
            diagnostics.fps=fpsFrames*1000.f/uint32_t(now-fpsStart);
            fpsFrames=0; fpsStart=now;
          }
          xSemaphoreGive(jpegMutex);
        }
      }
    }
    // Always block, including overruns. A periodic deadline that stays in the
    // past starves core 0's idle task and causes a five-second watchdog reset.
    const TickType_t elapsed=xTaskGetTickCount()-started;
    const TickType_t period=pdMS_TO_TICKS(100);
    vTaskDelay(elapsed<period?period-elapsed:1);
  }
}
}
bool startVideo() {
  jpegMutex=xSemaphoreCreateMutex();
  latest=static_cast<uint8_t*>(ps_malloc(JpegCapacity));
  encoded=static_cast<uint8_t*>(heap_caps_aligned_alloc(16,JpegCapacity,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  rgb=static_cast<uint8_t*>(heap_caps_aligned_alloc(16,VideoWidth*VideoHeight*3,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  nativeRgb=static_cast<uint8_t*>(ps_malloc(PixelCount*3));
  void* f=ps_malloc(sizeof(Frame)); renderFrame=f?new(f) Frame{}:nullptr;
  if (!jpegMutex||!latest||!encoded||!rgb||!nativeRgb||!renderFrame) return false;
  return xTaskCreatePinnedToCoreWithCaps(videoTask,"thermal-video",6144,nullptr,1,nullptr,0,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)==pdPASS;
}
VideoDiagnostics videoDiagnostics() {
  if(!jpegMutex) return {};
  xSemaphoreTake(jpegMutex,portMAX_DELAY);
  auto result=diagnostics;
  if(uint32_t(millis()-latestFrameMs)>1000) result.fps=0;
  xSemaphoreGive(jpegMutex);
  return result;
}
void setVideoEnabled(bool value) { enabled=value; }
bool videoEnabled() { return enabled.load(); }
bool copyJpeg(uint8_t* target,size_t capacity,size_t& length,uint32_t& serial) {
  if(!jpegMutex) return false;
  const auto health=getStatus();
  if(!health.sensorReady || uint32_t(millis()-health.lastFrameMs)>1000) return false;
  if(xSemaphoreTake(jpegMutex,pdMS_TO_TICKS(20))!=pdTRUE) return false;
  const bool ok=latestLength && latestLength<=capacity && uint32_t(millis()-latestFrameMs)<=1000;
  if(ok) { memcpy(target,latest,latestLength); length=latestLength; serial=latestSerial; }
  xSemaphoreGive(jpegMutex); return ok;
}
}
