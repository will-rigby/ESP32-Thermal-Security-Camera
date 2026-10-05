#pragma once
#include "Detector.h"
namespace thermal {
// Detection and MQTT retain native sensor coordinates. Only viewing and
// pointer input are transformed; applying the transform twice restores them.
inline int orientedColumn(int x, bool flipHorizontal) {
  return flipHorizontal ? SensorWidth-1-x : x;
}
inline Roi orientedRoi(Roi roi, bool flipHorizontal) {
  if(flipHorizontal) roi.x=SensorWidth-roi.x-roi.width;
  return roi;
}
}
