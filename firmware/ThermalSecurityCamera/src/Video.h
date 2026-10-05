#pragma once
#include "Application.h"
namespace thermal {
constexpr int VideoWidth = 320, VideoHeight = 240;
// 80:62 scaled to 310:240 with five-pixel side bars.
constexpr int ImageX = 5, ImageWidth = 310;
constexpr size_t JpegCapacity = 65536;
struct VideoDiagnostics {
  uint32_t frames=0, jpegBytes=0, renderMs=0;
  float fps=0;
};
VideoDiagnostics videoDiagnostics();
bool startVideo();
void setVideoEnabled(bool enabled);
bool videoEnabled();
bool copyJpeg(uint8_t* target, size_t capacity, size_t& length, uint32_t& serial);
void startUsb();
const char* usbStatus();
}
