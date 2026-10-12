#pragma once
#include "Application.h"
#include "Yuy2.h"
#include "VideoTiming.h"
namespace thermal {
struct VideoDiagnostics {
  uint32_t frames=0, renderUs=0;
  float fps=0;
  float temperatureC=NAN;
};
VideoDiagnostics videoDiagnostics();
bool startVideo();
void setVideoEnabled(bool enabled);
bool videoEnabled();
bool videoFrameUsable(uint32_t frameMs);
bool copyVideo(uint8_t* target,size_t capacity,uint32_t afterSerial=0);
void startUsb();
const char* usbStatus();
}
