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
  return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API EwiVst::EwiVstController::setComponentState (IBStream* state)
{
  if (!state)
    return kResultFalse;
  // processor state: version + preset + kStateCount doubles (see processor getState)
  IBStreamer s (state, kLittleEndian);
  int32 version = 0;
  if (!s.readInt32 (version) || version != 1)
    return kResultFalse;
  int32 preset = 0;
  if (!s.readInt32 (preset))
    return kResultFalse;
  for (int i = 0; i < EwiVst::kStateCount; i++)
  {
    double v = 0.0;
    if (!s.readDouble (v))
      return kResultFalse;
    setParamNormalized (EwiVst::kStateOrder[i], v);
  }
  return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API EwiVst::EwiVstController::getMidiControllerAssignment (
    int32 busIndex, int16 channel, CtrlNumber midiControllerNumber, ParamID& id)
{
  if (busIndex != 0 || channel != 0)
    return kResultFalse;
  switch (midiControllerNumber)
  {
    case 1: id = EwiVst::kVibrato; return kResultTrue;
    case 2: id = EwiVst::kBreath; return kResultTrue;
    case 5: id = EwiVst::kGlide; return kResultTrue;
    case 7: id = EwiVst::kVolume; return kResultTrue;
    case 11: id = EwiVst::kExpression; return kResultTrue;
    case 102: id = EwiVst::kBreath; return kResultTrue;
    default: return kResultFalse;
  }
}
} // namespace EwiVst
