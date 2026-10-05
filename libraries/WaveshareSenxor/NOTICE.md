# Sensor adapter provenance

Sensor archive and original headers are preserved from
https://github.com/0015/ESP32_Thermal_Camera_Viewer
commit `4fe4e6ad8b9c04d448908ac34115d7c595fd31d0`, directory `senxorESP32S3`.

Archive: `components/SenXorLib/libsenxorLib4M.a`

SHA-256: `E8ECAACACD9C02EAF24D5FD4BC6BE68CA1C46CE4CFF451DF98A1A0F7239108D8`

The vendor component states **2023 Meridian Innovation. All rights reserved.**
Capture source also states **(C) 2018 Meridian Innovation**. These components
retain their original ownership; the opaque sensor archive is not relicensed
as project-authored code. `WaveshareSenxor.cpp` adapts the vendor acquisition,
SPI, timer and initialization flow to Arduino-ESP32 3.3.12.

The containing reference repository includes this notice:

MIT License

Copyright (c) 2025 Eric Nam

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
