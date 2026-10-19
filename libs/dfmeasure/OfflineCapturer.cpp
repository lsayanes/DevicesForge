#include "OfflineCapturer.h"

#include "DFConstants.h"
#include "IRPostProcessor.h"
#include "IRResampler.h"
#include "SignalGenerator.h"
#include "SweepDeconvolver.h"

#include "FFTProcessor.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <ctime>
#include <sstream>

namespace DevicesForge
{

	namespace
	{

		constexpr double kLeadSilenceSec = 0.25;
		constexpr double kTailSilenceSec = 0.25;
		constexpr double kSyncSec = 0.10;
		constexpr double kGapSec = 0.50;
		constexpr int32_t kMaxRepeats = 8;
		constexpr int32_t kMaxCorrFft = 1 << 23;
		constexpr float kSyncAmplitude = 0.8f;

		int32_t secondsToSamples(double seconds, double sampleRate)
		{
			if (seconds <= 0.0 || sampleRate <= 0.0)
				return 0;
			return static_cast<int32_t>(std::llround(seconds * sampleRate));
		}

		int32_t nextPow2(int32_t value)
		{
			int32_t p = 1;
			while (p < value && p < kMaxCorrFft)
				p <<= 1;
			return p;
		}

		/*
		 * fillSync
		 *
		 * Pseudocodigo:
		 * 1. Generar una MLS corta, distinta del barrido de medicion
		 */
		void fillSync(std::vector<float>& sync, int32_t length)
		{
			sync.assign(static_cast<size_t>(std::max(length, 0)), 0.0f);
			uint16_t lfsr = 0xACE1u;
			for (int32_t i = 0; i < length; ++i)
			{
				const uint16_t lsb = static_cast<uint16_t>(lfsr & 1u);
				lfsr = static_cast<uint16_t>(lfsr >> 1);
				if (lsb)
					lfsr = static_cast<uint16_t>(lfsr ^ 0xB400u);
				sync[static_cast<size_t>(i)] = lsb ? kSyncAmplitude : -kSyncAmplitude;
			}
		}

		/*
		 * buildSweep
		 *
		 * Pseudocodigo:
		 * 1. Pedir el barrido logaritmico al generador ya usado por el plugin
		 */
		bool buildSweep(const SweepSpec& spec, std::vector<float>& sweep)
		{
			SignalGenerator generator;
			generator.prepare(spec.sampleRate);
			generator.setType(SignalType::SineSweep);
			generator.setDuration(spec.durationSeconds);
			generator.start();
			const float* reference = generator.getReference();
			const int32_t length = generator.getReferenceLength();
			if (!reference || length <= 0)
				return false;
			sweep.assign(reference, reference + length);
			/* Dejo margen: un seno a 1.0 pisa el umbral de clip. */
			for (float& sample : sweep)
				sample *= 0.9f;
			return true;
		}

		bool layoutFor(const SweepSpec& spec, int32_t sweepLength, int32_t syncLength, SweepLayout& layout)
		{
			if (spec.sampleRate < 8000.0 || spec.sampleRate > 192000.0)
				return false;
			if (spec.durationSeconds < SIGNAL_DURATION_MIN || spec.durationSeconds > SIGNAL_DURATION_MAX)
				return false;
			if (spec.repeats < 1 || spec.repeats > kMaxRepeats)
				return false;
			if (sweepLength <= 0 || syncLength <= 0)
				return false;

			layout.sampleRate = spec.sampleRate;
			layout.repeats = spec.repeats;
			layout.leadSilence = secondsToSamples(kLeadSilenceSec, spec.sampleRate);
			layout.tailSilence = secondsToSamples(kTailSilenceSec, spec.sampleRate);
			layout.gapLength = secondsToSamples(kGapSec, spec.sampleRate);
			layout.syncLength = syncLength;
			layout.sweepLength = sweepLength;
			layout.syncSpan = layout.syncLength + layout.gapLength +
							  layout.repeats * (layout.sweepLength + layout.gapLength);
			layout.totalLength = layout.leadSilence + layout.syncSpan + layout.syncLength + layout.tailSilence;
			return layout.gapLength > 0 && layout.totalLength > 0;
		}

