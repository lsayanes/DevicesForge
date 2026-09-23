#pragma once

#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

#include "PluginCIDs.h"
#include "DevicesForge.h"
#include "../dsp/DynamicConvolver.h"
#include "../dsp/IRManager.h"
#include "../dsp/SignalGenerator.h"
#include "../dsp/RingCaptureBuffer.h"

#ifdef HAS_ONNX_RUNTIME
#include "../ai/ONNXInference.h"
#endif

namespace Steinberg 
{
    namespace Vst 
    {

        class DevicesForgeProcessor : public AudioEffect 
        {
        public:
            DevicesForgeProcessor();
            ~DevicesForgeProcessor() override;

            static FUnknown* createInstance(void*) { return (IAudioProcessor*)new DevicesForgeProcessor(); }

            // AudioEffect overrides
            tresult PLUGIN_API initialize(FUnknown* context) SMTG_OVERRIDE;
            tresult PLUGIN_API terminate() SMTG_OVERRIDE;
            tresult PLUGIN_API setActive(TBool state) SMTG_OVERRIDE;
            tresult PLUGIN_API process(ProcessData& data) SMTG_OVERRIDE;
            tresult PLUGIN_API setState(IBStream* state) SMTG_OVERRIDE;
            tresult PLUGIN_API getState(IBStream* state) SMTG_OVERRIDE;

        private:
            DevicesForge::DynamicConvolver convolver;
            DevicesForge::IRManager irManager;
            DevicesForge::SignalGenerator generator;
            DevicesForge::RingCaptureBuffer captureBuffer;
            
            #ifdef HAS_ONNX_RUNTIME
            DevicesForge::ONNXInference aiEngine;
            #endif

            float paramMix = 1.0f;
            // 0.5 normalizado = 0 dB. Debe coincidir con el default del controller.
            float paramOutputGain = 0.5f;
            float paramIRSelect = 0.0f;
#if defined(HAS_ONNX_RUNTIME)
            float paramAIDenoise = 0.0f;
#endif
            float paramSignalType = 0.0f;
            float paramSignalDuration = DevicesForge::signalDurationNormalizedDefault();
            float paramGenerate = 0.0f;
            bool prevGenerateOn = false;
            bool sweepActive = false;
            bool prevCaptureComplete = false;
            float paramClearLatest = 1.0f;
            bool notifyGenerateOff = false;
            float paramExportFormat = 0.0f;
            float paramExport = 0.0f;
            bool prevExportOn = false;

            void processCompletedCapture();
            void exportCurrentIR(bool allFormats);

            float meterPeak = 0.0f;   // peak-hold con decaimiento para InPeak
        };

        class DevicesForgeController : public EditController 
        {
        public:
            DevicesForgeController();
            ~DevicesForgeController() override;

            static FUnknown* createInstance(void*) { return (IEditController*)new DevicesForgeController(); }

            // EditController overrides
            tresult PLUGIN_API initialize(FUnknown* context) SMTG_OVERRIDE;
            tresult PLUGIN_API terminate() SMTG_OVERRIDE;
            tresult PLUGIN_API setComponentState(IBStream* state) SMTG_OVERRIDE;
        };

    } //  Vst
} //  Steinberg
