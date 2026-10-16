#pragma once

#include "pluginterfaces/base/funknown.h"

namespace DevicesForge {

// ============================================================================
// Plugin UIDs
// ============================================================================
static const Steinberg::FUID PluginProcessorUID(0x1A2B3C4D, 0x5E6F7A8B, 0x9C0D1E2F, 0x3A4B5C6D);
static const Steinberg::FUID PluginControllerUID(0x6D5C4B3A, 0x2F1E0D9C, 0x8B7A6F5E, 0x4D3C2B1A);

// Define UIDs for inline use
#define DevicesForgeProcessorUID DevicesForge::PluginProcessorUID
#define DevicesForgeControllerUID DevicesForge::PluginControllerUID

} // namespace DevicesForge