		/*
		 * correlate
		 *
		 * Pseudocodigo:
		 * 1. Correlacionar por FFT la grabacion contra la marca
		 * 2. El pico en el lag k es el inicio de la marca
		 */
		bool correlate(const float* signal, int32_t signalLength,
					   const float* pattern, int32_t patternLength,
					   std::vector<float>& corr)
		{
			corr.clear();
			if (!signal || !pattern || signalLength <= 0 || patternLength <= 0)
				return false;
			if (patternLength > signalLength)
				return false;

			const int32_t fftSize = nextPow2(signalLength + patternLength);
			if (fftSize < signalLength + patternLength || fftSize > kMaxCorrFft)
				return false;

			FFTProcessor fft;
			if (!fft.prepare(fftSize))
				return false;

			const int32_t bins = fft.getNumBins();
			std::vector<float> signalPad(static_cast<size_t>(fftSize), 0.0f);
			std::vector<float> patternPad(static_cast<size_t>(fftSize), 0.0f);
			std::copy(signal, signal + signalLength, signalPad.begin());
			std::copy(pattern, pattern + patternLength, patternPad.begin());

			std::vector<std::complex<float>> signalSpec(static_cast<size_t>(bins));
			std::vector<std::complex<float>> patternSpec(static_cast<size_t>(bins));
			std::vector<std::complex<float>> product(static_cast<size_t>(bins));
			fft.forward(signalPad.data(), signalSpec.data(), fftSize);
			fft.forward(patternPad.data(), patternSpec.data(), fftSize);
			for (int32_t bin = 0; bin < bins; ++bin)
				product[static_cast<size_t>(bin)] =
					signalSpec[static_cast<size_t>(bin)] * std::conj(patternSpec[static_cast<size_t>(bin)]);

			corr.assign(static_cast<size_t>(fftSize), 0.0f);
			fft.inverse(product.data(), corr.data(), fftSize);
			corr.resize(static_cast<size_t>(signalLength));
			return true;
		}

		int32_t argMaxRange(const std::vector<float>& corr, int32_t from, int32_t to)
		{
			from = std::max(0, from);
			to = std::min(to, static_cast<int32_t>(corr.size()));
			if (to <= from)
				return -1;
			int32_t best = from;
			float bestValue = corr[static_cast<size_t>(from)];
			for (int32_t i = from + 1; i < to; ++i)
			{
				if (corr[static_cast<size_t>(i)] > bestValue)
				{
					bestValue = corr[static_cast<size_t>(i)];
					best = i;
				}
			}
			return best;
		}

		/*
		 * findSyncMarks
		 *
		 * Pseudocodigo:
		 * 1. Tomar el pico global de la correlacion
		 * 2. Buscar la otra marca cerca de una distancia de syncSpan
		 */
		bool findSyncMarks(const std::vector<float>& corr, int32_t expectedSpan,
						   int32_t& indexA, int32_t& indexB)
		{
			indexA = 0;
			indexB = 0;
			if (corr.empty() || expectedSpan <= 0)
				return false;

			const int32_t first = argMaxRange(corr, 0, static_cast<int32_t>(corr.size()));
			if (first < 0)
				return false;

			const int32_t slack = std::max(1, expectedSpan / 10);
			const int32_t plus = argMaxRange(corr, first + expectedSpan - slack, first + expectedSpan + slack);
			const int32_t minus = argMaxRange(corr, first - expectedSpan - slack, first - expectedSpan + slack);
			const float plusValue = (plus >= 0) ? corr[static_cast<size_t>(plus)] : -1.0f;
			const float minusValue = (minus >= 0) ? corr[static_cast<size_t>(minus)] : -1.0f;
			const int32_t second = (plusValue >= minusValue) ? plus : minus;
			if (second < 0)
				return false;
			if (corr[static_cast<size_t>(second)] < corr[static_cast<size_t>(first)] * 0.25f)
				return false;

			indexA = std::min(first, second);
			indexB = std::max(first, second);
			return indexB > indexA;
		}

