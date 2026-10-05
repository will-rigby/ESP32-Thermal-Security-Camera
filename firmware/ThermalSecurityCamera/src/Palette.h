#pragma once
#include <stdint.h>
#include <cstring>
namespace thermal {
enum class Palette : uint8_t { Fire, Ironbow, Rainbow, WhiteHot, BlackHot };
struct Colour { uint8_t r,g,b; };
inline const char* paletteName(Palette p) {
  switch(p) {
    case Palette::Ironbow:return "ironbow";
    case Palette::Rainbow:return "rainbow";
    case Palette::WhiteHot:return "white_hot";
    case Palette::BlackHot:return "black_hot";
    default:return "fire";
  }
}
inline bool parsePalette(const char* name,Palette& result) {
  for(unsigned i=0;i<5;++i) if(!std::strcmp(name,paletteName(Palette(i)))) { result=Palette(i);return true; }
  return false;
}
inline Colour paletteColour(Palette p,uint8_t level) {
  if(p==Palette::WhiteHot) return {level,level,level};
  if(p==Palette::BlackHot) { const uint8_t v=255-level;return {v,v,v}; }
  static constexpr Colour fire[]={{0,0,0},{255,0,0},{255,255,0},{255,255,255}};
  static constexpr Colour iron[]={{0,0,0},{35,0,85},{120,15,110},{220,55,45},{255,165,30},{255,255,225}};
  static constexpr Colour rainbow[]={{0,0,100},{0,80,255},{0,240,255},{0,220,40},{255,240,0},{255,0,0}};
  const Colour* stops=p==Palette::Ironbow?iron:p==Palette::Rainbow?rainbow:fire;
  const unsigned count=p==Palette::Fire?4:6;
  const unsigned scaled=unsigned(level)*(count-1),segment=scaled/255,fraction=scaled%255;
  if(segment==count-1) return stops[segment];
  const auto a=stops[segment],b=stops[segment+1];
  return {uint8_t((a.r*(255-fraction)+b.r*fraction)/255),
          uint8_t((a.g*(255-fraction)+b.g*fraction)/255),
          uint8_t((a.b*(255-fraction)+b.b*fraction)/255)};
}
}
