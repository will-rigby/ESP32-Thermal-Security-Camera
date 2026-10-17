#include "../firmware/ThermalSecurityCamera/src/ToneMapping.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

using namespace thermal;
constexpr size_t kPixels = 80 * 62;
using Frame = std::array<float, kPixels>;

static void update(ToneMapper& mapper, const Frame& frame, uint32_t ms,
                   ToneMapping mode = ToneMapping::Adaptive) {
  const auto limits = std::minmax_element(frame.begin(), frame.end());
  mapper.update(frame.data(), frame.size(), *limits.first, *limits.second, mode, ms);
}

static void monotonic(const ToneMapper& mapper, float low, float high) {
  float previous = 0;
  for (unsigned i = 0; i <= 4000; ++i) {
    const float value = float(double(low) + (double(high) - low) * i / 4000);
    const float mapped = mapper.map(value);
    assert(std::isfinite(mapped) && mapped >= 0 && mapped <= 1);
    assert(mapped + 1e-6f >= previous);
    previous = mapped;
  }
}

int main() {
  ToneMapping parsed = ToneMapping::Adaptive;
  assert(parseToneMapping("linear", parsed) && parsed == ToneMapping::Linear);
  assert(parseToneMapping("adaptive", parsed) && parsed == ToneMapping::Adaptive);
  assert(!parseToneMapping("log", parsed) && parsed == ToneMapping::Adaptive);
  assert(!parseToneMapping(nullptr, parsed));
  assert(!std::strcmp(toneMappingName(ToneMapping::Adaptive), "adaptive"));
  ToneMapper mapper;
  assert(mapper.map(20) == 0);

  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();
  mapper.update(nullptr, 0, -10, 30, ToneMapping::Linear, 10);
  for (float value : {-50.f, -10.f, -9.f, 0.f, 10.f, 29.f, 30.f, 50.f}) {
    const float expected = std::max(0.f, std::min(1.f, (value + 10.f) / 40.f));
    assert(std::abs(mapper.map(value) - expected) < 1e-7f);
  }
  mapper.update(nullptr, 0, 20, 20.2f, ToneMapping::Linear, 11);
  assert(std::abs(mapper.map(20.2f) - .2f) < 1e-6f);
  assert(mapper.map(nan) == 0 && mapper.map(inf) == 0 && mapper.map(-inf) == 0);

  Frame scene;
  // Room surfaces / person-like temperature cluster, plus four pixels of a
  // hotter object. Full-range linear mapping wastes most display contrast.
  for (size_t i = 0; i < scene.size(); ++i) scene[i] = 20.f + float(i % 32) * .125f;
  std::fill(scene.end() - 4, scene.end(), 100.f);
  ToneMapper linear;
  update(linear, scene, 100, ToneMapping::Linear);
  update(mapper, scene, 100);
  const float linearSeparation = linear.map(23) - linear.map(20.5f);
  const float adaptiveSeparation = mapper.map(23) - mapper.map(20.5f);
  assert(adaptiveSeparation > linearSeparation * 2.5f);
  assert(mapper.map(100) == 1.f);
  assert(mapper.map(95) < mapper.map(100));
  assert(mapper.map(80) < mapper.map(95));
  monotonic(mapper, -30, 120);

  // A single cold outlier is also retained, with negative temperatures valid.
  scene[0] = -80;
  update(mapper, scene, 2000);
  assert(mapper.map(-80) == 0);
  assert(mapper.map(-70) > mapper.map(-80));
  monotonic(mapper, -100, 120);

  // Uniform and very low-noise scenes must not turn noise into false objects.
  scene.fill(23);
  mapper.reset();
  update(mapper, scene, 3000);
  assert(std::abs(mapper.map(23) - .5f) < 1e-6f);
  assert(std::abs(mapper.map(23.1f) - mapper.map(22.9f) - .1f) < 2e-6f);
  for (size_t i = 0; i < scene.size(); ++i) scene[i] = 23.f + (i % 2 ? .05f : -.05f);
  update(mapper, scene, 5000);
  assert(mapper.map(23.05f) - mapper.map(22.95f) < .055f);
  monotonic(mapper, 21, 25);

  // Repeated renderer calls for one frame cannot advance temporal adaptation.
  Frame changed = scene;
  for (size_t i = 0; i < changed.size(); ++i) changed[i] = 23.f + float(i % 40) * .1f;
  const float before = mapper.map(23.05f);
  update(mapper, changed, 5000);
  assert(mapper.map(23.05f) == before);

  // Compare a real range shift to the expected blend in temperature space.
  // Expansion is immediate, but the old transfer contributes at the same C.
  ToneMapper old, smoothed, target;
  Frame expanded = changed;
  expanded[0] = 20.f; // Both range ends expand: no contraction changes the target.
  update(old, scene, 6000);
  update(smoothed, scene, 6000);
  update(target, expanded, 6050);
  update(smoothed, expanded, 6050);
  const float alpha = 1.f - std::exp(-50.f / 400.f);
  const float expandedSpan = *std::max_element(expanded.begin(), expanded.end()) - 20.f;
  for (unsigned bin = 0; bin <= 128; ++bin) {
    const float value = float(20.0 + double(expandedSpan) * bin / 128);
    const float expected = old.map(value) + (target.map(value) - old.map(value)) * alpha;
    assert(std::abs(smoothed.map(value) - expected) < 2e-6f);
  }
  monotonic(smoothed, 20, 30);

  // Time wrap must yield the same elapsed-time smoothing as ordinary times.
  ToneMapper wrapped, normal;
  update(wrapped, scene, UINT32_MAX - 24);
  update(normal, scene, 100);
  update(wrapped, changed, 25);
  update(normal, changed, 150);
  for (float value : {22.f, 23.f, 24.f, 25.f, 26.f, 27.f})
    assert(std::abs(wrapped.map(value) - normal.map(value)) < 1e-6f);

  // Slow input rates and disjoint scene changes must not retain stale imagery.
  ToneMapper fresh;
  update(smoothed, changed, 8000);
  update(fresh, changed, 8000);
  for (float value : {20.f, 23.f, 24.f, 25.f, 27.f})
    assert(std::abs(smoothed.map(value) - fresh.map(value)) < 1e-6f);
  scene.fill(-30);
  update(smoothed, scene, 8050);
  assert(std::abs(smoothed.map(-30) - .5f) < 1e-6f);

  // A mode switch, even at the same timestamp, discards prior transfer state.
  update(smoothed, changed, 8050, ToneMapping::Linear);
  update(smoothed, changed, 8050);
  for (float value : {20.f, 23.f, 24.f, 25.f, 27.f})
    assert(std::abs(smoothed.map(value) - fresh.map(value)) < 1e-6f);

  // Invalid samples are excluded; invalid-only or absent frames map to zero.
  const float mixed[] = {nan, -inf, 19, 20, 21, inf};
  mapper.update(mixed, 6, nan, inf, ToneMapping::Adaptive, 9000);
  monotonic(mapper, 17, 23);
  const float invalid[] = {nan, inf, -inf};
  mapper.update(invalid, 3, 0, 1, ToneMapping::Adaptive, 9050);
  assert(mapper.map(20) == 0);
  mapper.update(nullptr, kPixels, 0, 1, ToneMapping::Adaptive, 9100);
  assert(mapper.map(20) == 0);
  mapper.update(mixed, 0, 0, 1, ToneMapping::Adaptive, 9150);
  assert(mapper.map(20) == 0);
  mapper.update(nullptr, 0, nan, inf, ToneMapping::Linear, 9200);
  assert(mapper.map(20) == 0);

  // No NaN, overflow, or out-of-bounds histogram lookup on finite extremes.
  const float largest = std::numeric_limits<float>::max();
  const float extremes[] = {-largest, -1, 0, 1, largest};
  mapper.update(extremes, 5, -largest, largest, ToneMapping::Adaptive, 9300);
  monotonic(mapper, -largest, largest);
  for (float value : {-largest, largest}) {
    mapper.reset();
    mapper.update(&value, 1, value, value, ToneMapping::Adaptive, 9400);
    assert(std::isfinite(mapper.map(value)));
    assert(mapper.map(value) >= 0 && mapper.map(value) <= 1);
  }
  std::printf("PASS: thermal mapping; clustered-object contrast %.2fx linear, "
              "tiny targets, flat-scene noise, monotonicity, temporal stability, "
              "mode changes, timestamp wrap and invalid input\n",
              adaptiveSeparation / linearSeparation);
}
