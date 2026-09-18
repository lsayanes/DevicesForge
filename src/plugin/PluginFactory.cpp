#include "PluginProcessor.h"
#include "PluginCIDs.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"

#define stringPluginCategory "Fx"

using namespace Steinberg::Vst;

// ============================================================================
// VST Plug-in Entry
// ============================================================================
BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)

    // Processor
    DEF_CLASS2(INLINE_UID_FROM_FUID(DevicesForge::PluginProcessorUID),
               PClassInfo::kManyInstances,
               kVstAudioEffectClass,
               stringPluginName,
               Vst::kDistributable,
               stringPluginCategory,
               FULL_VERSION_STR,
               kVstVersionString,
               DevicesForgeProcessor::createInstance)

    // Controller
    DEF_CLASS2(INLINE_UID_FROM_FUID(DevicesForge::PluginControllerUID),
               PClassInfo::kManyInstances,
               kVstComponentControllerClass,
               stringPluginName "Controller",
               0,
               "",
               FULL_VERSION_STR,
               kVstVersionString,
               DevicesForgeController::createInstance)

END_FACTORY
