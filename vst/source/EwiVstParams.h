// EwiVstParams.h — parameter IDs + normalized/plain conversions shared
// by processor and controller. Plain ranges mirror EwiPreset units.
#pragma once
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include <cmath>

namespace EwiVst {
using ParamID = Steinberg::Vst::ParamID;
using ParamValue = Steinberg::Vst::ParamValue;

enum : ParamID {
  kBypass = 100,
  kPreset,      // 0..3 discrete
  kBreath,      // 0..1 (CC2)
  kVibrato,     // 0..1 (CC1)
  kVolume,      // 0..1 (CC7)
  kExpression,  // 0..1 (CC11)
  kCutoff,      // 80..8000 Hz (exp)
  kBreathDepth, // 0..12000 Hz
  kResonance,   // 0..0.95
  kGlide,       // 0..0.5 s
  kFormant,     // 0..1
  kDlyMix,      // 0..0.6
  kDlyTime,     // 0.05..0.5 s
  kDlyFb,       // 0..0.7
  kRevMix,      // 0..0.6
  kRevSize,     // 0..1
  kBend,        // 0..1 (PitchBend, center 0.5)
  kFilterGamma, // 0.3..3.0 (breath->cutoff power)
  kNumParamsSentinel
};
constexpr int kNumParams = 18;

// State stream order (processor <-> controller must match)
// NOTE: appended last so v1 (16) / v2 (17) states stay prefix-compatible.
static const ParamID kStateOrder[] = {
  kBypass, kPreset, kBreath, kVibrato, kVolume, kExpression, kCutoff,
  kBreathDepth, kResonance, kGlide, kFormant, kDlyMix, kDlyTime, kDlyFb,
  kRevMix, kRevSize, kBend, kFilterGamma,
};
constexpr int kStateCount = sizeof (kStateOrder) / sizeof (kStateOrder[0]);

inline double normToCutoff (double n) { return 80.0 * std::pow (100.0, n); }
inline double cutoffToNorm (double hz)
{
  if (hz < 80.0)
    hz = 80.0;
  if (hz > 8000.0)
    hz = 8000.0;
  return std::log (hz / 80.0) / std::log (100.0);
}
inline double normToDlyTime (double n) { return 0.05 + n * 0.45; }
inline double dlyTimeToNorm (double s) { return (s - 0.05) / 0.45; }
inline int normToPreset (double n)
{
  int p = (int)(n * 3.0 + 0.5);
  return p < 0 ? 0 : (p > 3 ? 3 : p);
}
inline double normToFilterGamma (double n) { return 0.3 + n * 2.7; }
inline double filterGammaToNorm (double g)
{
  if (g < 0.3)
    g = 0.3;
  if (g > 3.0)
    g = 3.0;
  return (g - 0.3) / 2.7;
}

inline double defaultNorm (ParamID id)
{
  switch (id)
  {
    case kBypass: return 0.0;
    case kPreset: return 0.0;
    case kBreath: return 0.0;
    case kVibrato: return 0.0;
    case kVolume: return 1.0;
    case kExpression: return 1.0;
    case kCutoff: return (750.0 - 80.0) / 7920.0;
    case kBreathDepth: return 6200.0 / 12000.0;
    case kResonance: return 0.38 / 0.95;
    case kGlide: return 0.012 / 0.5;
    case kFormant: return 0.35;
    case kDlyMix: return 0.12 / 0.6;
    case kDlyTime: return dlyTimeToNorm (0.27);
    case kDlyFb: return 0.32 / 0.7;
    case kRevMix: return 0.16 / 0.6;
    case kRevSize: return 0.55;
    case kBend: return 0.5;
    case kFilterGamma: return filterGammaToNorm (1.5);
    default: return 0.0;
  }
}
} // namespace EwiVst
