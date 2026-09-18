// bend_test.cpp — VST processor-level bend tracking test (console harness).
// Drives EwiVstProcessor::process() like a host: note + breath, then bend
// gestures via legacy MIDI CC129 AND note-expression tuning, repeated.
// Measures output frequency by zero-crossing. Exit 0 = all pass.
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <utility>

#include "EwiVstProcessor.h"
#include "EwiVstParams.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstnoteexpression.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "base/source/fobject.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

//--- minimal IEventList ------------------------------------------------
class EvList : public IEventList, public FObject
{
public:
  OBJ_METHODS (EvList, FObject)
  DEFINE_INTERFACES
  DEF_INTERFACE (IEventList)
  END_DEFINE_INTERFACES (FObject)
  REFCOUNT_METHODS (FObject)

  int32 PLUGIN_API getEventCount () SMTG_OVERRIDE { return (int32)evs.size (); }
  tresult PLUGIN_API getEvent (int32 index, Event& e) SMTG_OVERRIDE
  {
    if (index < 0 || index >= (int32)evs.size ())
      return kResultFalse;
    e = evs[(size_t)index];
    return kResultOk;
  }
  tresult PLUGIN_API addEvent (Event& e) SMTG_OVERRIDE
  {
    evs.push_back (e);
    return kResultOk;
  }
  void clear () { evs.clear (); }
  std::vector<Event> evs;
};

//--- minimal IParamValueQueue / IParameterChanges -----------------------
class PQueue : public IParamValueQueue, public FObject
{
public:
  OBJ_METHODS (PQueue, FObject)
  DEFINE_INTERFACES
  DEF_INTERFACE (IParamValueQueue)
  END_DEFINE_INTERFACES (FObject)
  REFCOUNT_METHODS (FObject)

  ParamID PLUGIN_API getParameterId () SMTG_OVERRIDE { return id; }
  int32 PLUGIN_API getPointCount () SMTG_OVERRIDE { return (int32)pts.size (); }
  tresult PLUGIN_API getPoint (int32 index, int32& off, ParamValue& v) SMTG_OVERRIDE
  {
    if (index < 0 || index >= (int32)pts.size ())
      return kResultFalse;
    off = pts[(size_t)index].first;
    v = pts[(size_t)index].second;
    return kResultOk;
  }
  tresult PLUGIN_API addPoint (int32, ParamValue, int32&) SMTG_OVERRIDE { return kResultFalse; }
  ParamID id = 0;
  std::vector<std::pair<int32, ParamValue>> pts;
};

class PChanges : public IParameterChanges, public FObject
{
public:
  OBJ_METHODS (PChanges, FObject)
  DEFINE_INTERFACES
  DEF_INTERFACE (IParameterChanges)
  END_DEFINE_INTERFACES (FObject)
  REFCOUNT_METHODS (FObject)

  int32 PLUGIN_API getParameterCount () SMTG_OVERRIDE { return (int32)queues.size (); }
  IParamValueQueue* PLUGIN_API getParameterData (int32 index) SMTG_OVERRIDE
  {
    if (index < 0 || index >= (int32)queues.size ())
      return nullptr;
    return &queues[(size_t)index];
  }
  IParamValueQueue* PLUGIN_API addParameterData (const ParamID&, int32&) SMTG_OVERRIDE
  {
    return nullptr;
  }
  void clear () { queues.clear (); }
  std::vector<PQueue> queues;
};

//--- helpers -----------------------------------------------------------
static constexpr int SR = 48000;
static constexpr int BLK = 512;

static void evNoteOn (Event& e, int pitch, float vel)
{
  memset (&e, 0, sizeof (e));
  e.type = Event::kNoteOnEvent;
  e.sampleOffset = 0;
  e.noteOn.channel = 0;
  e.noteOn.pitch = (int16)pitch;
  e.noteOn.velocity = vel;
  e.noteOn.noteId = -1;
}
static void evCC (Event& e, int cc, int v)
{
  memset (&e, 0, sizeof (e));
  e.type = Event::kLegacyMIDICCOutEvent;
  e.sampleOffset = 0;
  e.midiCCOut.controlNumber = (uint8)cc;
  e.midiCCOut.channel = 0;
  e.midiCCOut.value = (int8)v;
  e.midiCCOut.value2 = 0;
}
static void evBend (Event& e, int v14)
{
  memset (&e, 0, sizeof (e));
  e.type = Event::kLegacyMIDICCOutEvent;
  e.sampleOffset = 0;
  e.midiCCOut.controlNumber = 129; // kPitchBend
  e.midiCCOut.channel = 0;
  e.midiCCOut.value = (int8)(v14 & 0x7F);
  e.midiCCOut.value2 = (int8)((v14 >> 7) & 0x7F);
}
static void evTuning (Event& e, double v)
{
  memset (&e, 0, sizeof (e));
  e.type = Event::kNoteExpressionValueEvent;
  e.sampleOffset = 0;
  e.noteExpressionValue.typeId = NoteExpressionTypeIDs::kTuningTypeID;
  e.noteExpressionValue.noteId = -1;
  e.noteExpressionValue.value = v;
}

