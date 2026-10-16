#pragma once

#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

#include "PluginCIDs.h"
#include "DevicesForge.h"
#include "DynamicConvolver.h"
#include "IRManager.h"
#include "SignalGenerator.h"
#include "RingCaptureBuffer.h"

#include <mutex>
#include <string>

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
            tresult PLUGIN_API notify(IMessage* message) SMTG_OVERRIDE;

        private:
            DevicesForge::DynamicConvolver convolver;
            DevicesForge::IRManager irManager;
            DevicesForge::SignalGenerator generator;
            DevicesForge::RingCaptureBuffer captureBuffer;

            float paramMix = 1.0f;
            // 0.5 normalizado = 0 dB. Debe coincidir con el default del controller.
            float paramOutputGain = 0.5f;
            float paramSend = 0.5f;
            float paramIRSelect = 0.0f;
            float paramSignalType = 0.0f;
            float paramSignalDuration = DevicesForge::signalDurationNormalizedDefault();
            float paramGenerate = 0.0f;
            bool prevGenerateOn = false;
            bool sweepActive = false;
            bool prevCaptureComplete = false;
            float paramClearLatest = 1.0f;
            bool notifyGenerateOff = false;
            float paramCalibrate = 0.0f;
            float paramCalibrateSignal = 0.0f;
            bool prevCalibrateOn = false;
            bool notifyCalibrateOff = false;
            float paramExportFormat = 0.0f;
            float paramExport = 0.0f;
            bool prevExportOn = false;

            void processCompletedCapture();
            void exportCurrentIR(bool allFormats);
            void saveIRToPath(const std::string& path);
            void loadIRFromPath(const std::string& path);
            void publishLoadedIrName(const std::string& name);

            std::mutex irMutex;
            std::string loadedIrFileName;

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
            tresult PLUGIN_API setParamNormalized(ParamID tag, ParamValue value) SMTG_OVERRIDE;
            tresult PLUGIN_API notify(IMessage* message) SMTG_OVERRIDE;
            IPlugView* PLUGIN_API createView(FIDString name) SMTG_OVERRIDE;

            std::string loadedIrName() const;

        private:
            void sendIRPathMessage(const char* messageId, const std::string& path);
            void runFileDialogOnce(ParamID tag);
            void setLoadedIrName(const std::string& name);

            bool fileDialogBusy { false };
            double fileDialogIgnoreUntil { 0.0 };
            mutable std::mutex irNameMutex;
            std::string loadedIrFileName;
        };

    } //  Vst
} //  Steinberg
