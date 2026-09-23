// EwiVstController.cpp
#include "EwiVstController.h"
#include "EwiVstParams.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "pluginterfaces/base/ibstream.h"
#include "base/source/fstreamer.h"

namespace EwiVst {
using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {
RangeParameter* mk (const TChar* title, ParamID id, const TChar* units, ParamValue lo,
                    ParamValue hi, ParamValue defPlain, int32 steps = 0, int32 flags = ParameterInfo::kCanAutomate)
{
  return new RangeParameter (title, id, units, lo, hi, defPlain, steps, flags);
}
} // namespace

//------------------------------------------------------------------------
tresult PLUGIN_API EwiVst::EwiVstController::initialize (FUnknown* context)
{
  tresult result = EditController::initialize (context);
  if (result != kResultOk)
    return result;

  parameters.addParameter (mk (STR16 ("Bypass"), kBypass, nullptr, 0., 1., 0., 1,
                               ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass));
  auto* preset = new StringListParameter (STR16 ("Preset"), kPreset);
  preset->appendString (STR16 ("Axis"));
  preset->appendString (STR16 ("Flute"));
  preset->appendString (STR16 ("Brass"));
  preset->appendString (STR16 ("SyncLd"));
  parameters.addParameter (preset);

  parameters.addParameter (mk (STR16 ("Breath"), kBreath, STR16 ("CC2"), 0., 1., 0.));
  parameters.addParameter (mk (STR16 ("Vibrato"), kVibrato, STR16 ("CC1"), 0., 1., 0.));
  parameters.addParameter (mk (STR16 ("Volume"), kVolume, nullptr, 0., 1., 1.));
  parameters.addParameter (mk (STR16 ("Expression"), kExpression, nullptr, 0., 1., 1.));
  parameters.addParameter (mk (STR16 ("Cutoff"), kCutoff, STR16 ("Hz"), 80., 8000., 750.));
  parameters.addParameter (mk (STR16 ("BreathDepth"), kBreathDepth, STR16 ("Hz"), 0., 12000., 6200.));
  parameters.addParameter (mk (STR16 ("Resonance"), kResonance, nullptr, 0., 0.95, 0.38));
  parameters.addParameter (mk (STR16 ("Glide"), kGlide, STR16 ("s"), 0., 0.5, 0.012));
  parameters.addParameter (mk (STR16 ("Formant"), kFormant, nullptr, 0., 1., 0.35));
  parameters.addParameter (mk (STR16 ("DelayMix"), kDlyMix, nullptr, 0., 0.6, 0.12));
  parameters.addParameter (mk (STR16 ("DelayTime"), kDlyTime, STR16 ("s"), 0.05, 0.5, 0.27));
  parameters.addParameter (mk (STR16 ("DelayFB"), kDlyFb, nullptr, 0., 0.7, 0.32));
  parameters.addParameter (mk (STR16 ("ReverbMix"), kRevMix, nullptr, 0., 0.6, 0.16));
  parameters.addParameter (mk (STR16 ("ReverbSize"), kRevSize, nullptr, 0., 1., 0.55));
  parameters.addParameter (mk (STR16 ("Bend"), kBend, STR16 ("PB"), 0., 1., 0.5));
  parameters.addParameter (mk (STR16 ("FilterGamma"), kFilterGamma, nullptr, 0.3, 3.0, 1.5));
  return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API EwiVst::EwiVstController::setComponentState (IBStream* state)
{
  if (!state)
    return kResultFalse;
  // processor state: version + preset + N doubles (v1=16, v2=17, v3=18)
  IBStreamer s (state, kLittleEndian);
  int32 version = 0;
  if (!s.readInt32 (version) || (version < 1 || version > 3))
    return kResultFalse;
  int32 preset = 0;
  if (!s.readInt32 (preset))
    return kResultFalse;
  const int n = (version == 1) ? 16 : (version == 2) ? 17 : EwiVst::kStateCount;
  for (int i = 0; i < n; i++)
  {
    double v = 0.0;
    if (!s.readDouble (v))
      return kResultFalse;
    setParamNormalized (EwiVst::kStateOrder[i], v);
  }
  if (version < 2)
    setParamNormalized (EwiVst::kBend, 0.5);
  if (version < 3)
    setParamNormalized (EwiVst::kFilterGamma, EwiVst::filterGammaToNorm (1.5));
  return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API EwiVst::EwiVstController::getMidiControllerAssignment (
    int32 busIndex, int16 channel, CtrlNumber midiControllerNumber, ParamID& id)
{
  // Strict hosts (Cubase/Cantabile) convert MIDI CC/Pressure/Bend/ProgramChange
  // to parameters via this map and never deliver them as legacy events, so every
  // performance control must be mapped here on all 16 MIDI channels.
  if (busIndex != 0 || channel < 0 || channel >= 16)
    return kResultFalse;
  switch (midiControllerNumber)
  {
    case 1: id = EwiVst::kVibrato; return kResultTrue;
    case 2: id = EwiVst::kBreath; return kResultTrue;
    case 5: id = EwiVst::kGlide; return kResultTrue;
    case 7: id = EwiVst::kVolume; return kResultTrue;
    case 11: id = EwiVst::kExpression; return kResultTrue;
    case 102: id = EwiVst::kBreath; return kResultTrue;
    case 128: id = EwiVst::kBreath; return kResultTrue; // Channel Pressure -> breath
    case 129: id = EwiVst::kBend; return kResultTrue;   // PitchBend -> Bend param
    case 130: id = EwiVst::kPreset; return kResultTrue; // ProgramChange -> Preset
    default: return kResultFalse;
  }
}
} // namespace EwiVst
