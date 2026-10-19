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
#include <cstdio>
#include <ctime>
#include <fstream>
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
		/** 2^(1/12): borde de una banda de 1/6 de octava respecto del centro. */
		constexpr double kSixthEdge = 1.0594630943592953;

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
		 * sixthOctaveCenters
		 *
		 * Pseudocodigo:
		 * 1. Recorrer 1 kHz * 2^(k/6) desde ~25 Hz
		 * 2. Cortar en 20 kHz o antes, si la banda entera no entra bajo Nyquist
		 */
		void sixthOctaveCenters(double sampleRate, std::vector<float>& centers)
		{
			centers.clear();
			const double topHz = std::min(20000.0, 0.45 * sampleRate / kSixthEdge);
			for (int32_t k = -32;; ++k)
			{
				const double fc = 1000.0 * std::pow(2.0, k / 6.0);
				if (fc > topHz)
					break;
				centers.push_back(static_cast<float>(fc));
			}
		}

		/*
		 * bandLevelsDb
		 *
		 * Pseudocodigo:
		 * 1. Promediar |H|^2 entre los bordes de cada banda (al menos un bin)
		 * 2. Pasar a dB y restar la banda mas cercana a 1 kHz
		 */
		void bandLevelsDb(const std::vector<double>& power, int32_t fftSize, double sampleRate,
						  const std::vector<float>& centers, std::vector<float>& levels)
		{
			levels.assign(centers.size(), 0.0f);
			if (power.size() < 2 || fftSize <= 0 || sampleRate <= 0.0)
				return;

			const double binHz = sampleRate / fftSize;
			const int32_t last = static_cast<int32_t>(power.size()) - 1;
			size_t refBand = 0;
			for (size_t b = 0; b < centers.size(); ++b)
			{
				/* Promedio la potencia de los bins de la banda */
				const double fc = centers[b];
				const int32_t from = std::clamp(static_cast<int32_t>(std::ceil(fc / kSixthEdge / binHz)), 1, last);
				const int32_t to = std::clamp(static_cast<int32_t>(std::floor(fc * kSixthEdge / binHz)), from, last);
				double sum = 0.0;
				for (int32_t k = from; k <= to; ++k)
					sum += power[static_cast<size_t>(k)];
				const double mean = sum / static_cast<double>(to - from + 1);
				levels[b] = static_cast<float>(10.0 * std::log10(std::max(mean, 1.0e-30)));
				if (std::fabs(fc - 1000.0) < std::fabs(centers[refBand] - 1000.0))
					refBand = b;
			}

			/* Referencio todo a 1 kHz */
			const float ref = levels[refBand];
			for (float& level : levels)
				level -= ref;
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
		const int32_t spectrumSize = spectrumFft.getFFTSize();
		std::vector<double> power;
		std::vector<double> passPower;
		sixthOctaveCenters(sampleRate, report.bandHz);
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

			/* Saco |H|^2 de la pasada, lo sumo al total y guardo su respuesta por banda */
			std::fill(passPower.begin(), passPower.end(), 0.0);
			accumulatePower(ir, spectrumFft, passPower);
			power.resize(passPower.size(), 0.0);
			for (size_t k = 0; k < passPower.size(); ++k)
				power[k] += passPower[k];
			report.passBandsDb.emplace_back();
			bandLevelsDb(passPower, spectrumSize, sampleRate, report.bandHz, report.passBandsDb.back());
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

		/* Mido la IR ya ventaneada: es la que va a sonar en el plugin */
		std::vector<double> irPower;
		accumulatePower(capture.channels[0], spectrumFft, irPower);
		bandLevelsDb(irPower, spectrumSize, sampleRate, report.bandHz, report.irBandsDb);

		report.ok = true;
		report.repeatsUsed = used;
		report.text = buildReport(recording, recordingLength, noiseEnd, sampleRate, speedRatio,
								  layout.repeats, used, static_cast<int32_t>(capture.channels[0].size()),
								  bandGainDb, report.polarityInverted, report.peakIndex);
		return true;
	}

	namespace
	{

		std::string htmlEscape(const std::string& text)
		{
			std::string out;
			out.reserve(text.size());
			for (unsigned char ch : text)
			{
				if (ch == '&')
					out += "&amp;";
				else if (ch == '<')
					out += "&lt;";
				else if (ch == '>')
					out += "&gt;";
				else if (ch == '"')
					out += "&quot;";
				else
					out += static_cast<char>(ch);
			}
			return out;
		}

		std::string numText(double value, int decimals, bool sign)
		{
			char buf[32];
			if (sign)
				std::snprintf(buf, sizeof(buf), "%+.*f", decimals, value);
			else
				std::snprintf(buf, sizeof(buf), "%.*f", decimals, value);
			std::string text = buf;
			const auto dot = text.find('.');
			if (dot != std::string::npos)
				text[dot] = ',';
			if (sign && (text == "+0,0" || text == "-0,0"))
				text = "0,0";
			return text;
		}

		/*
		 * svgNum
		 *
		 * Pseudocodigo:
		 * 1. Redondear a centesimos y escribir entero + "." + 2 digitos
		 *    (con %f y locale en espanol sale coma y el SVG no la entiende)
		 */
		std::string svgNum(double value)
		{
			const long long cents = std::llround(value * 100.0);
			const long long magnitude = cents < 0 ? -cents : cents;
			char buf[32];
			std::snprintf(buf, sizeof(buf), "%s%lld.%02lld", cents < 0 ? "-" : "", magnitude / 100, magnitude % 100);
			return buf;
		}

		const char* passColor(size_t pass)
		{
			static const char* colors[] = {"#e69440", "#9a8cd8", "#d07090", "#c8b850",
										   "#6aa0e0", "#a0a8b0", "#e07050", "#80c080"};
			return colors[pass % 8];
		}

		std::string hzLabel(double hz)
		{
			if (hz < 1000.0)
				return numText(hz, 0, false) + " Hz";
			const double kilo = hz / 1000.0;
			const bool whole = std::abs(kilo - std::round(kilo)) < 0.05;
			std::string text = numText(kilo, whole ? 0 : 1, false);
			if (text.size() >= 2 && text.compare(text.size() - 2, 2, ",0") == 0)
				text.resize(text.size() - 2);
			return text + " kHz";
		}

	} // namespace

	/*
	 * OfflineCapturer::writeHtmlReport
	 */
	bool OfflineCapturer::writeHtmlReport(const std::string& path, const CaptureData& capture,
										  const QualityReport& report)
	{
		if (path.empty())
			return false;

		const std::vector<float>& hz = report.bandHz;
		const bool hasBands = !hz.empty();
		auto matches = [&](const std::vector<float>& curve) {
			return hasBands && curve.size() == hz.size();
		};

		/* Junto las pasadas validas y mido el rango y la separacion entre ellas */
		std::vector<const std::vector<float>*> passes;
		float low = 0.0f;
		float high = 0.0f;
		auto consider = [&](const std::vector<float>& curve) {
			if (!matches(curve))
				return;
			for (float db : curve)
			{
				low = std::min(low, db);
				high = std::max(high, db);
			}
		};
		for (const std::vector<float>& pass : report.passBandsDb)
		{
			if (!matches(pass))
				continue;
			passes.push_back(&pass);
			consider(pass);
		}
		consider(report.irBandsDb);

		float spread = 0.0f;
		for (size_t b = 0; passes.size() >= 2 && b < hz.size(); ++b)
		{
			if (hz[b] < 40.0f || hz[b] > 16000.0f)
				continue;
			float lo = (*passes[0])[b];
			float hi = lo;
			for (const std::vector<float>* pass : passes)
			{
				lo = std::min(lo, (*pass)[b]);
				hi = std::max(hi, (*pass)[b]);
			}
			spread = std::max(spread, hi - lo);
		}

		const float dbMin = std::clamp(std::floor((low - 1.0f) / 3.0f) * 3.0f, -30.0f, -6.0f);
		const float dbMax = std::clamp(std::ceil((high + 1.0f) / 3.0f) * 3.0f, 6.0f, 18.0f);
		constexpr double kLeft = 46.0;
		constexpr double kTop = 16.0;
		constexpr double kRight = 628.0;
		constexpr double kBottom = 232.0;
		auto xOf = [](double freq) {
			return kLeft + std::log(freq / 20.0) / std::log(1000.0) * (kRight - kLeft);
		};
		auto yOf = [&](double db) {
			const double clamped = std::clamp(db, static_cast<double>(dbMin), static_cast<double>(dbMax));
			return kBottom - (clamped - dbMin) / (dbMax - dbMin) * (kBottom - kTop);
		};

		/* Dibujo la grilla y las curvas en un SVG autonomo */
		std::ostringstream svg;
		std::ostringstream legend;
		if (!passes.empty())
		{
			svg << "<svg viewBox=\"0 0 640 252\">";
			const double gridHz[] = {20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000};
			for (double freq : gridHz)
			{
				const std::string x = svgNum(xOf(freq));
				svg << "<line x1=\"" << x << "\" y1=\"" << svgNum(kTop) << "\" x2=\"" << x << "\" y2=\""
					<< svgNum(kBottom) << "\" stroke=\"#2c3138\"/>"
					<< "<text x=\"" << x << "\" y=\"248\" fill=\"#c4c8d0\" font-size=\"11\" text-anchor=\"middle\">"
					<< (freq < 1000.0 ? numText(freq, 0, false) : numText(freq / 1000.0, 0, false) + "k")
					<< "</text>";
			}
			const float step = (dbMax - dbMin) > 24.0f ? 6.0f : 3.0f;
			for (float db = dbMin; db <= dbMax + 0.01f; db += step)
			{
				const std::string y = svgNum(yOf(db));
				svg << "<line x1=\"" << svgNum(kLeft) << "\" y1=\"" << y << "\" x2=\"" << svgNum(kRight)
					<< "\" y2=\"" << y << "\" stroke=\"" << (std::fabs(db) < 0.01f ? "#6a7078" : "#2c3138") << "\"/>"
					<< "<text x=\"40\" y=\"" << y
					<< "\" fill=\"#c4c8d0\" font-size=\"11\" text-anchor=\"end\" dominant-baseline=\"middle\">"
					<< static_cast<int>(db) << "</text>";
			}

			auto polyline = [&](const std::vector<float>& curve, const char* color, double width) {
				svg << "<polyline fill=\"none\" stroke=\"" << color << "\" stroke-width=\"" << svgNum(width)
					<< "\" stroke-linejoin=\"round\" points=\"";
				for (size_t b = 0; b < curve.size(); ++b)
				{
					if (b)
						svg << ' ';
					svg << svgNum(xOf(hz[b])) << ',' << svgNum(yOf(curve[b]));
				}
				svg << "\"/>";
			};
			for (size_t i = 0; i < passes.size(); ++i)
			{
				polyline(*passes[i], passColor(i), 1.4);
				legend << "<span><i style=\"background:" << passColor(i) << "\"></i>Repetición " << (i + 1) << "</span>";
			}
			if (matches(report.irBandsDb))
			{
				polyline(report.irBandsDb, "#7dccc4", 2.4);
				legend << "<span><i style=\"background:#7dccc4\"></i>IR</span>";
			}
			svg << "</svg>";
		}

		/* Tabla corta: la banda mas cercana a cada frecuencia de lectura */
		std::ostringstream table;
		const double marks[] = {50, 100, 200, 500, 1000, 2000, 4000, 8000, 10000, 12000, 16000};
		if (!passes.empty())
		{
			table << "<table><tr><th>Frecuencia</th>";
			for (size_t i = 0; i < passes.size(); ++i)
				table << "<th>Rep. " << (i + 1) << "</th>";
			if (matches(report.irBandsDb))
				table << "<th>IR</th>";
			table << "</tr>";
			for (double mark : marks)
			{
				size_t band = 0;
				double best = 1.0e9;
				for (size_t b = 0; b < hz.size(); ++b)
				{
					const double distance = std::fabs(std::log(hz[b] / mark));
					if (distance < best)
					{
						best = distance;
						band = b;
					}
				}
				/* Medio sexto de octava, con margen: la grilla no cae justo en la marca */
				if (best > std::log(2.0) / 12.0 * 1.5)
					continue;
				table << "<tr><td>" << hzLabel(hz[band]) << "</td>";
				for (const std::vector<float>* pass : passes)
					table << "<td>" << numText((*pass)[band], 1, true) << "</td>";
				if (matches(report.irBandsDb))
					table << "<td>" << numText(report.irBandsDb[band], 1, true) << "</td>";
				table << "</tr>";
			}
			table << "</table>";
		}

		/* Los primeros 3 ms de la IR, para ver el pico */
		std::ostringstream head;
		if (!capture.channels.empty() && capture.sampleRate > 0.0)
		{
			const std::vector<float>& ir = capture.channels[0];
			const int32_t count = std::min(static_cast<int32_t>(ir.size()),
											static_cast<int32_t>(std::llround(0.003 * capture.sampleRate)));
			float peak = 0.0f;
			for (int32_t i = 0; i < count; ++i)
				peak = std::max(peak, std::abs(ir[static_cast<size_t>(i)]));
			if (count >= 2 && peak > 0.0f)
			{
				head << "<h2>Comienzo de la IR</h2><svg viewBox=\"0 0 640 96\">";
				head << "<polyline fill=\"none\" stroke=\"#7dccc4\" stroke-width=\"1.6\" points=\"";
				for (int32_t i = 0; i < count; ++i)
				{
					if (i)
						head << ' ';
					const double x = 8.0 + static_cast<double>(i) / (count - 1) * 624.0;
					const double y = 48.0 - static_cast<double>(ir[static_cast<size_t>(i)]) / peak * 40.0;
					head << svgNum(x) << ',' << svgNum(y);
				}
				head << "\"/></svg><p class=\"note\">0 a 3 ms. El pico tiene que ser positivo y cerca del inicio.</p>";
			}
		}

		const std::string title = capture.name.empty() ? "Captura" : capture.name;
		std::string meta = htmlEscape(capture.device);
		if (!capture.author.empty())
			meta += (meta.empty() ? "" : " · ") + htmlEscape(capture.author);
		if (!capture.date.empty())
			meta += (meta.empty() ? "" : " · ") + htmlEscape(capture.date);

		std::ostringstream page;
		page << "<!DOCTYPE html><html lang=\"es\"><head><meta charset=\"utf-8\"><title>"
			 << htmlEscape(title) << "</title><style>"
			 << "body{background:#1c1e22;color:#dcdde2;font:14px -apple-system,sans-serif;margin:24px;}"
			 << "h1{font-size:20px;margin:0 0 4px;}h2{font-size:15px;margin:22px 0 8px;}"
			 << ".meta,.note{color:#9aa0a8;} .stats{display:flex;gap:28px;margin:16px 0;flex-wrap:wrap;}"
			 << ".stats b{display:block;font-size:18px;} .stats span{color:#9aa0a8;font-size:12px;}"
			 << "svg{background:#121416;border:1px solid #3a3e44;width:100%;max-width:860px;height:auto;}"
			 << ".legend{margin:8px 0 16px;} .legend span{margin-right:16px;}"
			 << ".legend i{display:inline-block;width:14px;height:3px;margin-right:6px;vertical-align:middle;}"
			 << "table{border-collapse:collapse;} td,th{padding:4px 12px;border-bottom:1px solid #3a3e44;"
			 << "text-align:right;} th:first-child,td:first-child{text-align:left;}"
			 << "pre{font:12px Menlo,monospace;color:#c4c8d0;white-space:pre-wrap;}"
			 << "</style></head><body><h1>" << htmlEscape(title) << "</h1><p class=\"meta\">" << meta << "</p>"
			 << "<div class=\"stats\">"
			 << "<div><b>" << numText(report.speedRatio, 5, false) << "</b><span>Velocidad</span></div>"
			 << "<div><b>" << (report.polarityInverted ? "Invertida" : "Normal")
			 << "</b><span>Polaridad" << (report.polarityInverted ? ", corregida" : "") << "</span></div>"
			 << "<div><b>" << report.peakIndex << "</b><span>Muestra del pico</span></div>"
			 << "<div><b>" << (passes.size() >= 2 ? numText(spread, 1, false) + " dB" : "—")
			 << "</b><span>Separación entre repeticiones</span></div></div>";
		if (!passes.empty())
		{
			page << "<h2>Respuesta, relativa a 1 kHz</h2>" << svg.str()
				 << "<div class=\"legend\">" << legend.str() << "</div>"
				 << "<p class=\"note\">0 dB es el nivel en 1 kHz. La IR es el promedio de magnitud de las repeticiones. "
				 << "De 20 Hz a 20 kHz, en 1/6 de octava.</p>" << table.str();
		}
		page << head.str();
		if (!capture.notes.empty())
			page << "<p class=\"note\">" << htmlEscape(capture.notes) << "</p>";
		page << "<h2>Reporte de la toma</h2><pre>" << htmlEscape(report.text) << "</pre></body></html>\n";

		/* Escribo el archivo de una vez */
		std::ofstream out(path);
		out << page.str();
		return static_cast<bool>(out);
	}

} // namespace DevicesForge
