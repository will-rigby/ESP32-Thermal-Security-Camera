# Third-party components

The repository's top-level MIT license applies to project-authored code and documentation only. Third-party files retain their own license terms and notices; in particular, it does not grant rights to the vendor sensor archive described below.

## Waveshare / Meridian sensor implementation

The Arduino adapter follows the hardware protocol, initialization and calibration flow from the [Waveshare thermal camera reference](https://www.waveshare.com/wiki/Thermal-Camera-ESP32-Module), as distributed in [0015/ESP32_Thermal_Camera_Viewer](https://github.com/0015/ESP32_Thermal_Camera_Viewer/tree/4fe4e6ad8b9c04d448908ac34115d7c595fd31d0/senxorESP32S3), commit `4fe4e6ad8b9c04d448908ac34115d7c595fd31d0`.

See the [library's included notice](libraries/WaveshareSenxor/NOTICE.md) for the upstream MIT notice and vendor ownership. The unchanged precompiled archive is **not relicensed as project-authored code**. Upstream CMake identifies `2023 Meridian Innovation. All rights reserved.`; portions of the acquisition source identify `(C) 2018 Meridian Innovation`. The archive contains the sensor processing implementation; its source/build recipe is not supplied upstream here. Firmware builds are reproducible from this pinned binary, not from sensor-library source.

| Included file | Upstream path under `senxorESP32S3` |
| --- | --- |
| `src/esp32s3/libsenxorLib4M.a` | `components/SenXorLib/libsenxorLib4M.a` |
| `src/vendor/SenXorLib.h` | `components/SenXorLib/include/SenXorLib.h` |
| `src/vendor/defines.h` | `components/SenXorLib/include/defines.h` |
| `src/vendor/Customer_Interface.h` | `components/Applications/include/Customer_Interface.h` |

Archive SHA-256: `E8ECAACACD9C02EAF24D5FD4BC6BE68CA1C46CE4CFF451DF98A1A0F7239108D8`.

`WaveshareSenxor.cpp` adapts `main/senxorTask.c`, `components/drivers/src/DrvSPIHost.c`, GPIO/PWM/timer hooks and `components/Applications/src/SenXor_Capturedata.c`. It replaces SDK-private DMA use with a bounded polling FIFO ISR, supplies the original C ABI hooks, reads factory calibration without writing it, and copies radiometric pixels before optional display filters. Calibration algorithms remain in the unchanged vendor archive.

## TinyUSB descriptors

`UsbVideo.cpp` adapts descriptor composition from TinyUSB's [video_capture example](https://github.com/hathach/tinyusb/tree/0.18.0/examples/device/video_capture). USB implementation and headers come from the pinned Arduino package; no second stack is bundled.

The MIT License (MIT)

Copyright (c) 2019 Ha Thach (tinyusb.org)

Copyright (c) 2020 Jerzy Kasenbreg

Copyright (c) 2021 Koji KITAYAMA

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

## Bundled Arduino dependencies

Arduino-ESP32 **3.3.12** and its S3 SDK package **3.3.12** are resolved through Espressif's Boards Manager index. The shipped SDK configuration identifies ESP-IDF **5.5.5**. WiFi, Preferences, ESPmDNS, esp-mqtt, cJSON, esp_http_server and Arduino TinyUSB use their respective upstream licenses distributed with that package. Nothing in this repository changes those licenses. No vendored PubSubClient or alternate USB stack is used.

Firmware 0.1.2 through 0.1.4 used the SDK's bundled **esp_new_jpeg 1.0.2** encoder. Hardware testing found its internal allocations exhausted the remaining RAM needed by Wi-Fi. From 0.1.5 through 0.2.2, rendering used esp32-camera's bundled Apache-2.0 `fmt2jpg_cb` converter, writing through a bounded callback into a PSRAM buffer. No encoder source or archive is copied or relicensed here. Firmware 0.3.0 removes the JPEG converter calls and buffers entirely. The SDK still distributes those libraries, but this firmware does not use their encoders. YUY2 packing, browser rendering and uncompressed BMP serialization are project-authored.
