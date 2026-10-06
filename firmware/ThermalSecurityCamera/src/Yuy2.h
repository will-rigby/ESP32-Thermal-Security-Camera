#pragma once
#include "Orientation.h"
#include "Palette.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace thermal {
constexpr int VideoWidth=SensorWidth, VideoHeight=SensorHeight;
constexpr uint32_t VideoFps=20, VideoPeriodMs=1000/VideoFps;
constexpr uint32_t VideoInterval100ns=10000000/VideoFps;
constexpr size_t VideoFrameBytes=PixelCount*2, VideoHeaderBytes=64;
constexpr size_t VideoPacketBytes=VideoHeaderBytes+VideoFrameBytes;
constexpr size_t BmpHeaderBytes=54+256*4, BmpBytes=BmpHeaderBytes+PixelCount;
static_assert(VideoWidth%4==0,"YUY2 pairs and BMP rows must be aligned");
inline void put16(uint8_t* p,uint16_t v) { p[0]=uint8_t(v);p[1]=uint8_t(v>>8); }
inline void put32(uint8_t* p,uint32_t v) { for(int i=0;i<4;++i)p[i]=uint8_t(v>>(8*i)); }
inline uint32_t get32(const uint8_t* p) { return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24; }
inline void putFloat(uint8_t* p,float v) { uint32_t bits;static_assert(sizeof(v)==sizeof(bits),"32-bit float required");memcpy(&bits,&v,4);put32(p,bits); }
inline void putRoi(uint8_t* p,const Roi& r) { put16(p,r.x);put16(p+2,r.y);put16(p+4,r.width);put16(p+6,r.height); }
// Packed Y0 U Y1 V, limited-range luma (16..235), neutral chroma (128).
// USB receives only these pixels. All geometry/overlays remain metadata.
inline void renderYuy2(const float* pixels,float low,float high,bool flip,bool blackHot,uint8_t* out) {
  const float range=std::max(1.f,high-low);
  for(int y=0;y<VideoHeight;++y) for(int x=0;x<VideoWidth;++x) {
    const float c=pixels[y*VideoWidth+orientedColumn(x,flip)];
    float v=std::isfinite(c)?std::max(0.f,std::min(1.f,(c-low)/range)):0.f;
    if(blackHot)v=1.f-v;
    const int i=(y*VideoWidth+x)*2;
    out[i]=uint8_t(16.f+v*219.f+0.5f);out[i+1]=128;
  }
}
inline void renderVideoPacket(const float* pixels,const Detection& d,const Roi& roi,bool flip,Palette palette,uint32_t serial,uint8_t* packet) {
  memset(packet,0,VideoHeaderBytes);memcpy(packet,"YUY2",4);
  put16(packet+4,1);put16(packet+6,VideoHeaderBytes);
  put16(packet+8,VideoWidth);put16(packet+10,VideoHeight);
  put32(packet+12,serial);put32(packet+16,d.frameMs);
  packet[20]=flip?1:0;packet[21]=uint8_t(palette);packet[22]=uint8_t(d.state);
  putFloat(packet+24,d.minC);putFloat(packet+28,d.maxC);putRoi(packet+32,roi);
  putRoi(packet+40,d.small.bounds);putRoi(packet+48,d.large.bounds);
  put16(packet+56,d.small.pixels);put16(packet+58,d.large.pixels);
  renderYuy2(pixels,d.minC,d.maxC,flip,palette==Palette::BlackHot,packet+VideoHeaderBytes);
}
// Uncompressed, top-down 8-bit grayscale BMP; no image encoder is involved.
inline void renderBmp(const uint8_t* packet,uint8_t* out) {
  memset(out,0,BmpHeaderBytes);out[0]='B';out[1]='M';put32(out+2,BmpBytes);
  put32(out+10,BmpHeaderBytes);put32(out+14,40);put32(out+18,VideoWidth);
  put32(out+22,uint32_t(-VideoHeight));put16(out+26,1);put16(out+28,8);
  put32(out+34,PixelCount);put32(out+46,256);
  for(int i=0;i<256;++i)out[54+i*4]=out[55+i*4]=out[56+i*4]=uint8_t(i);
  for(int i=0;i<PixelCount;++i)out[BmpHeaderBytes+i]=uint8_t((int(packet[VideoHeaderBytes+i*2])-16)*255/219);
}
}
