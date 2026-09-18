#include "PluginProcessor.h"
#include "public.sdk/source/vst/vstaudioprocessoralgo.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "base/source/fstreamer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace Steinberg 
{
    namespace Vst 
    {

        namespace 
        {
            float outputGainLinear(float normalizedGain)
            {
                return std::pow(10.0f, (normalizedGain * 24.0f - 12.0f) / 20.0f);
            }
        }

        DevicesForgeProcessor::DevicesForgeProcessor() 
        {
            setControllerClass(DevicesForgeControllerUID);
        }

        DevicesForgeProcessor::~DevicesForgeProcessor() {}

        tresult PLUGIN_API DevicesForgeProcessor::initialize(FUnknown* context) 
        {
            tresult result = AudioEffect::initialize(context);
            if (result != kResultOk) return result;

            addAudioInput(STR16("Stereo In"), SpeakerArr::kStereo);
            addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
            addEventInput(STR16("Event In"), 1);

            return kResultOk;
        }

        tresult PLUGIN_API DevicesForgeProcessor::terminate() 
        {
            return AudioEffect::terminate();
        }

        tresult PLUGIN_API DevicesForgeProcessor::setActive(TBool state) 
        {
            if (state) 
            {
                convolver.prepare(processSetup.sampleRate, DevicesForge::FFT_SIZE);
                generator.prepare(processSetup.sampleRate);
                captureBuffer.prepare(processSetup.sampleRate, DevicesForge::NUM_CHANNELS);
                prevGenerateOn = paramGenerate >= 0.5f;
            } 
            else 
            {
                convolver.reset();
                generator.reset();
                captureBuffer.reset();
            }
            
            return AudioEffect::setActive(state);
        }

        tresult PLUGIN_API DevicesForgeProcessor::process(ProcessData& data) 
        {
            if (IParameterChanges* paramChanges = data.inputParameterChanges) 
            {
                int32 numParamsChanged = paramChanges->getParameterCount();
            
                for (int32 i = 0; i < numParamsChanged; i++) 
                {
                    if (IParamValueQueue* paramQueue = paramChanges->getParameterData(i)) 
                    {
                        ParamValue value;
                        int32 sampleOffset;
                        int32 numPoints = paramQueue->getPointCount();
                        
                        switch (paramQueue->getParameterId()) 
                        {
                            case DevicesForge::PluginParamIDs::MIX:
                                if (paramQueue->getPoint(numPoints - 1, sampleOffset, value) == kResultTrue) 
                                    paramMix = static_cast<float>(value);
                                break;
                            case DevicesForge::PluginParamIDs::OUTPUT_GAIN:
                                if (paramQueue->getPoint(numPoints - 1, sampleOffset, value) == kResultTrue) 
                                    paramOutputGain = static_cast<float>(value);
                                break;
                            case DevicesForge::PluginParamIDs::IR_SELECT:
                                if (paramQueue->getPoint(numPoints - 1, sampleOffset, value) == kResultTrue) 
                                    paramIRSelect = static_cast<float>(value);
                                break;
                            case DevicesForge::PluginParamIDs::AI_DENOISE:
                                if (paramQueue->getPoint(numPoints - 1, sampleOffset, value) == kResultTrue) 
                                    paramAIDenoise = static_cast<float>(value);
                                break;
                            case DevicesForge::PluginParamIDs::SIGNAL_TYPE:
                                if (paramQueue->getPoint(numPoints - 1, sampleOffset, value) == kResultTrue) 
                                    paramSignalType = static_cast<float>(value);
                                break;
                            case DevicesForge::PluginParamIDs::SIGNAL_DURATION:
                                if (paramQueue->getPoint(numPoints - 1, sampleOffset, value) == kResultTrue) 
                                    paramSignalDuration = static_cast<float>(value);
                                break;
                            case DevicesForge::PluginParamIDs::GENERATE:
                                if (paramQueue->getPoint(numPoints - 1, sampleOffset, value) == kResultTrue) 
                                    paramGenerate = static_cast<float>(value);
                                break;
                        }
                    }
                }
            }

            const bool generateOn = paramGenerate >= 0.5f;
            const auto signalType = DevicesForge::normalizedToSignalType(paramSignalType);
            const float signalDuration = DevicesForge::normalizedToDuration(paramSignalDuration);

            if (generateOn && !prevGenerateOn) 
            {
                generator.setType(signalType);
                generator.setDuration(signalDuration);

                const double sr = processSetup.sampleRate > 0.0 ? processSetup.sampleRate
                                                                : DevicesForge::SAMPLE_RATE_DEFAULT;
                const int32_t preSamples =
                    DevicesForge::secondsToSamples(sr, DevicesForge::CAPTURE_PRE_SEC);
                const float postSec = DevicesForge::capturePostDurationSeconds(signalType, signalDuration);
                const int32_t postSamples = DevicesForge::secondsToSamples(sr, postSec);

                DevicesForge::CaptureMetadata meta {};
                meta.sampleRate = sr;
                meta.signalType = signalType;
                meta.signalDurationSec = signalDuration;
                captureBuffer.trigger(preSamples, postSamples, meta);

                generator.start();
            }

            prevGenerateOn = generateOn;

            const float gainLinear = outputGainLinear(paramOutputGain);

            if (data.numInputs > 0) 
            {
                void** in = getChannelBuffersPointer(processSetup, data.inputs[0]);
                const int32 inChannels = data.inputs[0].numChannels;
                const bool inputSilent =
                    data.inputs[0].silenceFlags == getChannelMask(data.inputs[0].numChannels);

                const float* channelPtrs[16] = {};
                if (!inputSilent) 
                {
                    for (int32 c = 0; c < inChannels && c < 16; ++c)
                        channelPtrs[c] = static_cast<float*>(in[c]);
                }
                captureBuffer.push(channelPtrs, inChannels, data.numSamples);
            }

            if (data.numOutputs == 0) 
                return kResultOk;

            void** out = getChannelBuffersPointer(processSetup, data.outputs[0]);
            const int32 numOutChannels = data.outputs[0].numChannels;

            if (generator.isPlaying()) 
            {
                float* dest = static_cast<float*>(out[0]);
                generator.render(dest, data.numSamples);
                for (int32 i = 0; i < data.numSamples; ++i) 
                    dest[i] *= gainLinear;

                for (int32 channel = 1; channel < numOutChannels; ++channel) 
                    std::memcpy(out[channel], dest, static_cast<size_t>(data.numSamples) * sizeof(float));

                data.outputs[0].silenceFlags = 0;
                return kResultOk;
            }

            if (data.numInputs == 0) 
                return kResultOk;

            void** in = getChannelBuffersPointer(processSetup, data.inputs[0]);
            const int32 numChannels = std::min(data.inputs[0].numChannels, numOutChannels);

            if (data.inputs[0].silenceFlags == getChannelMask(data.inputs[0].numChannels)) 
            {
                data.outputs[0].silenceFlags = data.inputs[0].silenceFlags;
                return kResultOk;
            }

            for (int32 channel = 0; channel < numChannels; ++channel) 
            {
                float* inputBuffer = static_cast<float*>(in[channel]);
                float* outputBuffer = static_cast<float*>(out[channel]);

                for (int32 i = 0; i < data.numSamples; ++i) 
                    outputBuffer[i] = inputBuffer[i] * gainLinear;
            }

            data.outputs[0].silenceFlags = 0;
            return kResultOk;
        }

        tresult PLUGIN_API DevicesForgeProcessor::setState(IBStream* state) 
        {
            if (!state) 
                return kResultFalse;

            IBStreamer streamer(state, kLittleEndian);
            float savedMix = 1.0f;
            float savedGain = 0.0f;
            float savedSignalType = 0.0f;
            float savedDuration = DevicesForge::signalDurationNormalizedDefault();

            streamer.readFloat(savedMix);
            streamer.readFloat(savedGain);
            streamer.readFloat(savedSignalType);
            streamer.readFloat(savedDuration);

            paramMix = savedMix;
            paramOutputGain = savedGain;
            paramSignalType = savedSignalType;
            paramSignalDuration = savedDuration;
            paramGenerate = 0.0f;
            prevGenerateOn = false;

            return kResultOk;
        }

        tresult PLUGIN_API DevicesForgeProcessor::getState(IBStream* state) 
        {
            if (!state) 
                return kResultFalse;

            IBStreamer streamer(state, kLittleEndian);
            streamer.writeFloat(paramMix);
            streamer.writeFloat(paramOutputGain);
            streamer.writeFloat(paramSignalType);
            streamer.writeFloat(paramSignalDuration);

            return kResultOk;
        }

        DevicesForgeController::DevicesForgeController() {}
        DevicesForgeController::~DevicesForgeController() {}

        tresult PLUGIN_API DevicesForgeController::initialize(FUnknown* context) 
        {
            tresult result = EditController::initialize(context);
            if (result != kResultOk) 
                return result;

            parameters.addParameter(STR16("Mix"), STR16("%"), 100, 1.0,
                ParameterInfo::kCanAutomate, DevicesForge::PluginParamIDs::MIX);

            parameters.addParameter(STR16("Gain"), STR16("dB"), 100, 0.5,
                ParameterInfo::kCanAutomate, DevicesForge::PluginParamIDs::OUTPUT_GAIN);

            parameters.addParameter(STR16("IR"), nullptr, 3, 0.0,
                ParameterInfo::kCanAutomate | ParameterInfo::kIsList, DevicesForge::PluginParamIDs::IR_SELECT);

            parameters.addParameter(STR16("AI"), nullptr, 1, 0.0,
                ParameterInfo::kCanAutomate, DevicesForge::PluginParamIDs::AI_DENOISE);

            auto* signalParam = new StringListParameter(STR16("Signal"),
                DevicesForge::PluginParamIDs::SIGNAL_TYPE);
            signalParam->appendString(STR16("Sweep"));
            signalParam->appendString(STR16("Dirac"));
            signalParam->appendString(STR16("Pink"));
            signalParam->appendString(STR16("MLS"));
            parameters.addParameter(signalParam);

            parameters.addParameter(new RangeParameter(STR16("Duration"),
                DevicesForge::PluginParamIDs::SIGNAL_DURATION,
                STR16("s"),
                DevicesForge::SIGNAL_DURATION_MIN,
                DevicesForge::SIGNAL_DURATION_MAX,
                DevicesForge::SIGNAL_DURATION_DEFAULT));

            parameters.addParameter(STR16("Generate"), nullptr, 1, 0.0,
                ParameterInfo::kCanAutomate, DevicesForge::PluginParamIDs::GENERATE);

            return kResultOk;
        }

        tresult PLUGIN_API DevicesForgeController::terminate() 
        {
            return EditController::terminate();
        }

        tresult PLUGIN_API DevicesForgeController::setComponentState(IBStream* state) 
        {
            if (!state) 
                return kResultFalse;

            IBStreamer streamer(state, kLittleEndian);
            float savedMix = 1.0f;
            float savedGain = 0.0f;
            float savedSignalType = 0.0f;
            float savedDuration = DevicesForge::signalDurationNormalizedDefault();

            streamer.readFloat(savedMix);
            streamer.readFloat(savedGain);
            streamer.readFloat(savedSignalType);
            streamer.readFloat(savedDuration);

            setParamNormalized(DevicesForge::PluginParamIDs::MIX, savedMix);
            setParamNormalized(DevicesForge::PluginParamIDs::OUTPUT_GAIN, savedGain);
            setParamNormalized(DevicesForge::PluginParamIDs::SIGNAL_TYPE, savedSignalType);
            setParamNormalized(DevicesForge::PluginParamIDs::SIGNAL_DURATION, savedDuration);
            setParamNormalized(DevicesForge::PluginParamIDs::GENERATE, 0.0);

            return kResultOk;
        }
    } //  Vst
} //  Steinberg
