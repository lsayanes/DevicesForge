#include "PluginProcessor.h"
#include "public.sdk/source/vst/vstaudioprocessoralgo.h"

#include "../dsp/IRPostProcessor.h"
#include "../dsp/SweepDeconvolver.h"
#include "../dsp/IRExporter.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "base/source/fstreamer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
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
                prevCaptureComplete = false;
                prevExportOn = paramExport >= 0.5f;
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
#if defined(HAS_ONNX_RUNTIME)
                            case DevicesForge::PluginParamIDs::AI_DENOISE:
                                if (paramQueue->getPoint(numPoints - 1, sampleOffset, value) == kResultTrue) 
                                    paramAIDenoise = static_cast<float>(value);
                                break;
#endif
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
                            case DevicesForge::PluginParamIDs::EXPORT_FORMAT:
                                if (paramQueue->getPoint(numPoints - 1, sampleOffset, value) == kResultTrue) 
                                    paramExportFormat = static_cast<float>(value);
                                break;
                            case DevicesForge::PluginParamIDs::EXPORT:
                                if (paramQueue->getPoint(numPoints - 1, sampleOffset, value) == kResultTrue) 
                                    paramExport = static_cast<float>(value);
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

            const bool exportOn = paramExport >= 0.5f;
            if (exportOn && !prevExportOn)
                exportCurrentIR(false);
            prevExportOn = exportOn;

            const float gainLinear = outputGainLinear(paramOutputGain);

            if (data.numInputs > 0) 
            {
                void** in = getChannelBuffersPointer(processSetup, data.inputs[0]);
                const int32 inChannels = data.inputs[0].numChannels;
                const float* channelPtrs[16] = {};
                for (int32 c = 0; c < inChannels && c < 16; ++c)
                    channelPtrs[c] = static_cast<float*>(in[c]);
                captureBuffer.push(channelPtrs, inChannels, data.numSamples);
            }

            if (IParameterChanges* outParams = data.outputParameterChanges)
            {
                int32 queueIndex = 0;
                if (IParamValueQueue* queue =
                        outParams->addParameterData(DevicesForge::PluginParamIDs::INPUT_PEAK, queueIndex))
                {
                    int32 pointIndex = 0;
                    const ParamValue peak =
                        std::min(1.0, static_cast<double>(captureBuffer.getInputPeak()));
                    queue->addPoint(0, peak, pointIndex);
                }
            }

            const bool captureComplete = captureBuffer.isComplete();
            if (captureComplete && !prevCaptureComplete)
                processCompletedCapture();
            prevCaptureComplete = captureComplete;

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

        void DevicesForgeProcessor::processCompletedCapture()
        {
            const float* recorded = captureBuffer.getCapturedMono();
            const int32_t recordedLength = captureBuffer.getCapturedLength();
            const float* reference = generator.getReference();
            const int32_t referenceLength = generator.getReferenceLength();

            if (!recorded || recordedLength <= 0 || !reference || referenceLength <= 0)
                return;

            const double sampleRate = captureBuffer.getMetadata().sampleRate > 0.0
                                          ? captureBuffer.getMetadata().sampleRate
                                          : DevicesForge::SAMPLE_RATE_DEFAULT;
            const std::string sessionDir = "latest";
            const std::string capturePath = DevicesForge::IRExporter::defaultExportDirectory() + "/" +
                                            sessionDir + "/capture_raw.float.wav";
            DevicesForge::IRExporter::exportCaptureRawWavFloat(capturePath, recorded, recordedLength,
                                                                 sampleRate);

            float capturePeak = 0.0f;
            for (int32_t i = 0; i < recordedLength; ++i)
                capturePeak = std::max(capturePeak, std::abs(recorded[i]));

            const float lengthMs =
                (sampleRate > 0.0) ? static_cast<float>(recordedLength) * 1000.0f / static_cast<float>(sampleRate)
                                   : 0.0f;

            std::ostringstream log;
            log << "capture_samples=" << recordedLength << "\n"
                << "capture_ms=" << lengthMs << "\n"
                << "sample_rate=" << sampleRate << "\n"
                << "peak=" << capturePeak << "\n"
                << "min_peak=" << DevicesForge::CAPTURE_MIN_PEAK << "\n";

            if (capturePeak < DevicesForge::CAPTURE_MIN_PEAK)
            {
                DevicesForge::IRExporter::removeExportedIRFiles(sessionDir);
                log << "ir_exported=0\n"
                    << "reason=capture_too_quiet_plugin_input_is_silence\n"
                    << "note=Duration is pre+post (100ms + max(1s, duration+0.25s)), not the Generate duration alone.\n";
                DevicesForge::IRExporter::writeTextFile(
                    DevicesForge::IRExporter::sessionDirectory(sessionDir) + "/capture_log.txt", log.str());
                return;
            }

            std::vector<float> ir;
            if (!DevicesForge::SweepDeconvolver::deconvolve(recorded, recordedLength, reference,
                                                            referenceLength, ir, DevicesForge::FFT_SIZE))
                return;

            DevicesForge::IRPostProcessSettings settings;
            settings.windowType = DevicesForge::IRWindowType::Hanning;
            DevicesForge::IRPostProcessor::process(ir, settings);

            if (ir.empty())
                return;

            convolver.getIRManager().clear();
            convolver.getIRManager().setLevelIR(0, ir, 0.0f);

            DevicesForge::IRExporter::exportAllFormats(sessionDir, ir.data(),
                                                       static_cast<int32_t>(ir.size()), sampleRate);
            log << "ir_exported=1\n"
                << "ir_samples=" << ir.size() << "\n";
            DevicesForge::IRExporter::writeTextFile(
                DevicesForge::IRExporter::sessionDirectory(sessionDir) + "/capture_log.txt", log.str());
        }

        void DevicesForgeProcessor::exportCurrentIR(bool allFormats)
        {
            const float* ir = convolver.getIRManager().getIR(0);
            const int32_t length = convolver.getIRManager().getIRLength(0);
            if (!ir || length <= 0)
                return;

            const double sampleRate = processSetup.sampleRate > 0.0 ? processSetup.sampleRate
                                                                    : DevicesForge::SAMPLE_RATE_DEFAULT;
            const std::string sessionDir = "latest";

            if (allFormats)
            {
                DevicesForge::IRExporter::exportAllFormats(sessionDir, ir, length, sampleRate);
                return;
            }

            DevicesForge::IRExportRequest request;
            request.format = DevicesForge::normalizedToExportFormat(paramExportFormat);
            request.samples = ir;
            request.numSamples = length;
            request.sampleRate = sampleRate;
            request.filePath = DevicesForge::IRExporter::defaultExportDirectory() + "/" + sessionDir +
                               "/IR" + DevicesForge::IRExporter::formatExtension(request.format);
            DevicesForge::IRExporter::exportBuffer(request);
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

#if defined(HAS_ONNX_RUNTIME)
            parameters.addParameter(STR16("AI"), nullptr, 1, 0.0,
                ParameterInfo::kCanAutomate, DevicesForge::PluginParamIDs::AI_DENOISE);
#endif

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

            auto* exportFormatParam = new StringListParameter(STR16("ExportFmt"),
                DevicesForge::PluginParamIDs::EXPORT_FORMAT);
            exportFormatParam->appendString(STR16("WAV24"));
            exportFormatParam->appendString(STR16("WAV32f"));
            exportFormatParam->appendString(STR16("AIFF96"));
            exportFormatParam->appendString(STR16("DFIR"));
            parameters.addParameter(exportFormatParam);

            parameters.addParameter(STR16("Export"), nullptr, 1, 0.0,
                ParameterInfo::kCanAutomate, DevicesForge::PluginParamIDs::EXPORT);

            parameters.addParameter(STR16("InPeak"), nullptr, 0, 0.0,
                ParameterInfo::kIsReadOnly, DevicesForge::PluginParamIDs::INPUT_PEAK);

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
