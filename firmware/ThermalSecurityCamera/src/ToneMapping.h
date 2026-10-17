#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace thermal {
enum class ToneMapping : uint8_t { Linear, Adaptive };

inline const char* toneMappingName(ToneMapping mode) {
  return mode == ToneMapping::Adaptive ? "adaptive" : "linear";
}

inline bool parseToneMapping(const char* name, ToneMapping& result) {
  if (!name) return false;
  if (!std::strcmp(name, "linear")) { result = ToneMapping::Linear; return true; }
  if (!std::strcmp(name, "adaptive")) { result = ToneMapping::Adaptive; return true; }
  return false;
}

// A single monotonic temperature-to-colour transfer for the whole frame. The
// histogram redistributes display contrast, never the measured temperatures.
// Full extrema are retained: a small hot target must not vanish in a clipped
// percentile tail. Unlike tile-based equalization, equal temperatures always
// receive equal colours everywhere in the image.
class ToneMapper {
 public:
  void reset() { ready_ = false; }

  void update(const float* pixels, size_t count, float rawLow, float rawHigh,
              ToneMapping mode, uint32_t frameMs) {
    const bool changed = !ready_ || mode != mode_;
    mode_ = mode;
    if (mode == ToneMapping::Linear) {
      ready_ = std::isfinite(rawLow) && std::isfinite(rawHigh);
      low_ = rawLow;
      // Preserve the previous display's max(1 C, high-low) normalization.
      const double span = double(rawHigh) - double(rawLow);
      inverseSpan_ = float(1.0 / (span > 1.0 ? span : 1.0));
      linearSpan_ = rawHigh - rawLow;
      if (!(linearSpan_ > 1.f)) linearSpan_ = 1.f;
      high_ = rawHigh;
      lastMs_ = frameMs;
      return;
    }

    float minimum = std::numeric_limits<float>::infinity();
    float maximum = -std::numeric_limits<float>::infinity();
    size_t valid = 0;
    if (pixels) {
      for (size_t i = 0; i < count; ++i) {
        const float value = pixels[i];
        if (!std::isfinite(value)) continue;
        if (value < minimum) minimum = value;
        if (value > maximum) maximum = value;
        ++valid;
      }
    }
    if (!valid) { ready_ = false; return; }

    const uint32_t elapsed = frameMs - lastMs_; // Also handles millis() wrap.
    if (!changed && elapsed == 0) return;
    const bool fresh = changed || elapsed >= 1000 ||
                       maximum < low_ || minimum > high_;
    const float alpha = fresh ? 1.f : 1.f - std::exp(-float(elapsed) / 400.f);

    float nextLow = minimum, nextHigh = maximum;
    const double measuredSpan = double(maximum) - double(minimum);
    if (measuredSpan < kMinimumSpan) {
      const double middle = (double(minimum) + double(maximum)) * .5;
      nextLow = float(middle - kMinimumSpan * .5);
      nextHigh = float(middle + kMinimumSpan * .5);
      // At extreme finite float values, +/-1 C can round back to the same
      // value. Keep a finite, representable interval even for such inputs.
      if (!(nextHigh > nextLow)) {
        nextLow = std::nextafter(minimum, -std::numeric_limits<float>::infinity());
        nextHigh = std::nextafter(maximum, std::numeric_limits<float>::infinity());
        if (!std::isfinite(nextLow)) nextLow = minimum;
        if (!std::isfinite(nextHigh)) nextHigh = maximum;
      }
    }
    if (!fresh) {
      // Expand immediately so a newly arrived hot/cold object is visible;
      // contract gradually when an extreme leaves the scene.
      if (nextLow > low_) nextLow = interpolate(low_, nextLow, alpha);
      if (nextHigh < high_) nextHigh = interpolate(high_, nextHigh, alpha);
    }
    const float nextInverse = float(1.0 / (double(nextHigh) - double(nextLow)));

    for (unsigned i = 0; i <= kBins; ++i) scratch_[i] = 0;
    for (size_t i = 0; i < count; ++i) {
      const float value = pixels[i];
      if (!std::isfinite(value)) continue;
      const float position = normalized(value, nextLow, nextHigh, nextInverse);
      unsigned bin = unsigned(position * kBins);
      if (bin >= kBins) bin = kBins - 1;
      scratch_[bin + 1] += 1.f;
    }

    // Limit histogram mass to 4x the mean. Uniformly redistribute the clipped
    // mass among bins with capacity; re-clipping bounds local contrast gain.
    const float cap = float(valid) * (4.f / kBins);
    float excess = 0;
    for (unsigned i = 1; i <= kBins; ++i) {
      if (scratch_[i] > cap) { excess += scratch_[i] - cap; scratch_[i] = cap; }
    }
    for (unsigned pass = 0; pass < kBins && excess > float(valid) * 1e-6f; ++pass) {
      unsigned available = 0;
      for (unsigned i = 1; i <= kBins; ++i) if (scratch_[i] < cap) ++available;
      if (!available) break;
      const float share = excess / available;
      float assigned = 0;
      for (unsigned i = 1; i <= kBins; ++i) {
        const float room = cap - scratch_[i];
        const float addition = room < share ? room : share;
        scratch_[i] += addition;
        assigned += addition;
      }
      if (!(assigned > 0)) break;
      excess -= assigned;
    }

    // A minimum display span alone does not suppress histogram-driven noise
    // amplification. Fade equalization out quadratically in almost-flat scenes.
    const float sceneContrast = measuredSpan < kMinimumSpan
                                    ? float(measuredSpan / kMinimumSpan) : 1.f;
    const float strength = .65f * sceneContrast * sceneContrast;
    float total = 0;
    for (unsigned i = 1; i <= kBins; ++i) total += scratch_[i];
    float cumulative = 0;
    for (unsigned i = 1; i <= kBins; ++i) {
      cumulative += scratch_[i];
      const float position = float(i) / kBins;
      scratch_[i] = (1.f - strength) * position + strength * cumulative / total;
    }
    scratch_[kBins] = 1.f;

    if (!fresh) {
      // The range has moved: blend old and new LUT values at the SAME absolute
      // temperature, rather than blending unrelated histogram bin numbers.
      for (unsigned i = 0; i <= kBins; ++i) {
        const float temperature = interpolate(nextLow, nextHigh, float(i) / kBins);
        scratch_[i] = interpolate(sample(temperature), scratch_[i], alpha);
      }
    }
    for (unsigned i = 0; i <= kBins; ++i) curve_[i] = bounded(scratch_[i]);
    low_ = nextLow;
    high_ = nextHigh;
    inverseSpan_ = nextInverse;
    lastMs_ = frameMs;
    ready_ = true;
  }

