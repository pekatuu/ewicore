// factory.cpp — VST3 plug-in factory
#include "public.sdk/source/main/pluginfactory.h"
#include "EwiVstIds.h"
#include "EwiVstProcessor.h"
#include "EwiVstController.h"

#define stringPluginName "EWI Axis VA"
#define stringVendor "pekat"
#define stringUrl ""
#define stringEmail ""

#define stringCtrlName "EWI Axis VA Controller"

using namespace Steinberg;
using namespace Steinberg::Vst;

BEGIN_FACTORY_DEF (stringVendor, stringUrl, stringEmail)

DEF_CLASS2 (INLINE_UID_FROM_FUID (EwiVst::ProcUID), PClassInfo::kManyInstances,
            kVstAudioEffectClass, stringPluginName, Vst::kDistributable,
            "Instrument|Synth|Mono",
            "1.0.0", kVstVersionString, EwiVst::EwiVstProcessor::createInstance)

DEF_CLASS2 (INLINE_UID_FROM_FUID (EwiVst::CtrlUID), PClassInfo::kManyInstances,
            kVstComponentControllerClass, stringCtrlName, 0, "",
            "1.0.0", kVstVersionString, EwiVst::EwiVstController::createInstance)

END_FACTORY