		int32_t peakIndexOf(const std::vector<float>& ir)
		{
			size_t peak = 0;
			for (size_t i = 1; i < ir.size(); ++i)
			{
				if (std::abs(ir[i]) > std::abs(ir[peak]))
					peak = i;
			}
			return static_cast<int32_t>(peak);
		}

		/*
		 * accumulatePower
		 *
		 * Pseudocodigo:
		 * 1. Pasar la IR de una pasada a frecuencia
		 * 2. Sumar |H|^2 por bin: no depende de en que muestra cayo cada pasada
		 */
		bool accumulatePower(const std::vector<float>& ir, FFTProcessor& fft, std::vector<double>& power)
		{
			const int32_t n = fft.getFFTSize();
			const int32_t half = n / 2;
			std::vector<float> padded(static_cast<size_t>(n), 0.0f);
			std::copy(ir.begin(), ir.begin() + std::min(ir.size(), padded.size()), padded.begin());
			std::vector<std::complex<float>> spec(static_cast<size_t>(fft.getNumBins()));
			fft.forward(padded.data(), spec.data(), n);

			/* pffft guarda DC en real y Nyquist en imaginario del bin 0 */
			power.resize(static_cast<size_t>(half + 1), 0.0);
			power[0] += static_cast<double>(spec[0].real()) * spec[0].real();
			power[static_cast<size_t>(half)] += static_cast<double>(spec[0].imag()) * spec[0].imag();
			for (int32_t k = 1; k < half; ++k)
				power[static_cast<size_t>(k)] += std::norm(std::complex<double>(spec[static_cast<size_t>(k)]));
			return true;
		}

		/*
		 * buildMinimumPhase
		 *
		 * Pseudocodigo:
		 * 1. Tomar el log de la magnitud promedio, con piso para los bins vacios
		 * 2. Cepstro real: IFFT del log
		 * 3. Plegar el cepstro (dejar solo la parte causal)
		 * 4. FFT y exponencial: espectro de fase minima con la misma magnitud
		 * 5. IFFT y recortar al largo pedido
		 */
		void buildMinimumPhase(const std::vector<double>& power, int32_t passes, FFTProcessor& fft,
							   int32_t outLength, std::vector<float>& ir)
		{
			const int32_t n = fft.getFFTSize();
			const int32_t half = n / 2;

			/* Calculo el log de la magnitud media con piso a -60 dB del maximo */
			std::vector<float> logMag(static_cast<size_t>(half + 1), 0.0f);
			double maxMag = 0.0;
			for (int32_t k = 0; k <= half; ++k)
				maxMag = std::max(maxMag, std::sqrt(power[static_cast<size_t>(k)] / passes));
			const double floor = std::max(maxMag * 1.0e-3, 1.0e-30);
			for (int32_t k = 0; k <= half; ++k)
			{
				const double mag = std::sqrt(power[static_cast<size_t>(k)] / passes);
				logMag[static_cast<size_t>(k)] = static_cast<float>(std::log(std::max(mag, floor)));
			}

			/* Armo el espectro real y par, y saco el cepstro */
			std::vector<std::complex<float>> spec(static_cast<size_t>(fft.getNumBins()));
			spec[0] = {logMag[0], logMag[static_cast<size_t>(half)]};
			for (int32_t k = 1; k < half; ++k)
				spec[static_cast<size_t>(k)] = {logMag[static_cast<size_t>(k)], 0.0f};
			std::vector<float> cepstrum(static_cast<size_t>(n), 0.0f);
			fft.inverse(spec.data(), cepstrum.data(), n);

			/* Pliego: duplico la parte positiva y anulo la negativa */
			for (int32_t i = 1; i < half; ++i)
				cepstrum[static_cast<size_t>(i)] *= 2.0f;
			std::fill(cepstrum.begin() + half + 1, cepstrum.end(), 0.0f);

			/* Vuelvo a frecuencia y aplico la exponencial compleja */
			fft.forward(cepstrum.data(), spec.data(), n);
			spec[0] = {std::exp(spec[0].real()), std::exp(spec[0].imag())};
			for (int32_t k = 1; k < half; ++k)
				spec[static_cast<size_t>(k)] = std::exp(spec[static_cast<size_t>(k)]);

			/* Paso a tiempo y me quedo con el largo de la cola */
			ir.assign(static_cast<size_t>(n), 0.0f);
			fft.inverse(spec.data(), ir.data(), n);
			ir.resize(static_cast<size_t>(std::min(outLength, n)));
		}

