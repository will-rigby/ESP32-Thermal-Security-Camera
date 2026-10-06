#include "../firmware/ThermalSecurityCamera/src/Yuy2.h"
#include <cassert>
#include <fstream>
#include <limits>
#include <vector>
#include <cstdio>
using namespace thermal;
int main(int argc,char** argv) {
  std::vector<float> pixels(PixelCount,20.f);
  std::vector<uint8_t> guarded(VideoFrameBytes+2,0xa5);
  auto out=guarded.data()+1;
  pixels[0]=10.f;pixels[1]=30.f;pixels[2]=25.f;pixels[3]=40.f;
  pixels[4]=std::numeric_limits<float>::quiet_NaN();pixels.back()=30.f;
  renderYuy2(pixels.data(),20,30,false,false,out);
  assert(guarded.front()==0xa5 && guarded.back()==0xa5);
  assert(out[0]==16 && out[2]==235 && out[4]==126 && out[6]==235 && out[8]==16);
  for(int i=0;i<PixelCount;++i)assert(out[i*2+1]==128);
  assert(out[VideoFrameBytes-2]==235);
  renderYuy2(pixels.data(),20,30,true,false,out);
  assert(out[2*(VideoWidth-2)]==235 && out[2*(PixelCount-VideoWidth)]==235);
  renderYuy2(pixels.data(),20,30,false,true,out);
  assert(out[0]==235 && out[2]==16);
  std::fill(pixels.begin(),pixels.end(),20.f);
  renderYuy2(pixels.data(),20,20,false,false,out);
  for(int i=0;i<PixelCount;++i)assert(out[i*2]==16); // Flat scene: finite, no stripes or borders.

  Detection d;d.minC=20;d.maxC=30;d.frameMs=1234;d.state=Occupancy::Occupied;
  d.small.bounds={2,3,4,5};d.small.pixels=20;d.large.bounds={40,10,8,8};d.large.pixels=64;
  const Roi roi{0,0,80,62};
  std::vector<uint8_t> packet(VideoPacketBytes),other(VideoPacketBytes),bmp(BmpBytes);
  pixels[1]=30;
  renderVideoPacket(pixels.data(),d,roi,false,Palette::WhiteHot,42,packet.data());
  assert(VideoFrameBytes==9920 && VideoPacketBytes==9984 && VideoInterval100ns==500000);
  assert(memcmp(packet.data(),"YUY2",4)==0 && packet[4]==1 && packet[6]==64);
  assert(packet[8]==80 && packet[10]==62 && get32(packet.data()+12)==42 && get32(packet.data()+16)==1234);
  uint32_t temperature=get32(packet.data()+24);float decoded;memcpy(&decoded,&temperature,4);assert(decoded==20.f);
  assert(packet[40]==2 && packet[42]==3 && packet[56]==20 && packet[58]==64);
  auto noBoxes=d;noBoxes.small={};noBoxes.large={};noBoxes.state=Occupancy::Learning;
  renderVideoPacket(pixels.data(),noBoxes,{9,9,10,10},false,Palette::WhiteHot,43,other.data());
  assert(memcmp(packet.data()+VideoHeaderBytes,other.data()+VideoHeaderBytes,VideoFrameBytes)==0);
  renderBmp(packet.data(),bmp.data());
  assert(bmp[0]=='B' && bmp[1]=='M' && get32(bmp.data()+2)==BmpBytes);
  assert(get32(bmp.data()+18)==80 && int32_t(get32(bmp.data()+22))==-62 && bmp[28]==8);
  assert(bmp[BmpHeaderBytes]==0 && bmp[BmpHeaderBytes+1]==255);
  if(argc>1) { std::ofstream fixture(argv[1],std::ios::binary);fixture.write(reinterpret_cast<char*>(packet.data()),packet.size());assert(fixture.good()); }
  puts("PASS: YUY2 byte order, levels, neutral chroma, flip, black hot, flat scenes, bounds, metadata, BMP and overlay-free pixels");
}