  float map(float temperature) const {
    if (!ready_ || !std::isfinite(temperature)) return 0.f;
    if (mode_ == ToneMapping::Linear) {
      const float delta = temperature - low_;
      if (std::isfinite(delta) && std::isfinite(linearSpan_))
        return bounded(delta / linearSpan_);
      return bounded(float((double(temperature) - double(low_)) * inverseSpan_));
    }
    return sample(temperature);
  }

 private:
  static constexpr unsigned kBins = 128;
  static constexpr float kMinimumSpan = 2.f;
  static float bounded(float value) {
    return value > 0.f ? (value < 1.f ? value : 1.f) : 0.f;
  }
  static float interpolate(float from, float to, float fraction) {
    // Double intermediates avoid overflow on pathological finite float ranges.
    return float(double(from) + (double(to) - double(from)) * fraction);
  }
  static float normalized(float value, float low, float high, float inverse) {
    if (value <= low) return 0.f;
    if (value >= high) return 1.f;
    const float delta = value - low;
    if (std::isfinite(delta)) return bounded(delta * inverse);
    return bounded(float((double(value) - double(low)) * inverse));
  }
  float sample(float temperature) const {
    const float position = normalized(temperature, low_, high_, inverseSpan_) * kBins;
    const unsigned bin = unsigned(position);
    if (bin >= kBins) return curve_[kBins];
    const float fraction = position - bin;
    return curve_[bin] + (curve_[bin + 1] - curve_[bin]) * fraction;
  }

  float curve_[kBins + 1]{};
  float scratch_[kBins + 1]{};
  float low_ = 0.f, high_ = 2.f, inverseSpan_ = .5f;
  float linearSpan_ = 1.f;
  uint32_t lastMs_ = 0;
  ToneMapping mode_ = ToneMapping::Linear;
  bool ready_ = false;
};
}