		std::string todayIso()
		{
			const std::time_t now = std::time(nullptr);
			std::tm local {};
#if defined(_WIN32)
			localtime_s(&local, &now);
#else
			localtime_r(&now, &local);
#endif
			char text[16] = {};
			if (std::strftime(text, sizeof(text), "%Y-%m-%d", &local) == 0)
				return {};
			return text;
		}

		float rmsOf(const float* data, int32_t from, int32_t to)
		{
			if (!data || to <= from)
				return 0.0f;
			double sum = 0.0;
			for (int32_t i = from; i < to; ++i)
			{
				const double sample = data[i];
				sum += sample * sample;
			}
			return static_cast<float>(std::sqrt(sum / static_cast<double>(to - from)));
		}

		/*
		 * buildReport
		 *
		 * Pseudocodigo:
		 * 1. Medir pico, clips y relacion senal/ruido
		 * 2. Anotar la velocidad media, la polaridad y el limite de wow/flutter
		 */
		std::string buildReport(const float* recording, int32_t length, int32_t noiseEnd,
								double sampleRate, double speedRatio, int32_t repeats,
								int32_t repeatsUsed, int32_t irSamples, float bandGainDb,
								bool polarityInverted, int32_t peakIndex)
		{
			float peak = 0.0f;
			int32_t clipped = 0;
			if (recording && length > 0)
			{
				for (int32_t i = 0; i < length; ++i)
				{
					const float magnitude = std::abs(recording[i]);
					peak = std::max(peak, magnitude);
					if (magnitude >= CAPTURE_CLIP_THRESHOLD)
						++clipped;
				}
			}

			const int32_t noiseTo = std::clamp(noiseEnd, 0, length);
			const float noiseRms = rmsOf(recording, 0, noiseTo);
			const float signalRms = rmsOf(recording, noiseTo, length);
			const float snrDb = (noiseRms > 0.0f && signalRms > 0.0f)
									? 20.0f * std::log10(signalRms / noiseRms)
									: 0.0f;

			const char* quality = "ok";
			if (peak < CAPTURE_MIN_PEAK)
				quality = "silence";
			else if (clipped > 0)
				quality = "clipping";
			else if (peak < CAPTURE_GOOD_PEAK_MIN)
				quality = "level_low";
			else if (noiseRms > 0.0f && snrDb < CAPTURE_GOOD_SNR_DB)
				quality = "low_snr";

			std::ostringstream text;
			text << "sample_rate=" << sampleRate << "\n"
				 << "repeats=" << repeats << "\n"
				 << "repeats_used=" << repeatsUsed << "\n"
				 << "speed_ratio=" << speedRatio << "\n"
				 << "peak=" << peak << "\n"
				 << "peak_dbfs=" << linearToDbfs(peak) << "\n"
				 << "noise_floor_dbfs=" << linearToDbfs(noiseRms) << "\n"
				 << "signal_rms_dbfs=" << linearToDbfs(signalRms) << "\n"
				 << "snr_db=" << snrDb << "\n"
				 << "clipped_samples=" << clipped << "\n"
				 << "ir_samples=" << irSamples << "\n"
				 << "band_gain_db=" << bandGainDb << "\n"
				 << "polarity=" << (polarityInverted ? "invertida_corregida" : "normal") << "\n"
				 << "peak_sample=" << peakIndex << "\n"
				 << "ir_phase=minima_magnitud_promedio\n"
				 << "quality=" << quality << "\n"
				 << "limit=velocidad_media_corregida_wow_flutter_no\n";
			return text.str();
		}

	} // namespace

