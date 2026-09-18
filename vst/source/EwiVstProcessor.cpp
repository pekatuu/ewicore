// EwiVstProcessor.cpp
#include "EwiVstProcessor.h"
#include "EwiVstIds.h"
#include "EwiVstParams.h"
#include "public.sdk/source/vst/vsteventshelper.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstnoteexpression.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/base/ibstream.h"
#include "base/source/fstreamer.h"
#include <cstring>

namespace EwiVst {
using namespace Steinberg;
using namespace Steinberg::Vst;

EwiVstProcessor::EwiVstProcessor ()
{
  setControllerClass (EwiVst::CtrlUID);
  for (int i = 0; i < EwiVst::kNumParams; i++)
    mirror[i] = 0.0;
}

//------------------------------------------------------------------------
tresult PLUGIN_API EwiVst::EwiVstProcessor::initialize (FUnknown* context)
{
  tresult result = AudioEffect::initialize (context);
  if (result != kResultOk)
    return result;
  addAudioOutput (STR16 ("Stereo Out"), SpeakerArr::kStereo);
  addEventInput (STR16 ("Event In"), 1);
  for (int i = 0; i < EwiVst::kNumParams; i++)
    mirror[i] = EwiVst::defaultNorm (EwiVst::kStateOrder[i]);
  return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API EwiVst::EwiVstProcessor::setBusArrangements (
    SpeakerArrangement* inputs, int32 numIns, SpeakerArrangement* outputs, int32 numOuts)
{
  if (numIns != 0 || numOuts != 1)
    return kResultFalse;
  if (outputs[0] != SpeakerArr::kStereo)
    return kResultFalse;
  return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API EwiVst::EwiVstProcessor::setupProcessing (ProcessSetup& setup)
{
  Ewi_Init (&synth, (float)setup.sampleRate);
  return AudioEffect::setupProcessing (setup);
}

//------------------------------------------------------------------------
tresult PLUGIN_API EwiVst::EwiVstProcessor::canProcessSampleSize (int32 symbolicSampleSize)
{
  if (symbolicSampleSize == Vst::kSample32 || symbolicSampleSize == Vst::kSample64)
    return kResultTrue;
  return kResultFalse;
}

//------------------------------------------------------------------------
uint32 PLUGIN_API EwiVst::EwiVstProcessor::getTailSamples ()
{
  return 48000; // delay + reverb tails
}

//------------------------------------------------------------------------
int EwiVst::EwiVstProcessor::paramIndex (ParamID id) const
{
  for (int i = 0; i < EwiVst::kStateCount; i++)
    if (EwiVst::kStateOrder[i] == id)
      return i;
  return -1;
}

//------------------------------------------------------------------------
void EwiVst::EwiVstProcessor::applyParam (ParamID id, ParamValue norm)
{
  int idx = paramIndex (id);
  if (idx >= 0)
    mirror[idx] = norm;
  using namespace EwiVst;
  switch (id)
  {
    case kPreset: Ewi_SetPreset (&synth, (uint8_t)normToPreset (norm)); break;
    case kBreath: Ewi_CC (&synth, 2, (uint8_t)(norm * 127.0 + 0.5)); break;
    case kVibrato: Ewi_CC (&synth, 1, (uint8_t)(norm * 127.0 + 0.5)); break;
    case kVolume: Ewi_CC (&synth, 7, (uint8_t)(norm * 127.0 + 0.5)); break;
    case kExpression: Ewi_CC (&synth, 11, (uint8_t)(norm * 127.0 + 0.5)); break;
    case kCutoff: Ewi_SetCutoffBase (&synth, (float)(80.0 + norm * 7920.0)); break;
    case kBreathDepth: Ewi_SetBreathDepth (&synth, (float)(norm * 12000.0)); break;
    case kResonance: Ewi_SetResonance (&synth, (float)(norm * 0.95)); break;
    case kGlide: Ewi_SetGlide (&synth, (float)(norm * 0.5)); break;
    case kFormant: Ewi_SetFormantMix (&synth, (float)norm); break;
    case kDlyMix: Ewi_SetDelayMix (&synth, (float)(norm * 0.6)); break;
    case kDlyTime: Ewi_SetDelayTime (&synth, (float)normToDlyTime (norm)); break;
    case kDlyFb: Ewi_SetDelayFb (&synth, (float)(norm * 0.7)); break;
    case kRevMix: Ewi_SetRevMix (&synth, (float)(norm * 0.6)); break;
    case kRevSize: Ewi_SetRevSize (&synth, (float)norm); break;
    case kBypass: bypass = (norm > 0.5); break;
    default: break;
  }
}

//------------------------------------------------------------------------
void EwiVst::EwiVstProcessor::applyEvent (const Event& ev)
{
  switch (ev.type)
  {
    case Event::kNoteOnEvent:
      Ewi_NoteOn (&synth, (uint8_t)ev.noteOn.pitch,
                  (uint8_t)(ev.noteOn.velocity * 127.f + 0.5f));
      break;
    case Event::kNoteOffEvent: Ewi_NoteOff (&synth, (uint8_t)ev.noteOff.pitch); break;
    case Event::kNoteExpressionValueEvent:
    {
      // Some hosts deliver pitch bend as tuning note expression instead of
      // legacy MIDI CC129. Tuning is +/-120 semitones around 0.5; our bend
      // range is +/-2 semitones, so clamp into it.
      if (ev.noteExpressionValue.typeId == NoteExpressionTypeIDs::kTuningTypeID)
      {
        double semi = (ev.noteExpressionValue.value - 0.5) * 240.0;
        if (semi < -2.0)
          semi = -2.0;
        if (semi > 2.0)
          semi = 2.0;
        Ewi_PitchBend (&synth, 8192 + (int)(semi / 2.0 * 8192.0));
      }
      break;
    }
    case Event::kLegacyMIDICCOutEvent:
    {
      const uint8 cc = ev.midiCCOut.controlNumber;
      const uint8 v = (uint8_t)(ev.midiCCOut.value & 0x7F);
      if (cc == 1 || cc == 2 || cc == 5 || cc == 7 || cc == 11 || cc == 65 || cc == 102 ||
          cc == 120 || cc == 123 ||
          cc == ControllerNumbers::kAfterTouch) // Channel Pressure → ブレス
        Ewi_CC (&synth, cc, v);
      else if (cc == ControllerNumbers::kPitchBend)
        Ewi_PitchBend (&synth, (int)Helpers::getPitchBendValue (ev.midiCCOut));
      else if (cc == ControllerNumbers::kCtrlProgramChange)
        Ewi_SetPreset (&synth, (uint8_t)((v % EWI_NUM_PRESETS + EWI_NUM_PRESETS) % EWI_NUM_PRESETS));
      break;
    }
    default: break;
  }
}

//------------------------------------------------------------------------
void EwiVst::EwiVstProcessor::renderInto (float* outL, float* outR, int32 n)
{
  if (bypass)
  {
    for (int32 i = 0; i < n; i++)
    {
      outL[i] = 0.f;
      outR[i] = 0.f;
    }
    return;
  }
  Ewi_Render (&synth, outL, outR, (int)n);
}

//------------------------------------------------------------------------
tresult PLUGIN_API EwiVst::EwiVstProcessor::process (ProcessData& data)
{
  struct Action
  {
    int32 offset = 0;
    uint8 kind = 0; // 0=param, 1=event
    ParamID id = 0;
    ParamValue norm = 0.0;
    Event ev {};
  };
  static constexpr int kMaxActions = 512;
  Action actions[kMaxActions];
  int nAct = 0;
  auto pushParam = [&](int32 off, ParamID id, ParamValue v) {
    if (nAct < kMaxActions)
    {
      actions[nAct].offset = off < 0 ? 0 : (off > data.numSamples ? data.numSamples : off);
      actions[nAct].kind = 0;
      actions[nAct].id = id;
      actions[nAct].norm = v;
      nAct++;
    }
  };
  auto pushEvent = [&](const Event& e) {
    if (nAct < kMaxActions)
    {
      actions[nAct].offset =
          e.sampleOffset < 0 ? 0 : (e.sampleOffset > data.numSamples ? data.numSamples : e.sampleOffset);
      actions[nAct].kind = 1;
      actions[nAct].ev = e;
      nAct++;
    }
  };

  if (data.inputParameterChanges)
  {
    int32 n = data.inputParameterChanges->getParameterCount ();
    for (int32 i = 0; i < n; i++)
    {
      IParamValueQueue* q = data.inputParameterChanges->getParameterData (i);
      if (!q)
        continue;
      int32 np = q->getPointCount ();
      for (int32 p = 0; p < np; p++)
      {
        int32 off = 0;
        ParamValue v = 0.0;
        if (q->getPoint (p, off, v) == kResultTrue)
          pushParam (off, q->getParameterId (), v);
      }
    }
  }
  if (data.inputEvents)
  {
    int32 n = data.inputEvents->getEventCount ();
    for (int32 i = 0; i < n; i++)
    {
      Event e {};
      if (data.inputEvents->getEvent (i, e) == kResultOk)
        pushEvent (e);
    }
  }
  // insertion sort by offset (stable enough for our sizes)
  for (int i = 1; i < nAct; i++)
  {
    Action a = actions[i];
    int j = i - 1;
    while (j >= 0 && actions[j].offset > a.offset)
    {
      actions[j + 1] = actions[j];
      j--;
    }
    actions[j + 1] = a;
  }

  const bool is32 = (data.symbolicSampleSize == kSample32);
  float** o32 = is32 && data.numOutputs > 0 ? data.outputs[0].channelBuffers32 : nullptr;
  double** o64 = !is32 && data.numOutputs > 0 ? data.outputs[0].channelBuffers64 : nullptr;
  int32 pos = 0;
  auto renderTo = [&](int32 target) {
    while (pos < target)
    {
      int32 n = target - pos > 4096 ? 4096 : target - pos;
      if (o32)
      {
        renderInto (o32[0] + pos, o32[1] + pos, n);
      }
      else if (o64)
      {
        renderInto (tmpL, tmpR, n);
        for (int32 i = 0; i < n; i++)
        {
          o64[0][pos + i] = (double)tmpL[i];
          o64[1][pos + i] = (double)tmpR[i];
        }
      }
      else
      {
        // no outputs: still advance DSP so events stay in sync
        renderInto (tmpL, tmpR, n);
      }
      pos += n;
    }
  };
  for (int i = 0; i < nAct; i++)
  {
    renderTo (actions[i].offset);
    if (actions[i].kind == 0)
      applyParam (actions[i].id, actions[i].norm);
    else
      applyEvent (actions[i].ev);
  }
  renderTo (data.numSamples);
  return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API EwiVst::EwiVstProcessor::setState (IBStream* state)
{
  if (!state)
    return kResultFalse;
  IBStreamer s (state, kLittleEndian);
  int32 version = 0;
  if (!s.readInt32 (version) || version != 1)
    return kResultFalse;
  int32 preset = 0;
  if (!s.readInt32 (preset))
    return kResultFalse;
  Ewi_SetPreset (&synth, (uint8_t)preset);
  for (int i = 0; i < EwiVst::kStateCount; i++)
  {
    double v = 0.0;
    if (!s.readDouble (v))
      return kResultFalse;
    applyParam (EwiVst::kStateOrder[i], v);
  }
  return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API EwiVst::EwiVstProcessor::getState (IBStream* state)
{
  if (!state)
    return kResultFalse;
  IBStreamer s (state, kLittleEndian);
  s.writeInt32 (1);
  s.writeInt32 ((int32)synth.preset_no);
  for (int i = 0; i < EwiVst::kStateCount; i++)
  {
    int idx = paramIndex (EwiVst::kStateOrder[i]);
    s.writeDouble (idx >= 0 ? mirror[idx] : 0.0);
  }
  return kResultOk;
}
} // namespace EwiVst
