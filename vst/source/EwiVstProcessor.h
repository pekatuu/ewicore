// EwiVstProcessor.h — VST3 audio processor driving the shared EWI core
#pragma once
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "EwiVstParams.h"
#include "ewi_synth.h"

namespace EwiVst {
class EwiVstProcessor : public Steinberg::Vst::AudioEffect
{
public:
  EwiVstProcessor ();
  Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API setBusArrangements (
      Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
      Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API setupProcessing (Steinberg::Vst::ProcessSetup& setup) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API canProcessSampleSize (Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;
  Steinberg::uint32 PLUGIN_API getTailSamples () SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API process (Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API setState (Steinberg::IBStream* state) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API getState (Steinberg::IBStream* state) SMTG_OVERRIDE;

  static Steinberg::FUnknown* createInstance (void*) { return (Steinberg::Vst::IAudioProcessor*)new EwiVstProcessor; }

protected:
  void applyParam (Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue norm);
  void applyEvent (const Steinberg::Vst::Event& ev);
  void renderInto (float* outL, float* outR, Steinberg::int32 n);
  int paramIndex (Steinberg::Vst::ParamID id) const;

  EwiSynth synth;
  double mirror[kNumParams] {};
  bool bypass {false};
  bool active {false};
  // 64-bit rendering scratch (chunked)
  float tmpL[4096];
  float tmpR[4096];
};
} // namespace EwiVst