	bool OfflineCapturer::makeSweep(const SweepSpec& spec, std::vector<float>& samples, SweepLayout& layout)
	{
		samples.clear();
		std::vector<float> sweep;
		std::vector<float> sync;
		if (!buildSweep(spec, sweep))
			return false;
		fillSync(sync, secondsToSamples(kSyncSec, spec.sampleRate));
		if (!layoutFor(spec, static_cast<int32_t>(sweep.size()), static_cast<int32_t>(sync.size()), layout))
			return false;

		/* Armo silencio + marca + N barridos con cola + marca + silencio */
		samples.assign(static_cast<size_t>(layout.totalLength), 0.0f);
		const int32_t syncA = layout.leadSilence;
		std::copy(sync.begin(), sync.end(), samples.begin() + syncA);

		for (int32_t rep = 0; rep < layout.repeats; ++rep)
		{
			const int32_t at = syncA + layout.syncLength + layout.gapLength +
							   rep * (layout.sweepLength + layout.gapLength);
			std::copy(sweep.begin(), sweep.end(), samples.begin() + at);
		}

		const int32_t syncB = syncA + layout.syncSpan;
		std::copy(sync.begin(), sync.end(), samples.begin() + syncB);
		return true;
	}

	bool OfflineCapturer::analyze(const float* recording,
								  int32_t recordingLength,
								  double sampleRate,
								  const AnalyzeOptions& options,
								  CaptureData& capture,
								  QualityReport& report)
	{
		report = QualityReport {};
		capture = CaptureData {};
		if (!recording || recordingLength <= 0 || sampleRate <= 0.0)
		{
			report.text = "grabacion vacia\n";
			return false;
		}

		SweepSpec spec = options.sweep;
		spec.sampleRate = sampleRate;

		std::vector<float> sweep;
		std::vector<float> sync;
		SweepLayout layout;
		if (!buildSweep(spec, sweep) || !layoutFor(spec, static_cast<int32_t>(sweep.size()),
												   secondsToSamples(kSyncSec, sampleRate), layout))
		{
			report.text = "duration o repeats fuera de rango\n";
			return false;
		}
		fillSync(sync, layout.syncLength);

		std::vector<float> corr;
		if (!correlate(recording, recordingLength, sync.data(), layout.syncLength, corr))
		{
			report.text = "no se pudo correlacionar (grabacion demasiado larga)\n";
			return false;
		}

		int32_t indexA = 0;
		int32_t indexB = 0;
		if (!findSyncMarks(corr, layout.syncSpan, indexA, indexB))
		{
			report.text = "no encontre las dos marcas; duration y repeats tienen que ser los del barrido\n";
			return false;
		}

		const int32_t measuredSpan = indexB - indexA;
		const int32_t noiseEnd = indexA;
		const double speedRatio = static_cast<double>(measuredSpan) / static_cast<double>(layout.syncSpan);
		report.speedRatio = speedRatio;
		if (speedRatio < 0.90 || speedRatio > 1.10)
		{
			report.text = "la distancia entre marcas no coincide con el barrido (ratio " +
						  std::to_string(speedRatio) + ")\n";
			return false;
		}

		/* Si la cinta corrio a otra velocidad, estiro la grabacion a la distancia esperada */
		std::vector<float> corrected;
		const float* aligned = recording;
		int32_t alignedLength = recordingLength;
		if (std::abs(measuredSpan - layout.syncSpan) > 2)
		{
			if (!IRResampler::resample(recording, recordingLength,
									   static_cast<double>(measuredSpan), corrected,
									   static_cast<double>(layout.syncSpan)))
			{
				report.text = "no pude corregir la velocidad\n";
				return false;
			}
			aligned = corrected.data();
			alignedLength = static_cast<int32_t>(corrected.size());

			if (!correlate(aligned, alignedLength, sync.data(), layout.syncLength, corr) ||
				!findSyncMarks(corr, layout.syncSpan, indexA, indexB))
			{
				report.text = "perdi las marcas despues de corregir la velocidad\n";
				return false;
			}
		}

		/* Cada barrido ya arranca en su lugar: busco el alineado solo en un margen corto */
		const int32_t margin = std::min(layout.gapLength / 4, 2048);
		const int32_t maxLag = std::max(margin + 512, 1024);
		/* Promedio |H|^2: el wow corre distinto cada pasada y sumar IRs cancela bandas */
		FFTProcessor spectrumFft;
		if (!spectrumFft.prepare(nextPow2(4 * layout.gapLength)))
		{
			report.text = "no pude preparar la FFT de la IR\n";
			return false;
		}
		std::vector<double> power;
		bool polarityChecked = false;
		int32_t used = 0;

		for (int32_t rep = 0; rep < layout.repeats; ++rep)
		{
			const int32_t nominal = indexA + layout.syncLength + layout.gapLength +
									rep * (layout.sweepLength + layout.gapLength);
			int32_t start = nominal - margin;
			if (start < 0)
				start = 0;
			const int32_t end = std::min(alignedLength, nominal + layout.sweepLength + layout.gapLength - margin);
			if (end - start <= layout.sweepLength / 2)
				continue;

			std::vector<float> ir;
			if (!SweepDeconvolver::deconvolve(aligned + start, end - start, sweep.data(),
											  static_cast<int32_t>(sweep.size()), ir, 0, sampleRate, maxLag))
				continue;

			/* Mas alla de la cola, el barrido siguiente ya contamina */
			if (static_cast<int32_t>(ir.size()) > layout.gapLength)
				ir.resize(static_cast<size_t>(layout.gapLength));

			/* Anoto si el equipo invierte: la fase minima sale siempre positiva */
			if (!polarityChecked)
			{
				const int32_t peak = peakIndexOf(ir);
				report.polarityInverted = ir[static_cast<size_t>(peak)] < 0.0f;
				polarityChecked = true;
			}

			accumulatePower(ir, spectrumFft, power);
			++used;
		}

		if (used < 1 || power.empty())
		{
			report.text = "ningun barrido se pudo deconvolucionar\n";
			report.repeatsUsed = 0;
			return false;
		}

		/* Armo la IR de fase minima con la magnitud promedio */
		std::vector<float> accumulated;
		buildMinimumPhase(power, used, spectrumFft, layout.gapLength, accumulated);
		report.peakIndex = peakIndexOf(accumulated);

		IRPostProcessSettings settings;
		settings.windowType = IRWindowType::Hanning;
		settings.applyNormalize = false;
		IRPostProcessor::process(accumulated, settings);

		float bandGainDb = 0.0f;
		if (!normalizeBandUnity(accumulated, sampleRate, bandGainDb))
		{
			report.text = "no pude normalizar la IR\n";
			return false;
		}

		capture.name = options.name;
		capture.device = options.device;
		capture.author = options.author;
		capture.date = todayIso();
		capture.notes = options.notes;
		capture.sampleRate = sampleRate;
		capture.sendDb = options.sendDb;
		capture.bandGainDb = bandGainDb;
		capture.bandNormalized = true;
		capture.channels.assign(1, std::move(accumulated));

		report.ok = true;
		report.repeatsUsed = used;
		report.text = buildReport(recording, recordingLength, noiseEnd, sampleRate, speedRatio,
								  layout.repeats, used, static_cast<int32_t>(capture.channels[0].size()),
								  bandGainDb, report.polarityInverted, report.peakIndex);
		return true;
	}

} // namespace DevicesForge
