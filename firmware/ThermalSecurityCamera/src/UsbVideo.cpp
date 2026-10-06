#include "Video.h"
#include "BuildOptions.h"
#include "UsbDiagnostics.h"
#if THERMAL_ENABLE_USB
#include <USB.h>
#include <esp32-hal-tinyusb.h>
#include <class/video/video_device.h>
#include <device/usbd_pvt.h>
#include <esp_heap_caps.h>
#include <freertos/idf_additions.h>
#if ARDUINO_USB_MODE != 0 || ARDUINO_USB_CDC_ON_BOOT
#error Select USB Mode: USB-OTG (TinyUSB), and USB CDC On Boot: Disabled.
#endif
#if !CFG_TUD_VIDEO || !CFG_TUD_VIDEO_STREAMING
#error This Arduino core must be built with TinyUSB video enabled. Use the pinned 3.3.12 package.
#endif
namespace {
std::atomic<bool> usbStarted{false};
std::atomic<bool> submissionPending{false};
std::atomic<uint32_t> lastSubmitted{0};
std::atomic<uint32_t> lastAcceptedMs{0};
uint8_t *usbBuffer, *usbPrevious;
uint8_t usbEndpoint=0;
uint32_t pendingSerial=0;
uint32_t pendingFrameMs=0;
constexpr uint16_t DescriptorLength = 150;
// Descriptor composition follows TinyUSB's MIT-licensed video_capture example.
// Copyright (c) 2020 Jerzy Kasenbreg; (c) 2021 Koji KITAYAMA. See THIRD_PARTY.md.
uint16_t descriptors(uint8_t* dest,uint8_t* interfaceNumber) {
  const uint8_t control=*interfaceNumber, stream=control+1;
  const uint8_t ep=tinyusb_get_free_in_endpoint();
  if(!ep) return 0;
  const uint8_t endpoint=0x80|ep;
  usbEndpoint=endpoint;
  const uint8_t name=tinyusb_add_string_descriptor("Thermal camera");
  const uint8_t desc[]={
    TUD_VIDEO_DESC_IAD(control,2,name),
    TUD_VIDEO_DESC_STD_VC(control,0,name),
    TUD_VIDEO_DESC_CS_VC(0x0150,TUD_VIDEO_DESC_CAMERA_TERM_LEN+TUD_VIDEO_DESC_OUTPUT_TERM_LEN,27000000,stream),
    TUD_VIDEO_DESC_CAMERA_TERM(1,0,0,0,0,0,0),
    TUD_VIDEO_DESC_OUTPUT_TERM(2,VIDEO_TT_STREAMING,0,1,0),
    TUD_VIDEO_DESC_STD_VS(stream,0,1,name),
    TUD_VIDEO_DESC_CS_VS_INPUT(1,TUD_VIDEO_DESC_CS_VS_FMT_UNCOMPR_LEN+TUD_VIDEO_DESC_CS_VS_FRM_UNCOMPR_DISC_LEN+4+TUD_VIDEO_DESC_CS_VS_COLOR_MATCHING_LEN,
      endpoint,0,2,0,0,0,0),
    TUD_VIDEO_DESC_CS_VS_FMT_UNCOMPR(1,1,TUD_VIDEO_GUID_YUY2,16,1,0,0,0,0),
    // The bundled discrete-frame macro counts expanded bytes as intervals.
    // Spell out this 30-byte descriptor: one discrete interval of 50 ms.
    30,TUSB_DESC_CS_INTERFACE,VIDEO_CS_ITF_VS_FRAME_UNCOMPRESSED,1,0,
    U16_TO_U8S_LE(thermal::VideoWidth),U16_TO_U8S_LE(thermal::VideoHeight),
    U32_TO_U8S_LE(8*thermal::VideoFrameBytes*thermal::VideoFps),
    U32_TO_U8S_LE(8*thermal::VideoFrameBytes*thermal::VideoFps),U32_TO_U8S_LE(thermal::VideoFrameBytes),
    U32_TO_U8S_LE(thermal::VideoInterval100ns),1,U32_TO_U8S_LE(thermal::VideoInterval100ns),
    TUD_VIDEO_DESC_CS_VS_COLOR_MATCHING(VIDEO_COLOR_PRIMARIES_BT709,VIDEO_COLOR_XFER_CH_BT709,VIDEO_COLOR_COEF_SMPTE170M),
    TUD_VIDEO_DESC_EP_BULK(endpoint,64,0)
  };
  static_assert(sizeof(desc)==DescriptorLength,"Incorrect UVC descriptor length");
  memcpy(dest,desc,sizeof(desc)); *interfaceNumber+=2; return sizeof(desc);
}
// Run frame handoff in TinyUSB's task, alongside probe/commit callbacks. This
// prevents a recommit from clearing a just-submitted frame on the other core.
void submitFrame(void*) {
  if(tud_mounted() && tud_video_n_streaming(0,0) && thermal::videoEnabled() &&
     thermal::getStatus().sensorReady && uint32_t(millis()-pendingFrameMs)<=1000 &&
     tud_video_n_frame_xfer(0,0,usbBuffer+thermal::VideoHeaderBytes,thermal::VideoFrameBytes)) {
    uint8_t* submitted=usbBuffer; usbBuffer=usbPrevious; usbPrevious=submitted;
    lastSubmitted=pendingSerial;
    lastAcceptedMs=millis();
  }
  submissionPending=false;
}
void usbTask(void*) {
  for(;;) {
    const bool mounted=tud_mounted();
    const bool streaming=mounted && tud_video_n_streaming(0,0);
    thermal::usbStreaming=streaming;
    if(streaming && !submissionPending.load() && uint32_t(millis()-lastAcceptedMs.load())>=thermal::VideoPeriodMs) {
      if(thermal::copyVideo(usbBuffer,thermal::VideoPacketBytes,lastSubmitted.load())) {
        // TinyUSB refuses a new frame while it owns the previous buffer. Only
        // swap after acceptance, so the active frame is never overwritten,
        // including host stop/recommit paths that omit the completion callback.
        pendingSerial=thermal::get32(usbBuffer+12);pendingFrameMs=thermal::get32(usbBuffer+16);submissionPending=true;
        usbd_defer_func(submitFrame,nullptr,false);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
}
extern "C" void tud_video_frame_xfer_complete_cb(uint_fast8_t,uint_fast8_t) {}
extern "C" int tud_video_commit_cb(uint_fast8_t,uint_fast8_t,const video_probe_and_commit_control_t* parameters) {
  if(parameters->bFormatIndex!=1 || parameters->bFrameIndex!=1 || parameters->dwFrameInterval!=thermal::VideoInterval100ns)
    return VIDEO_ERROR_INVALID_VALUE_WITHIN_RANGE;
  // The bundled video driver discards stm->buffer on recommit but leaves an
  // outstanding bulk IN transaction claimed. On S3, usbd_edpt_close is a no-op
  // (TUP_DCD_EDPT_ISO_ALLOC). Stall/clear aborts and flushes the old transaction,
  // resets its data toggle and releases the endpoint's BUSY/CLAIMED state.
  // This runs before the control request is acknowledged, on the USB task.
  if(usbEndpoint) {
    usbd_edpt_stall(0,usbEndpoint);
    usbd_edpt_clear_stall(0,usbEndpoint);
  }
  return VIDEO_ERROR_NONE;
}
#endif
namespace thermal {
void startUsb() {
#if THERMAL_ENABLE_USB
  startUsbDiagnostics();
  usbBuffer=static_cast<uint8_t*>(ps_malloc(VideoPacketBytes));
  usbPrevious=static_cast<uint8_t*>(ps_malloc(VideoPacketBytes));
  if(!usbBuffer || !usbPrevious || tinyusb_enable_interface(USB_INTERFACE_CUSTOM,DescriptorLength,descriptors)!=ESP_OK) return;
  USB.productName(THERMAL_TEST_PATTERN?"Thermal Camera TEST":"Thermal Security Camera");
  USB.manufacturerName("Thermal camera project"); USB.serialNumber(deviceId);
  USB.usbClass(TUSB_CLASS_MISC); USB.usbSubClass(MISC_SUBCLASS_COMMON); USB.usbProtocol(MISC_PROTOCOL_IAD);
  USB.usbAttributes(0x80); USB.usbPower(250);
  if(!USB.begin()) return;
  usbStarted=xTaskCreatePinnedToCoreWithCaps(usbTask,"thermal-usb",4096,nullptr,2,nullptr,0,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)==pdPASS;
#endif
}
const char* usbStatus() {
#if THERMAL_ENABLE_USB
  return !usbStarted?"initialization failed":usbStreaming.load()?"streaming":"ready";
#else
  return "disabled";
#endif
}
}
