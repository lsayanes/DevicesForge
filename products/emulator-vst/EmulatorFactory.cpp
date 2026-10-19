#include "EmulatorController.h"
#include "EmulatorIDs.h"
#include "EmulatorProcessor.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace DevicesForge::Emulator;

BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)

	DEF_CLASS2(INLINE_UID_FROM_FUID(ProcessorUID),
			   PClassInfo::kManyInstances,
			   kVstAudioEffectClass,
			   stringPluginName,
			   Vst::kDistributable,
			   PlugType::kFx,
			   FULL_VERSION_STR,
			   kVstVersionString,
			   EmulatorProcessor::createInstance)

	DEF_CLASS2(INLINE_UID_FROM_FUID(ControllerUID),
			   PClassInfo::kManyInstances,
			   kVstComponentControllerClass,
			   "Virtual Devices Controller",
			   0,
			   "",
			   FULL_VERSION_STR,
			   kVstVersionString,
			   EmulatorController::createInstance)

END_FACTORY
