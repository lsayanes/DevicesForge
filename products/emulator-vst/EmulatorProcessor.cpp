#include "EmulatorProcessor.h"
#include "EmulatorIDs.h"
#include "EmulatorState.h"

#include "CaptureFile.h"
#include "DFConstants.h"
#include "IRResampler.h"
#include "WavIO.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/vst/vstaudioprocessoralgo.h"

#include <algorithm>
#include <cstring>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace DevicesForge
{
	namespace Emulator
	{

		namespace
		{
			/*
			 * readCaptureFile
			 *
			 * Pseudocodigo:
			 * 1. Si es .dfcapture, tomar el canal 0, el nombre y el autor
			 * 2. Si es WAV, tomar el canal 0 y el nombre del archivo
			 */
			bool readCaptureFile(const std::string& path, std::vector<float>& samples, double& sampleRate,
								 std::string& name, std::string& author)
			{
				author.clear();
				/* Leo la captura publicable con su nombre y su autor */
				if (isCapturePath(path))
				{
					CaptureData capture;
					if (!CaptureFile::load(path, capture) || capture.channels.empty())
						return false;
					samples = std::move(capture.channels[0]);
					sampleRate = capture.sampleRate;
					name = capture.name;
					author = capture.author;
				}
				else
				{
					/* Leo un WAV comun (IR de terceros) */
					WavBuffer wav;
					if (!WavIO::load(path, wav) || wav.channels.empty())
						return false;
					samples = std::move(wav.channels[0]);
					sampleRate = wav.sampleRate;
					name.clear();
				}

				/* Sin nombre en la captura, uso el archivo sin carpeta ni extension */
				if (name.empty())
				{
					const size_t slash = path.find_last_of("/\\");
					name = slash == std::string::npos ? path : path.substr(slash + 1);
					const size_t dot = name.find_last_of('.');
					if (dot != std::string::npos && dot > 0)
						name.erase(dot);
				}
				return !samples.empty();
			}

			float lastPointValue(IParamValueQueue* queue, float current)
			{
				ParamValue value = 0.0;
				int32 offset = 0;
				const int32 points = queue->getPointCount();
				if (points > 0 && queue->getPoint(points - 1, offset, value) == kResultTrue)
					return static_cast<float>(value);
				return current;
			}

			void addOutputPoint(IParameterChanges* changes, ParamID id, double value)
			{
				int32 queueIndex = 0;
				if (IParamValueQueue* queue = changes->addParameterData(id, queueIndex))
				{
					int32 pointIndex = 0;
					queue->addPoint(0, std::clamp(value, 0.0, 1.0), pointIndex);
				}
			}
		}

		EmulatorProcessor::EmulatorProcessor()
		{
			setControllerClass(ControllerUID);
		}

		tresult PLUGIN_API EmulatorProcessor::initialize(FUnknown* context)
		{
			const tresult result = AudioEffect::initialize(context);
			if (result != kResultOk)
				return result;

			addAudioInput(STR16("Stereo In"), SpeakerArr::kStereo);
			addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
			return kResultOk;
		}

		tresult PLUGIN_API EmulatorProcessor::setupProcessing(ProcessSetup& setup)
		{
			/* Preparo el convolver ya aca: el host pide la latencia antes de activar */
			convolver.prepare(setup.sampleRate, CONV_FFT_SIZE);
			return AudioEffect::setupProcessing(setup);
		}

		tresult PLUGIN_API EmulatorProcessor::setActive(TBool state)
		{
			if (state)
			{
				const double rate = processSetup.sampleRate > 0.0 ? processSetup.sampleRate : SAMPLE_RATE_DEFAULT;
				convolver.prepare(rate, CONV_FFT_SIZE);
				vuIn.prepare(rate, VU_REFERENCE_DBFS);
				vuOut.prepare(rate, VU_REFERENCE_DBFS);
				for (auto& line : idleDelay)
					line.assign(static_cast<size_t>(convolver.getLatencySamples()), 0.0f);
				idlePos = 0;
			}
			else
			{
				convolver.reset();
				for (auto& line : idleDelay)
					std::fill(line.begin(), line.end(), 0.0f);
				idlePos = 0;
				vuIn.reset();
				vuOut.reset();
			}
			return AudioEffect::setActive(state);
		}

		uint32 PLUGIN_API EmulatorProcessor::getLatencySamples()
		{
			return static_cast<uint32>(convolver.getLatencySamples());
		}

		void EmulatorProcessor::readParameterChanges(IParameterChanges* changes)
		{
			if (!changes)
				return;

			for (int32 i = 0; i < changes->getParameterCount(); ++i)
			{
				IParamValueQueue* queue = changes->getParameterData(i);
				if (!queue)
					continue;

				switch (queue->getParameterId())
				{
					case INPUT_GAIN:
						paramInputGain = lastPointValue(queue, paramInputGain);
						break;
					case MIX:
						paramMix = lastPointValue(queue, paramMix);
						break;
					case OUTPUT_GAIN:
						paramOutputGain = lastPointValue(queue, paramOutputGain);
						break;
					case BYPASS:
						bypass = lastPointValue(queue, bypass ? 1.0f : 0.0f) >= 0.5f;
						break;
					default:
						break;
				}
			}
		}

		void EmulatorProcessor::writeMeters(IParameterChanges* changes)
		{
			if (!changes)
				return;

			addOutputPoint(changes, METER_IN, vuIn.getNeedle() / VU_NEEDLE_MAX);
			addOutputPoint(changes, METER_OUT, vuOut.getNeedle() / VU_NEEDLE_MAX);
			addOutputPoint(changes, PEAK_IN, vuIn.getPeak());
			addOutputPoint(changes, PEAK_OUT, vuOut.getPeak());
		}

		/*
		 * process
		 *
		 * Pseudocodigo:
		 * 1. Leer los cambios de parametros
		 * 2. Copiar la entrada a la salida y aplicar la ganancia de entrada
		 * 3. Medir el VU de entrada
		 * 4. Convolucionar con la captura (en bypass, todo seco)
		 * 5. Aplicar la ganancia de salida y medir el VU de salida
		 * 6. Mandar agujas y picos al editor
		 */
		tresult PLUGIN_API EmulatorProcessor::process(ProcessData& data)
		{
			/* Leo los parametros (tambien llegan en llamadas sin audio) */
			readParameterChanges(data.inputParameterChanges);

			if (data.numSamples <= 0 || data.numInputs == 0 || data.numOutputs == 0)
				return kResultOk;

			void** in = getChannelBuffersPointer(processSetup, data.inputs[0]);
			void** out = getChannelBuffersPointer(processSetup, data.outputs[0]);
			const int32 outChannels = data.outputs[0].numChannels;
			const int32 channels = std::min({data.inputs[0].numChannels, outChannels, NUM_CHANNELS});
			const size_t bytes = static_cast<size_t>(data.numSamples) * sizeof(float);

			/* Copio la entrada a la salida: el resto trabaja en el lugar */
			float* buffers[NUM_CHANNELS] = {};
			for (int32 c = 0; c < channels; ++c)
			{
				buffers[c] = static_cast<float*>(out[c]);
				if (out[c] != in[c])
					std::memcpy(out[c], in[c], bytes);
			}
			for (int32 c = channels; c < outChannels; ++c)
				std::memset(out[c], 0, bytes);

			/* Aplico la ganancia de entrada y mido lo que entra a la captura */
			const float inputGain = bypass ? 1.0f : gainFromNormalized(paramInputGain);
			const float outputGain = bypass ? 1.0f : gainFromNormalized(paramOutputGain);
			for (int32 c = 0; c < channels; ++c)
			{
				for (int32 i = 0; i < data.numSamples; ++i)
					buffers[c][i] *= inputGain;
			}
			vuIn.process(buffers, channels, data.numSamples);

			/* Convoluciono: en bypass la mezcla queda seca pero con la misma latencia */
			bool convolved = false;
			{
				std::lock_guard<std::mutex> lock(captureMutex);
				convolver.setMix(bypass ? 0.0f : paramMix);
				convolved = convolver.process(buffers, channels, data.numSamples, 0);
			}
			/* Sin captura retraso igual, para que el host compense bien */
			if (!convolved)
				delayWithoutCapture(buffers, channels, data.numSamples);

			/* Aplico la ganancia de salida y mido lo que sale */
			for (int32 c = 0; c < channels; ++c)
			{
				for (int32 i = 0; i < data.numSamples; ++i)
					buffers[c][i] *= outputGain;
			}
			vuOut.process(buffers, channels, data.numSamples);

			/* Mando agujas y picos al editor */
			writeMeters(data.outputParameterChanges);

			data.outputs[0].silenceFlags = 0;
			return kResultOk;
		}

		/*
		 * delayWithoutCapture
		 *
		 * Pseudocodigo:
		 * 1. Por canal, cambiar cada muestra por la que entro hace "latencia" muestras
		 * 2. Avanzar la posicion comun del anillo
		 */
		void EmulatorProcessor::delayWithoutCapture(float** buffers, int32 channels, int32 numSamples)
		{
			const int32 length = static_cast<int32>(idleDelay[0].size());
			if (length <= 0)
				return;

			/* Intercambio con el anillo: sale la vieja, queda guardada la nueva */
			int32 pos = idlePos;
			for (int32 c = 0; c < channels; ++c)
			{
				float* line = idleDelay[c].data();
				pos = idlePos;
				for (int32 i = 0; i < numSamples; ++i)
				{
					std::swap(buffers[c][i], line[pos]);
					if (++pos == length)
						pos = 0;
				}
			}

			/* Avanzo la posicion comun */
			idlePos = pos;
		}

		tresult PLUGIN_API EmulatorProcessor::setState(IBStream* stream)
		{
			if (!stream)
				return kResultFalse;

			IBStreamer streamer(stream, kLittleEndian);
			StoredState state;
			std::vector<float> ir;
			if (!readState(streamer, state, &ir))
				return kResultFalse;

			paramInputGain = state.inputGain;
			paramMix = state.mix;
			paramOutputGain = state.outputGain;
			bypass = state.bypass >= 0.5f;

			/* Restauro la captura del proyecto (o la vacio si no tenia) */
			{
				std::lock_guard<std::mutex> lock(captureMutex);
				convolver.getIRManager().clear();
				if (!ir.empty())
					convolver.getIRManager().setLevelIR(0, ir, 0.0f);
			}
			publishCaptureInfo(ir.empty() ? std::string() : state.captureName,
							   ir.empty() ? std::string() : state.captureAuthor);
			return kResultOk;
		}

		tresult PLUGIN_API EmulatorProcessor::getState(IBStream* stream)
		{
			if (!stream)
				return kResultFalse;

			StoredState state;
			state.inputGain = paramInputGain;
			state.mix = paramMix;
			state.outputGain = paramOutputGain;
			state.bypass = bypass ? 1.0f : 0.0f;

			/* Copio la captura bajo el candado y escribo fuera de el */
			std::vector<float> ir;
			{
				std::lock_guard<std::mutex> lock(captureMutex);
				const IRManager& manager = convolver.getIRManager();
				const float* samples = manager.getIR(0);
				const int32_t length = manager.getIRLength(0);
				if (samples && length > 0 && length <= IR_STATE_MAX_SAMPLES)
					ir.assign(samples, samples + length);
				state.captureName = captureName;
				state.captureAuthor = captureAuthor;
			}

			IBStreamer streamer(stream, kLittleEndian);
			writeState(streamer, state, ir);
			return kResultOk;
		}

		/*
		 * loadCapture
		 *
		 * Pseudocodigo:
		 * 1. Leer la captura (.dfcapture o WAV)
		 * 2. Llevarla a la tasa del proyecto y recortarla al maximo del estado
		 * 3. Reemplazar la IR del convolver
		 * 4. Avisar el nombre y el autor al editor
		 */
		void EmulatorProcessor::loadCapture(const std::string& path)
		{
			/* Leo la captura */
			std::vector<float> samples;
			double fileRate = 0.0;
			std::string name;
			std::string author;
			if (!readCaptureFile(path, samples, fileRate, name, author))
				return;

			/* La llevo a la tasa del proyecto */
			const double projectRate = processSetup.sampleRate > 0.0 ? processSetup.sampleRate : SAMPLE_RATE_DEFAULT;
			std::vector<float> resampled;
			if (!IRResampler::resample(samples.data(), static_cast<int32_t>(samples.size()), fileRate, resampled,
									   projectRate))
				return;
			if (static_cast<int32_t>(resampled.size()) > IR_STATE_MAX_SAMPLES)
				resampled.resize(static_cast<size_t>(IR_STATE_MAX_SAMPLES));

			/* Reemplazo la IR */
			{
				std::lock_guard<std::mutex> lock(captureMutex);
				convolver.getIRManager().clear();
				convolver.getIRManager().setLevelIR(0, resampled, 0.0f);
			}
			publishCaptureInfo(name, author);
		}

		void EmulatorProcessor::publishCaptureInfo(const std::string& name, const std::string& author)
		{
			{
				std::lock_guard<std::mutex> lock(captureMutex);
				captureName = name;
				captureAuthor = author;
			}

			IPtr<IMessage> message = owned(allocateMessage());
			if (!message)
				return;
			message->setMessageID(MSG_CAPTURE_NAME);
			setUtf8Attribute(message->getAttributes(), ATTR_NAME, name);
			setUtf8Attribute(message->getAttributes(), ATTR_AUTHOR, author);
			sendMessage(message);
		}

		tresult PLUGIN_API EmulatorProcessor::notify(IMessage* message)
		{
			if (!message || !FIDStringsEqual(message->getMessageID(), MSG_LOAD_CAPTURE))
				return AudioEffect::notify(message);

			std::string path;
			if (!getUtf8Attribute(message->getAttributes(), ATTR_PATH, path) || path.empty())
				return kResultFalse;
			loadCapture(path);
			return kResultOk;
		}

	} // namespace Emulator
} // namespace DevicesForge
