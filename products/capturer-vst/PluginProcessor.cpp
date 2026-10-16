#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "FileDialog.h"
#include "version.h"
#include "public.sdk/source/vst/vstaudioprocessoralgo.h"

#include "IRPostProcessor.h"
#include "SweepDeconvolver.h"
#include "IRExporter.h"
#include "WavIO.h"
#include "IRResampler.h"
#include "CaptureFile.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "base/source/fstreamer.h"
#include "base/source/fstring.h"

#include <algorithm>
#include <chrono>
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

            /*
             * readIrFile
             *
             * Pseudocodigo:
             * 1. Si es .dfcapture, leer el canal 0
             * 2. Si es WAV, leer PCM 16/24 o float 32, canal 0
             */
            bool readIrFile(const std::string& path, std::vector<float>& samples, double& sampleRate)
            {
                samples.clear();
                sampleRate = 0.0;
                if (DevicesForge::isCapturePath(path))
                {
                    DevicesForge::CaptureData capture;
                    if (!DevicesForge::CaptureFile::load(path, capture) || capture.channels.empty())
                        return false;
                    samples = capture.channels[0];
                    sampleRate = capture.sampleRate;
                    return !samples.empty();
                }

                DevicesForge::WavBuffer wav;
                if (!DevicesForge::WavIO::load(path, wav) || wav.channels.empty())
                    return false;
                samples = std::move(wav.channels[0]);
                sampleRate = wav.sampleRate;
                return !samples.empty();
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
                prevCalibrateOn = paramCalibrate >= 0.5f;
                notifyCalibrateOff = false;
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
                            case DevicesForge::PluginParamIDs::SEND:
                                if (paramQueue->getPoint(numPoints - 1, sampleOffset, value) == kResultTrue)
                                    paramSend = static_cast<float>(value);
                                break;
                            case DevicesForge::PluginParamIDs::IR_SELECT:
                                if (paramQueue->getPoint(numPoints - 1, sampleOffset, value) == kResultTrue) 
                                    paramIRSelect = static_cast<float>(value);
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
                            case DevicesForge::PluginParamIDs::CALIBRATE:
                                if (paramQueue->getPoint(numPoints - 1, sampleOffset, value) == kResultTrue)
                                    paramCalibrate = static_cast<float>(value);
                                break;
                            case DevicesForge::PluginParamIDs::CALIBRATE_SIGNAL:
                                if (paramQueue->getPoint(numPoints - 1, sampleOffset, value) == kResultTrue)
                                    paramCalibrateSignal = static_cast<float>(value);
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
            const auto calSignal = DevicesForge::normalizedToCalibrateSignal(paramCalibrateSignal);
            bool calibrateOn = paramCalibrate >= 0.5f;

            if (!generateOn && prevGenerateOn)
                sweepActive = false;

            if (generateOn && !prevGenerateOn) 
            {
                // Generate manda: apaga Cal para no mezclar tono de prueba con la toma.
                if (calibrateOn)
                {
                    calibrateOn = false;
                    paramCalibrate = 0.0f;
                    notifyCalibrateOff = true;
                    generator.stop();
                }

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

            // Cal no captura: solo manda señal para ajustar Gain / InPeak del loop.
            if (sweepActive && calibrateOn)
            {
                calibrateOn = false;
                paramCalibrate = 0.0f;
                notifyCalibrateOff = true;
            }

            if (!sweepActive)
            {
                const bool wantSweepLoop = calibrateOn &&
                    calSignal == DevicesForge::CalibrateSignal::Sweep;
                if (wantSweepLoop)
                {
                    if (!generator.isLooping() ||
                        std::abs(generator.getDuration() - signalDuration) > 1.0e-4f)
                    {
                        generator.setType(DevicesForge::SignalType::SineSweep);
                        generator.setDuration(signalDuration);
                        generator.startLoop();
                    }
                }
                else
                {
                    if (generator.isLooping())
                        generator.stop();
                    if (calibrateOn && !prevCalibrateOn)
                        generator.resetTonePhase();
                }
            }
            prevCalibrateOn = calibrateOn;

            const bool exportOn = paramExport >= 0.5f;
            if (exportOn && !prevExportOn)
                exportCurrentIR(false);
            prevExportOn = exportOn;

            const float gainLinear = outputGainLinear(paramOutputGain);
            const float sendLinear = outputGainLinear(paramSend);

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

                if (notifyCalibrateOff)
                {
                    notifyCalibrateOff = false;
                    int32 queueIndexCal = 0;
                    if (IParamValueQueue* queue = outParams->addParameterData(
                            DevicesForge::PluginParamIDs::CALIBRATE, queueIndexCal))
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

            if (generator.isPlaying() || calibrateOn) 
            {
                float* dest = static_cast<float*>(out[0]);
                if (generator.isPlaying())
                    generator.render(dest, data.numSamples);
                else
                    generator.renderTone(dest, data.numSamples, DevicesForge::CALIBRATE_TONE_HZ);

                for (int32 i = 0; i < data.numSamples; ++i) 
                    dest[i] *= sendLinear;

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
            {
                std::lock_guard<std::mutex> lock(irMutex);
                convolver.setMix(paramMix);
                const int32_t irIndex = static_cast<int32_t>(
                    paramIRSelect * static_cast<float>(DevicesForge::NUM_CAPTURE_LEVELS - 1) + 0.5f);
                convolver.process(outPtrs, numChannels, data.numSamples, irIndex);
            }

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
            paramCalibrate = 0.0f;
            prevCalibrateOn = false;

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
                {
                    std::lock_guard<std::mutex> lock(irMutex);
                    convolver.getIRManager().setLevelIR(0, ir, 0.0f);
                }
            }

            float savedSend = 0.5f;
            if (streamer.readFloat(savedSend))
                paramSend = savedSend;

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

            std::vector<float> irCopy;
            {
                std::lock_guard<std::mutex> lock(irMutex);
                const DevicesForge::IRManager& irManager = convolver.getIRManager();
                const float* ir = irManager.getIR(0);
                const int32_t irLength = irManager.getIRLength(0);
                if (ir != nullptr && irLength > 0 &&
                    irLength <= DevicesForge::IR_STATE_MAX_SAMPLES)
                    irCopy.assign(ir, ir + irLength);
            }

            const bool hasIR = !irCopy.empty();
            streamer.writeInt32(hasIR ? static_cast<int32>(irCopy.size()) : 0);
            if (hasIR)
                streamer.writeFloatArray(irCopy.data(), static_cast<int32>(irCopy.size()));
            streamer.writeFloat(paramSend);

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

            {
                std::lock_guard<std::mutex> lock(irMutex);
                convolver.getIRManager().clear();
                convolver.getIRManager().setLevelIR(0, ir, 0.0f);
            }

            DevicesForge::IRExporter::exportAllFormats(sessionDir, ir.data(),
                                                       static_cast<int32_t>(ir.size()), sampleRate);
            log << "ir_exported=1\n"
                << "ir_samples=" << ir.size() << "\n";
            DevicesForge::IRExporter::writeTextFile(
                DevicesForge::IRExporter::sessionDirectory(sessionDir) + "/capture_log.txt", log.str());
        }

        void DevicesForgeProcessor::saveIRToPath(const std::string& path)
        {
            std::vector<float> copy;
            {
                std::lock_guard<std::mutex> lock(irMutex);
                const float* ir = convolver.getIRManager().getIR(0);
                const int32_t length = convolver.getIRManager().getIRLength(0);
                if (ir && length > 0)
                    copy.assign(ir, ir + length);
            }
            if (copy.empty())
                return;

            const double sampleRate = processSetup.sampleRate > 0.0 ? processSetup.sampleRate
                                                                    : DevicesForge::SAMPLE_RATE_DEFAULT;
            DevicesForge::IRExporter::saveWavFloat32(path, copy.data(),
                                                     static_cast<int32_t>(copy.size()), sampleRate);
        }

        void DevicesForgeProcessor::loadIRFromPath(const std::string& path)
        {
            std::vector<float> ir;
            double fileRate = 0.0;
            if (!readIrFile(path, ir, fileRate) || ir.empty())
                return;

            /* Llevo la IR a la tasa del proyecto */
            const double projectRate = processSetup.sampleRate > 0.0 ? processSetup.sampleRate
                                                                     : DevicesForge::SAMPLE_RATE_DEFAULT;
            std::vector<float> resampled;
            if (!DevicesForge::IRResampler::resample(ir.data(), static_cast<int32_t>(ir.size()),
                                                     fileRate, resampled, projectRate))
                return;
            if (static_cast<int32_t>(resampled.size()) > DevicesForge::IR_STATE_MAX_SAMPLES)
                resampled.resize(static_cast<size_t>(DevicesForge::IR_STATE_MAX_SAMPLES));

            std::lock_guard<std::mutex> lock(irMutex);
            convolver.getIRManager().clear();
            convolver.getIRManager().setLevelIR(0, resampled, 0.0f);
        }

        tresult PLUGIN_API DevicesForgeProcessor::notify(IMessage* message)
        {
            if (!message)
                return AudioEffect::notify(message);

            const FIDString id = message->getMessageID();
            TChar pathW[1024] = {};
            if (message->getAttributes()->getString("Path", pathW, sizeof(pathW)) != kResultOk)
                return AudioEffect::notify(message);

            String pathStr(pathW);
            const std::string path = pathStr.text8() ? std::string(pathStr.text8()) : std::string();
            if (path.empty())
                return kResultFalse;

            if (FIDStringsEqual(id, "DFSaveIR"))
            {
                saveIRToPath(path);
                return kResultOk;
            }
            if (FIDStringsEqual(id, "DFLoadIR"))
            {
                loadIRFromPath(path);
                return kResultOk;
            }

            return AudioEffect::notify(message);
        }

        void DevicesForgeProcessor::exportCurrentIR(bool allFormats)
        {
            std::vector<float> copy;
            {
                std::lock_guard<std::mutex> lock(irMutex);
                const float* ir = convolver.getIRManager().getIR(0);
                const int32_t length = convolver.getIRManager().getIRLength(0);
                if (ir && length > 0)
                    copy.assign(ir, ir + length);
            }
            if (copy.empty())
                return;
            const float* ir = copy.data();
            const int32_t length = static_cast<int32_t>(copy.size());

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

        IPlugView* PLUGIN_API DevicesForgeController::createView(FIDString name)
        {
            if (FIDStringsEqual(name, ViewType::kEditor))
                return new DevicesForgeEditor(this);
            return nullptr;
        }

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

            /* 10000 pasos = 0.01 %. */
            parameters.addParameter(STR16("Mix"), STR16("%"), 10000, 1.0,
                ParameterInfo::kCanAutomate, DevicesForge::PluginParamIDs::MIX);

            parameters.addParameter(STR16("Send"), STR16("dB"), 100, 0.5,
                ParameterInfo::kCanAutomate, DevicesForge::PluginParamIDs::SEND);

            parameters.addParameter(STR16("Gain"), STR16("dB"), 100, 0.5,
                ParameterInfo::kCanAutomate, DevicesForge::PluginParamIDs::OUTPUT_GAIN);

            parameters.addParameter(STR16("IR"), nullptr, 3, 0.0,
                ParameterInfo::kCanAutomate | ParameterInfo::kIsList, DevicesForge::PluginParamIDs::IR_SELECT);

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

            parameters.addParameter(STR16("SaveIR"), nullptr, 1, 0.0,
                ParameterInfo::kCanAutomate, DevicesForge::PluginParamIDs::SAVE_IR);

            parameters.addParameter(STR16("LoadIR"), nullptr, 1, 0.0,
                ParameterInfo::kCanAutomate, DevicesForge::PluginParamIDs::LOAD_IR);

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

            int32 irLength = 0;
            if (streamer.readInt32(irLength) && irLength > 0 &&
                irLength <= DevicesForge::IR_STATE_MAX_SAMPLES)
            {
                float dummy = 0.0f;
                for (int32 i = 0; i < irLength; ++i)
                    streamer.readFloat(dummy);
            }

            float savedSend = 0.5f;
            if (streamer.readFloat(savedSend))
                setParamNormalized(DevicesForge::PluginParamIDs::SEND, savedSend);

            return kResultOk;
        }

        void DevicesForgeController::sendIRPathMessage(const char* messageId, const std::string& path)
        {
            IPtr<IMessage> msg = owned(allocateMessage());
            if (!msg)
                return;
            msg->setMessageID(messageId);
            String pathStr(path.c_str());
            msg->getAttributes()->setString("Path", pathStr);
            sendMessage(msg);
        }

        void DevicesForgeController::runFileDialogOnce(ParamID tag)
        {
            if (fileDialogBusy)
                return;

            const double now = std::chrono::duration<double>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            if (now < fileDialogIgnoreUntil)
                return;

            fileDialogBusy = true;
            std::string path;
            if (tag == DevicesForge::PluginParamIDs::SAVE_IR)
            {
                if (DevicesForge::chooseSaveIRPath(path))
                    sendIRPathMessage("DFSaveIR", path);
            }
            else if (tag == DevicesForge::PluginParamIDs::LOAD_IR)
            {
                if (DevicesForge::chooseOpenIRPath(path))
                    sendIRPathMessage("DFLoadIR", path);
            }
            fileDialogBusy = false;
            fileDialogIgnoreUntil = std::chrono::duration<double>(
                std::chrono::steady_clock::now().time_since_epoch()).count() + 0.75;
            EditController::setParamNormalized(tag, 0.0);
        }

        tresult PLUGIN_API DevicesForgeController::setParamNormalized(ParamID tag, ParamValue value)
        {
            const tresult result = EditController::setParamNormalized(tag, value);
            if (result != kResultOk)
                return result;

            if (value < 0.5)
                return result;

            if (tag == DevicesForge::PluginParamIDs::SAVE_IR ||
                tag == DevicesForge::PluginParamIDs::LOAD_IR)
                runFileDialogOnce(tag);

            return result;
        }
    } //  Vst
} //  Steinberg
