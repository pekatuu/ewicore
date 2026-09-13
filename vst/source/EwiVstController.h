// EwiVstController.h — parameter controller + MIDI CC mapping (no custom GUI)
#pragma once
#include "public.sdk/source/vst/vsteditcontroller.h"

namespace EwiVst {
class EwiVstController : public Steinberg::Vst::EditController, public Steinberg::Vst::IMidiMapping
{
public:
  Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API setComponentState (Steinberg::IBStream* state) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API getMidiControllerAssignment (
      Steinberg::int32 busIndex, Steinberg::int16 channel,
      Steinberg::Vst::CtrlNumber midiControllerNumber, Steinberg::Vst::ParamID& id) SMTG_OVERRIDE;

  static Steinberg::FUnknown* createInstance (void*) { return (Steinberg::Vst::IEditController*)new EwiVstController; }

  OBJ_METHODS (EwiVstController, Steinberg::Vst::EditController)
  DEFINE_INTERFACES
  DEF_INTERFACE (Steinberg::Vst::IMidiMapping)
  END_DEFINE_INTERFACES (Steinberg::Vst::EditController)
  REFCOUNT_METHODS (Steinberg::Vst::EditController)
};
} // namespace EwiVst
