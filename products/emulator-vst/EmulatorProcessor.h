#pragma once

#include "public.sdk/source/vst/vstaudioeffect.h"

#include "DynamicConvolver.h"
#include "VuMeter.h"

#include <mutex>
#include <string>
#include <vector>

namespace DevicesForge
{
	namespace Emulator
	{

		/**
		 * EmulatorProcessor
		 *
		 * Cadena: ganancia de entrada -> VU de entrada -> convolucion con la
		 * captura (mezcla seco/humedo) -> ganancia de salida -> VU de salida.
		 * Bypass deja la mezcla en seco con ganancias unitarias, manteniendo
		 * la latencia para que el host no corra la pista al activarlo.
		 */
		class EmulatorProcessor : public Steinberg::Vst::AudioEffect
		{
		public:
			EmulatorProcessor();

			static Steinberg::FUnknown* createInstance(void*)
			{
				return static_cast<Steinberg::Vst::IAudioProcessor*>(new EmulatorProcessor());
			}

			Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
			Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) SMTG_OVERRIDE;
			Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) SMTG_OVERRIDE;
			Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
			Steinberg::uint32 PLUGIN_API getLatencySamples() SMTG_OVERRIDE;
			Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
			Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;
			Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) SMTG_OVERRIDE;

		private:
			void readParameterChanges(Steinberg::Vst::IParameterChanges* changes);
			void writeMeters(Steinberg::Vst::IParameterChanges* changes);
			void loadCapture(const std::string& path);
			void publishCaptureInfo(const std::string& name, const std::string& author);
			void delayWithoutCapture(float** buffers, Steinberg::int32 channels, Steinberg::int32 numSamples);

			DynamicConvolver convolver;
			/** Sin captura la senal se retrasa igual: la latencia informada no cambia. */
			std::vector<float> idleDelay[NUM_CHANNELS];
			Steinberg::int32 idlePos { 0 };
			VuMeter vuIn;
			VuMeter vuOut;

			float paramInputGain { 0.5f };
			float paramMix { 1.0f };
			float paramOutputGain { 0.5f };
			bool bypass { false };

			std::mutex captureMutex;
			std::string captureName;
			std::string captureAuthor;
		};

	} // namespace Emulator
} // namespace DevicesForge
