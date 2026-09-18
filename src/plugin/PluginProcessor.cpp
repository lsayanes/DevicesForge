#include "PluginProcessor.h"
#include "public.sdk/source/vst/vstaudioprocessoralgo.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "base/source/fstreamer.h"

#include <cstring>
#include <cmath>

namespace Steinberg {
namespace Vst {

// ============================================================================
// Processor Implementation
// ============================================================================

DevicesForgeProcessor::DevicesForgeProcessor() {
    setControllerClass(DevicesForgeControllerUID);
}

DevicesForgeProcessor::~DevicesForgeProcessor() {}

tresult PLUGIN_API DevicesForgeProcessor::initialize(FUnknown* context) {
    tresult result = AudioEffect::initialize(context);
    if (result != kResultOk) return result;

    addAudioInput(STR16("Stereo In"), SpeakerArr::kStereo);
    addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
    addEventInput(STR16("Event In"), 1);

    return kResultOk;
}

tresult PLUGIN_API DevicesForgeProcessor::terminate() {
    return AudioEffect::terminate();
}

tresult PLUGIN_API DevicesForgeProcessor::setActive(TBool state) {
    if (state) {
        convolver.prepare(processSetup.sampleRate, DevicesForge::FFT_SIZE);
    } else {
        convolver.reset();
    }
    return AudioEffect::setActive(state);
}

tresult PLUGIN_API DevicesForgeProcessor::process(ProcessData& data) {
    // 1) Read input parameter changes
    if (IParameterChanges* paramChanges = data.inputParameterChanges) {
        int32 numParamsChanged = paramChanges->getParameterCount();
        for (int32 i = 0; i < numParamsChanged; i++) {
            if (IParamValueQueue* paramQueue = paramChanges->getParameterData(i)) {
                ParamValue value;
                int32 sampleOffset;
                int32 numPoints = paramQueue->getPointCount();
                switch (paramQueue->getParameterId()) {
                    case DevicesForge::PluginParamIDs::MIX:
                        if (paramQueue->getPoint(numPoints - 1, sampleOffset, value) == kResultTrue) {
                            paramMix = static_cast<float>(value);
                        }
                        break;
                    case DevicesForge::PluginParamIDs::OUTPUT_GAIN:
                        if (paramQueue->getPoint(numPoints - 1, sampleOffset, value) == kResultTrue) {
                            paramOutputGain = static_cast<float>(value);
                        }
                        break;
                    case DevicesForge::PluginParamIDs::IR_SELECT:
                        if (paramQueue->getPoint(numPoints - 1, sampleOffset, value) == kResultTrue) {
                            paramIRSelect = static_cast<float>(value);
                        }
                        break;
                    case DevicesForge::PluginParamIDs::AI_DENOISE:
                        if (paramQueue->getPoint(numPoints - 1, sampleOffset, value) == kResultTrue) {
                            paramAIDenoise = static_cast<float>(value);
                        }
                        break;
                }
            }
        }
    }

    // 2) Process audio
    if (data.numInputs == 0 || data.numOutputs == 0) {
        return kResultOk;
    }

    int32 numChannels = data.inputs[0].numChannels;
    uint32 sampleFramesSize = getSampleFramesSizeInBytes(processSetup, data.numSamples);
    void** in = getChannelBuffersPointer(processSetup, data.inputs[0]);
    void** out = getChannelBuffersPointer(processSetup, data.outputs[0]);

    // Check if silence
    if (data.inputs[0].silenceFlags == getChannelMask(data.inputs[0].numChannels)) {
        data.outputs[0].silenceFlags = data.inputs[0].silenceFlags;
        return kResultOk;
    }

    // Process
    for (int32 channel = 0; channel < numChannels; ++channel) {
        float* inputBuffer = static_cast<float*>(in[channel]);
        float* outputBuffer = static_cast<float*>(out[channel]);

        // Apply output gain
        float gainLinear = std::pow(10.0f, (paramOutputGain * 24.0f - 12.0f) / 20.0f);
        for (int32 i = 0; i < data.numSamples; ++i) {
            outputBuffer[i] = inputBuffer[i] * gainLinear;
        }
    }

    // Mark output as not silent
    data.outputs[0].silenceFlags = 0;

    return kResultOk;
}

tresult PLUGIN_API DevicesForgeProcessor::setState(IBStream* state) {
    if (!state) return kResultFalse;

    IBStreamer streamer(state, kLittleEndian);
    float savedMix = 1.0f;
    float savedGain = 0.0f;
    
    streamer.readFloat(savedMix);
    streamer.readFloat(savedGain);

    paramMix = savedMix;
    paramOutputGain = savedGain;

    return kResultOk;
}

tresult PLUGIN_API DevicesForgeProcessor::getState(IBStream* state) {
    if (!state) return kResultFalse;

    IBStreamer streamer(state, kLittleEndian);
    streamer.writeFloat(paramMix);
    streamer.writeFloat(paramOutputGain);

    return kResultOk;
}

// ============================================================================
// Controller Implementation
// ============================================================================

DevicesForgeController::DevicesForgeController() {}
DevicesForgeController::~DevicesForgeController() {}

tresult PLUGIN_API DevicesForgeController::initialize(FUnknown* context) {
    tresult result = EditController::initialize(context);
    if (result != kResultOk) return result;

    // Mix parameter (0-100%)
    parameters.addParameter(STR16("Mix"), STR16("%"), 100, 1.0,
        ParameterInfo::kCanAutomate, DevicesForge::PluginParamIDs::MIX);

    // Gain parameter (-12 to +12 dB)
    parameters.addParameter(STR16("Gain"), STR16("dB"), 100, 0.5,
        ParameterInfo::kCanAutomate, DevicesForge::PluginParamIDs::OUTPUT_GAIN);

    // IR Select (0-3)
    parameters.addParameter(STR16("IR"), nullptr, 3, 0.0,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsList, DevicesForge::PluginParamIDs::IR_SELECT);

    // AI Denoise (0 or 1)
    parameters.addParameter(STR16("AI"), nullptr, 1, 0.0,
        ParameterInfo::kCanAutomate, DevicesForge::PluginParamIDs::AI_DENOISE);

    return kResultOk;
}

tresult PLUGIN_API DevicesForgeController::terminate() {
    return EditController::terminate();
}

tresult PLUGIN_API DevicesForgeController::setComponentState(IBStream* state) {
    if (!state) return kResultFalse;

    IBStreamer streamer(state, kLittleEndian);
    float savedMix = 1.0f;
    float savedGain = 0.0f;
    
    streamer.readFloat(savedMix);
    streamer.readFloat(savedGain);

    setParamNormalized(DevicesForge::PluginParamIDs::MIX, savedMix);
    setParamNormalized(DevicesForge::PluginParamIDs::OUTPUT_GAIN, savedGain);

    return kResultOk;
}

} // namespace Vst
} // namespace Steinberg