static int failures = 0;
static void check (bool ok, const char* name, double f)
{
  printf ("%s %s: %.1f Hz\n", ok ? "PASS" : "FAIL", name, f);
  if (!ok)
    failures++;
}

int main ()
{
  EwiVst::EwiVstProcessor proc;
  if (proc.initialize (nullptr) != kResultOk)
  {
    printf ("FAIL initialize\n");
    return 1;
  }
  ProcessSetup setup {};
  setup.sampleRate = SR;
  setup.maxSamplesPerBlock = BLK;
  setup.symbolicSampleSize = kSample32;
  setup.processMode = kRealtime;
  if (proc.setupProcessing (setup) != kResultOk)
  {
    printf ("FAIL setupProcessing\n");
    return 1;
  }
  proc.setActive (true);
  proc.setProcessing (true);

  EvList evs;
  PChanges params;
  float outL[BLK], outR[BLK];
  AudioBusBuffers outBuf {};
  outBuf.numChannels = 2;
  float* outs[2] = {outL, outR};
  outBuf.channelBuffers32 = outs;

  // measurement scratch: render N blocks, freq from last 60% via rising crossings
  std::vector<float> acc;
  acc.reserve (SR);
  auto runBlocks = [&](int n) {
    for (int b = 0; b < n; b++)
    {
      ProcessData data {};
      data.processMode = kRealtime;
      data.symbolicSampleSize = kSample32;
      data.numSamples = BLK;
      data.numOutputs = 1;
      data.outputs = &outBuf;
      data.inputEvents = &evs;
      data.inputParameterChanges = &params;
      if (proc.process (data) != kResultOk)
      {
        printf ("FAIL process\n");
        exit (1);
      }
      for (int i = 0; i < BLK; i++)
        acc.push_back (outL[i]);
      evs.clear ();
      params.clear ();
    }
  };
  auto measure = [&]() {
    size_t n = acc.size ();
    // 1kHz LPF (高調波・うなり対策) + 自己相関 (350..560Hz)
    std::vector<float> lp (n);
    float s = 0.f;
    const float c = 1.f - expf (-2.f * 3.14159265f * 1000.f / (float)SR);
    for (size_t i = 0; i < n; i++)
    {
      s += (acc[i] - s) * c;
      lp[i] = s;
    }
    size_t skip = n * 4 / 10;
    int kMin = SR / 650, kMax = SR / 300; // 300..650Hz (bend min 392〜74番587Hz対応)
    double best = 0.0;
    int bestK = 0;
    for (int k = kMin; k <= kMax; k++)
    {
      double r = 0.0;
      for (size_t i = skip; i < n - (size_t)kMax; i++)
        r += (double)lp[i] * (double)lp[i + (size_t)k];
      if (r > best)
      {
        best = r;
        bestK = k;
      }
    }
    double f = bestK > 0 ? (double)SR / (double)bestK : 0.0;
    acc.clear ();
    return f;
  };
  auto param = [&](EwiVst::ParamID id, double v) {
    PQueue q;
    q.id = id;
    q.pts.push_back ({0, v});
    params.queues.push_back (q);
  };
  auto evt = [&](const Event& e) { evs.evs.push_back (e); };
  auto rmsOf = [&]() {
    double s = 0.0;
    for (size_t i = 0; i < acc.size (); i++)
      s += (double)acc[i] * (double)acc[i];
    return sqrt (s / (double)(acc.size () ? acc.size () : 1));
  };
  Event e {};

  // clean measurement voice: cutoff open, FX off, resonance 0
  param (EwiVst::kCutoff, 1.0);
  param (EwiVst::kResonance, 0.0);
  param (EwiVst::kDlyMix, 0.0);
  param (EwiVst::kRevMix, 0.0);
  param (EwiVst::kGlide, 0.0);
  evNoteOn (e, 69, 0.9f);
  evt (e);
  evCC (e, 2, 110);
  evt (e);
  runBlocks (48); // ~0.5 s settle
  {
    double f = measure ();
    check (f > 440 * 0.97 && f < 440 * 1.03, "base A4", f);
  }
  // CC102 breath (CC2=0 first so only CC102 opens the VCA)
  evCC (e, 2, 0);
  evt (e);
  evCC (e, 102, 110);
  evt (e);
  runBlocks (20);
  {
    double r = rmsOf ();
    bool ok = r > 0.01;
    printf ("%s CC102 breath opens VCA: rms %.4f\n", ok ? "PASS" : "FAIL", r);
    if (!ok)
      failures++;
    double f = measure ();
    check (f > 440 * 0.97 && f < 440 * 1.03, "CC102 pitch steady", f);
  }
  // Aftertouch breath (CC2/CC102を0に戻してからAftertouchだけで開くこと)
  evCC (e, 2, 0);
  evt (e);
  evCC (e, 102, 0);
  evt (e);
  evCC (e, 128, 110);
  evt (e);
  runBlocks (20);
  {
    double r = rmsOf ();
    bool ok = r > 0.01;
    printf ("%s Aftertouch breath opens VCA: rms %.4f\n", ok ? "PASS" : "FAIL", r);
    if (!ok)
      failures++;
    double f = measure ();
    check (f > 440 * 0.97 && f < 440 * 1.03, "Aftertouch pitch steady", f);
  }
  // CC5 glide: slow slide 69->74, then snap back with CC5=0
  evCC (e, 5, 127);
  evt (e);
  evNoteOn (e, 74, 0.9f);
  evt (e);
  runBlocks (5); // ~52 ms: must still be near 69 (glide lagging)
  {
    double f = measure ();
    check (f < 500.0, "CC5 glide lagging", f);
  }
  runBlocks (120); // 0.4s時定数に十分な整定
  {
    double f = measure ();
    check (f > 587 * 0.97 && f < 587 * 1.03, "CC5 glide arrived", f);
  }
  evCC (e, 5, 0);
  evt (e);
  evNoteOn (e, 69, 0.9f);
  evt (e);
  runBlocks (5); // preset glide 12 ms: snaps back quickly
  {
    double f = measure ();
    check (f > 440 * 0.97 && f < 440 * 1.03, "CC5=0 snap back", f);
  }
  // legacy bend max #1
  evBend (e, 16383);
  evt (e);
  runBlocks (30);
  {
    double f = measure ();
    check (f > 440 * 1.09 && f < 440 * 1.16, "legacy bend max #1", f);
  }
  // legacy bend center
  evBend (e, 8192);
  evt (e);
  runBlocks (30);
  {
    double f = measure ();
    check (f > 440 * 0.97 && f < 440 * 1.03, "legacy bend center", f);
  }
  // legacy bend max #2 (user case)
  evBend (e, 16383);
  evt (e);
  runBlocks (30);
  {
    double f = measure ();
    check (f > 440 * 1.09 && f < 440 * 1.16, "legacy bend max #2", f);
  }
  // note-expression tuning +2st
  evTuning (e, 0.5 + 2.0 / 240.0);
  evt (e);
  runBlocks (30);
  {
    double f = measure ();
    check (f > 440 * 1.09 && f < 440 * 1.16, "noteexpr tuning +2st", f);
  }
  // note-expression tuning center
  evTuning (e, 0.5);
  evt (e);
  runBlocks (30);
  {
    double f = measure ();
    check (f > 440 * 0.97 && f < 440 * 1.03, "noteexpr tuning center", f);
  }
  // note-expression tuning +2st again
  evTuning (e, 0.5 + 2.0 / 240.0);
  evt (e);
  runBlocks (30);
  {
    double f = measure ();
    check (f > 440 * 1.09 && f < 440 * 1.16, "noteexpr tuning +2st #2", f);
  }

  proc.setProcessing (false);
  proc.setActive (false);
  proc.terminate ();
  if (failures)
  {
    printf ("NG: %d stage(s) failed\n", failures);
    return 1;
  }
  printf ("VSTBEND-OK\n");
  return 0;
}
