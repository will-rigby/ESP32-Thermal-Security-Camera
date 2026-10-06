#pragma once
#include "Application.h"
#include "Yuy2.h"
namespace thermal {
struct VideoDiagnostics {
  uint32_t frames=0, renderUs=0;
  float fps=0;
};
VideoDiagnostics videoDiagnostics();
bool startVideo();
void setVideoEnabled(bool enabled);
bool videoEnabled();
bool copyVideo(uint8_t* target,size_t capacity,uint32_t afterSerial=0);
void startUsb();
const char* usbStatus();
}
