// strict_test.cpp — strict-host (Cubase/Cantabile) simulation.
// MIDI CC/Pressure/Bend/ProgramChange arrive ONLY as parameters via IMidiMapping,
// never as LegacyMIDICCOut events. Verifies mapping + param path.
#include <cstdio>
#include <cmath>
#include <cstring>
#include <vector>
#include <utility>

#include "EwiVstProcessor.h"
#include "EwiVstController.h"
#include "EwiVstParams.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "base/source/fobject.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

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

static constexpr int SR = 48000;
static constexpr int BLK = 512;
static int failures = 0;
static void check (bool ok, const char* name)
{
  printf ("%s %s\n", ok ? "PASS" : "FAIL", name);
  if (!ok)
    failures++;
}

int main ()
{
  // 1) IMidiMapping: strict host queries all channels + extended controllers
  {
    EwiVst::EwiVstController ctrl;
    if (ctrl.initialize (nullptr) != kResultOk)
    {
      printf ("FAIL controller initialize\n");
      return 1;
    }
    ParamID id = 0;
    // CC2 breath on every MIDI channel 0..15
    for (int ch = 0; ch < 16; ch++)
    {
      bool ok = ctrl.getMidiControllerAssignment (0, (int16)ch, 2, id) == kResultOk && id == EwiVst::kBreath;
      if (!ok)
      {
        printf ("FAIL CC2 mapping ch=%d\n", ch);
        failures++;
      }
    }
    check (true, "CC2 mapped on ch0..15");
    struct MapCase
    {
      int cc;
      EwiVst::ParamID expect;
      const char* name;
    };
    MapCase cases[] = {
      {1, EwiVst::kVibrato, "CC1->Vibrato"},
      {5, EwiVst::kGlide, "CC5->Glide"},
      {7, EwiVst::kVolume, "CC7->Volume"},
      {11, EwiVst::kExpression, "CC11->Expression"},
      {102, EwiVst::kBreath, "CC102->Breath"},
      {128, EwiVst::kBreath, "AfterTouch->Breath"},
      {129, EwiVst::kBend, "PitchBend->Bend"},
      {130, EwiVst::kPreset, "ProgramChange->Preset"},
    };
    for (auto& c : cases)
    {
      ParamID got = 0;
      bool ok = ctrl.getMidiControllerAssignment (0, 0, (CtrlNumber)c.cc, got) == kResultOk && got == c.expect;
      check (ok, c.name);
    }
    // invalid bus must fail
    {
      ParamID got = 0;
      check (ctrl.getMidiControllerAssignment (1, 0, 2, got) != kResultOk, "bus1 rejected");
      check (ctrl.getMidiControllerAssignment (0, 16, 2, got) != kResultOk, "ch16 rejected");
    }
    ctrl.terminate ();
  }

  // 2) Processor param path (no legacy CC events at all)
  EwiVst::EwiVstProcessor proc;
  if (proc.initialize (nullptr) != kResultOk)
  {
    printf ("FAIL proc initialize\n");
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
  auto rmsOf = [&]() {
    double s = 0.0;
    for (size_t i = 0; i < acc.size (); i++)
      s += (double)acc[i] * (double)acc[i];
    double r = sqrt (s / (double)(acc.size () ? acc.size () : 1));
    acc.clear ();
    return r;
  };
  auto measure = [&]() {
    size_t n = acc.size ();
    std::vector<float> lp (n);
    float s = 0.f;
    const float c = 1.f - expf (-2.f * 3.14159265f * 1000.f / (float)SR);
    for (size_t i = 0; i < n; i++)
    {
      s += (acc[i] - s) * c;
      lp[i] = s;
    }
    size_t skip = n * 4 / 10;
    int kMin = SR / 650, kMax = SR / 300;
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

  // NoteOn via event, breath via PARAM (strict host converts CC2->Breath)
  param (EwiVst::kCutoff, 1.0);
  param (EwiVst::kResonance, 0.0);
  param (EwiVst::kDlyMix, 0.0);
  param (EwiVst::kRevMix, 0.0);
  param (EwiVst::kGlide, 0.0);
  {
    Event e {};
    memset (&e, 0, sizeof (e));
    e.type = Event::kNoteOnEvent;
    e.noteOn.channel = 0;
    e.noteOn.pitch = 69;
    e.noteOn.velocity = 0.9f;
    e.noteOn.noteId = -1;
    evs.evs.push_back (e);
  }
  param (EwiVst::kBreath, 110.0 / 127.0);
  runBlocks (48);
  {
    double r = rmsOf ();
    // need fresh audio for rms: re-run with same state
    param (EwiVst::kBreath, 110.0 / 127.0);
    runBlocks (20);
    r = rmsOf ();
    check (r > 0.01, "param Breath opens VCA");
  }
  // Bend via PARAM (strict host converts PitchBend->Bend)
  param (EwiVst::kBend, 1.0);
  runBlocks (30);
  {
    double f = measure ();
    bool ok = f > 440 * 1.09 && f < 440 * 1.16;
    printf ("%s param Bend max: %.1f Hz\n", ok ? "PASS" : "FAIL", f);
    if (!ok)
      failures++;
  }
  param (EwiVst::kBend, 0.5);
  runBlocks (30);
  {
    double f = measure ();
    bool ok = f > 440 * 0.97 && f < 440 * 1.03;
    printf ("%s param Bend center: %.1f Hz\n", ok ? "PASS" : "FAIL", f);
    if (!ok)
      failures++;
  }

  proc.setProcessing (false);
  proc.setActive (false);
  proc.terminate ();
  if (failures)
  {
    printf ("NG: %d stage(s) failed\n", failures);
    return 1;
  }
  printf ("VSTSTRICT-OK\n");
  return 0;
}
