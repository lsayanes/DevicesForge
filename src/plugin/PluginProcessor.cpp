#include "PluginProcessor.h"
#include "version.h"
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
                convolver.prepare(processSetup.sampleRate, DevicesForge::CONV_FFT_SIZE);
                generator.prepare(processSetup.sampleRate);
                captureBuffer.prepare(processSetup.sampleRate, DevicesForge::NUM_CHANNELS);
                prevGenerateOn = paramGenerate >= 0.5f;
                sweepActive = false;
                notifyGenerateOff = false;
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
                            case DevicesForge::PluginParamIDs::CLEAR_LATEST:
                                if (paramQueue->getPoint(numPoints - 1, sampleOffset, value) == kResultTrue) 
                                    paramClearLatest = static_cast<float>(value);
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

            if (!generateOn && prevGenerateOn)
                sweepActive = false;

            if (generateOn && !prevGenerateOn) 
            {
                if (paramClearLatest >= 0.5f)
                    DevicesForge::IRExporter::clearSessionDirectory("latest");

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
                sweepActive = true;
            }

            prevGenerateOn = generateOn;

            if (sweepActive && !generator.isPlaying())
            {
                sweepActive = false;
                paramGenerate = 0.0f;
                prevGenerateOn = false;
                notifyGenerateOff = true;
            }

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
                    // Peak-hold con decaimiento: sin esto el valor parpadea por bloque
                    // y es imposible leerlo en el panel del host.
                    meterPeak = std::max(captureBuffer.getInputPeak(),
                                         meterPeak * DevicesForge::METER_DECAY_PER_BLOCK);

                    const float peakDb = DevicesForge::linearToDbfs(meterPeak);
                    const ParamValue normalized =
                        std::clamp((peakDb - DevicesForge::METER_FLOOR_DB) /
                                       (0.0 - DevicesForge::METER_FLOOR_DB),
                                   0.0, 1.0);

                    int32 pointIndex = 0;
                    queue->addPoint(0, normalized, pointIndex);
                }

                // IRLen = 0 significa que no hay IR en memoria: el plugin solo pasa
                // la señal. La IR no se guarda en el estado del proyecto todavía.
                int32 queueIndexIR = 0;
                if (IParamValueQueue* queue = outParams->addParameterData(
                        DevicesForge::PluginParamIDs::IR_LENGTH_MS, queueIndexIR))
                {
                    const double sr = processSetup.sampleRate > 0.0 ? processSetup.sampleRate
                                                                    : DevicesForge::SAMPLE_RATE_DEFAULT;
                    const double irMs =
                        static_cast<double>(convolver.getIRManager().getIRLength(0)) * 1000.0 / sr;
                    const ParamValue normalized =
                        std::clamp(irMs / DevicesForge::IR_LENGTH_METER_MAX_MS, 0.0, 1.0);

                    int32 pointIndex = 0;
                    queue->addPoint(0, normalized, pointIndex);
                }

                if (notifyGenerateOff)
                {
                    notifyGenerateOff = false;
                    int32 queueIndexGen = 0;
                    if (IParamValueQueue* queue = outParams->addParameterData(
                            DevicesForge::PluginParamIDs::GENERATE, queueIndexGen))
                    {
                        int32 pointIndex = 0;
                        queue->addPoint(0, 0.0, pointIndex);
                    }
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
            const int32 numChannels =
                std::min({data.inputs[0].numChannels, numOutChannels,
                          static_cast<int32>(DevicesForge::NUM_CHANNELS)});

            // Copiar entrada a salida; el convolver procesa in-place sobre la salida.
            float* outPtrs[DevicesForge::NUM_CHANNELS] = {};
            for (int32 channel = 0; channel < numChannels; ++channel) 
            {
                float* inputBuffer = static_cast<float*>(in[channel]);
                float* outputBuffer = static_cast<float*>(out[channel]);
                if (outputBuffer != inputBuffer)
                    std::memcpy(outputBuffer, inputBuffer,
                                static_cast<size_t>(data.numSamples) * sizeof(float));
                outPtrs[channel] = outputBuffer;
            }

            // Emulación: convolución con la IR seleccionada (si hay IR en memoria).
            // Sin IR el buffer queda intacto → passthrough.
            convolver.setMix(paramMix);
            const int32_t irIndex = static_cast<int32_t>(
                paramIRSelect * static_cast<float>(DevicesForge::NUM_CAPTURE_LEVELS - 1) + 0.5f);
            convolver.process(outPtrs, numChannels, data.numSamples, irIndex);

            for (int32 channel = 0; channel < numChannels; ++channel) 
            {
                float* outputBuffer = outPtrs[channel];
                for (int32 i = 0; i < data.numSamples; ++i) 
                    outputBuffer[i] *= gainLinear;
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
            float savedGain = 0.5f;
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

            // La IR se guarda junto a los parámetros: sin esto habría que
            // recapturar el dispositivo cada vez que se abre el proyecto.
            // Los estados de versiones previas terminan aquí y la lectura
            // falla sin efecto, dejando el convolver vacío.
            int32 irLength = 0;
            if (streamer.readInt32(irLength) && irLength > 0 &&
                irLength <= DevicesForge::IR_STATE_MAX_SAMPLES)
            {
                std::vector<float> ir(static_cast<size_t>(irLength));
                if (streamer.readFloatArray(ir.data(), irLength))
                    convolver.getIRManager().setLevelIR(0, ir, 0.0f);
            }

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

            const DevicesForge::IRManager& irManager = convolver.getIRManager();
            const float* ir = irManager.getIR(0);
            const int32_t irLength = irManager.getIRLength(0);
            const bool hasIR = ir != nullptr && irLength > 0 &&
                               irLength <= DevicesForge::IR_STATE_MAX_SAMPLES;

            streamer.writeInt32(hasIR ? irLength : 0);
            if (hasIR)
                streamer.writeFloatArray(ir, irLength);

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
            int32_t clippedSamples = 0;
            for (int32_t i = 0; i < recordedLength; ++i)
            {
                const float magnitude = std::abs(recorded[i]);
                capturePeak = std::max(capturePeak, magnitude);
                if (magnitude >= DevicesForge::CAPTURE_CLIP_THRESHOLD)
                    ++clippedSamples;
            }

            // El pre-trigger (antes del sweep) es ruido de fondo puro: sirve de
            // referencia para estimar la relacion senal/ruido de la toma.
            const int32_t preSamples =
                std::min(captureBuffer.getMetadata().preSamples, recordedLength);
            auto rmsOf = [recorded](int32_t from, int32_t to) -> float {
                if (to <= from)
                    return 0.0f;
                double sum = 0.0;
                for (int32_t i = from; i < to; ++i)
                    sum += static_cast<double>(recorded[i]) * recorded[i];
                return static_cast<float>(std::sqrt(sum / static_cast<double>(to - from)));
            };

            const float noiseRms = rmsOf(0, preSamples);
            const float signalRms = rmsOf(preSamples, recordedLength);
            const float snrDb = (noiseRms > 0.0f && signalRms > 0.0f)
                                    ? 20.0f * std::log10(signalRms / noiseRms)
                                    : 0.0f;

            const float lengthMs =
                (sampleRate > 0.0) ? static_cast<float>(recordedLength) * 1000.0f / static_cast<float>(sampleRate)
                                   : 0.0f;

            std::ostringstream log;
            log << "capture_samples=" << recordedLength << "\n"
                << "capture_ms=" << lengthMs << "\n"
                << "sample_rate=" << sampleRate << "\n"
                << "peak=" << capturePeak << "\n"
                << "peak_dbfs=" << DevicesForge::linearToDbfs(capturePeak) << "\n"
                << "noise_floor_dbfs=" << DevicesForge::linearToDbfs(noiseRms) << "\n"
                << "signal_rms_dbfs=" << DevicesForge::linearToDbfs(signalRms) << "\n"
                << "snr_db=" << snrDb << "\n"
                << "clipped_samples=" << clippedSamples << "\n"
                << "min_peak=" << DevicesForge::CAPTURE_MIN_PEAK << "\n";

            const char* quality = "ok";
            if (capturePeak < DevicesForge::CAPTURE_MIN_PEAK)
                quality = "silence_check_routing";
            else if (clippedSamples > 0)
                quality = "clipping_lower_gain";
            else if (capturePeak < DevicesForge::CAPTURE_GOOD_PEAK_MIN)
                quality = "level_low_raise_gain";
            else if (snrDb < DevicesForge::CAPTURE_GOOD_SNR_DB)
                quality = "low_snr_noisy_take";
            log << "quality=" << quality << "\n";

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
                                                            referenceLength, ir, DevicesForge::FFT_SIZE,
                                                            sampleRate))
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

            // Lista de un solo valor: el host muestra el string, no un número.
            auto* versionParam = new StringListParameter(STR16("Version"),
                DevicesForge::PluginParamIDs::VERSION_LABEL, nullptr,
                ParameterInfo::kIsReadOnly);
            versionParam->appendString(STR16(FULL_VERSION_STR));
            parameters.addParameter(versionParam);

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

            parameters.addParameter(STR16("ClrLatest"), nullptr, 1, 1.0,
                ParameterInfo::kCanAutomate, DevicesForge::PluginParamIDs::CLEAR_LATEST);

            auto* exportFormatParam = new StringListParameter(STR16("ExportFmt"),
                DevicesForge::PluginParamIDs::EXPORT_FORMAT);
            exportFormatParam->appendString(STR16("WAV24"));
            exportFormatParam->appendString(STR16("WAV32f"));
            exportFormatParam->appendString(STR16("AIFF96"));
            exportFormatParam->appendString(STR16("DFIR"));
            parameters.addParameter(exportFormatParam);

            parameters.addParameter(STR16("Export"), nullptr, 1, 0.0,
                ParameterInfo::kCanAutomate, DevicesForge::PluginParamIDs::EXPORT);

            // Medidor de entrada en dBFS: legible antes de disparar Generate.
            parameters.addParameter(new RangeParameter(STR16("InPeak"),
                DevicesForge::PluginParamIDs::INPUT_PEAK,
                STR16("dB"),
                DevicesForge::METER_FLOOR_DB, 0.0,
                DevicesForge::METER_FLOOR_DB,
                0, ParameterInfo::kIsReadOnly));

            // 0 ms = no hay IR capturada en memoria (el plugin solo pasa señal).
            parameters.addParameter(new RangeParameter(STR16("IRLen"),
                DevicesForge::PluginParamIDs::IR_LENGTH_MS,
                STR16("ms"),
                0.0, DevicesForge::IR_LENGTH_METER_MAX_MS, 0.0,
                0, ParameterInfo::kIsReadOnly));

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
            float savedGain = 0.5f;
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
