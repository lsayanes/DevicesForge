#include "SweepDeconvolver.h"

#include "DFMeasureConstants.h"
#include "FFTProcessor.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>

namespace DevicesForge
{

	namespace
	{
		int32_t nextPowerOfTwo(int32_t value)
		{
			if (value <= 1)
				return 1;
			int32_t p = 1;
			while (p < value)
				p <<= 1;
			return p;
		}
	}

	int32_t SweepDeconvolver::chooseFftSize(int32_t recordedLength, int32_t referenceLength, int32_t hint)
	{
		const int32_t minSize = nextPowerOfTwo(recordedLength + referenceLength);
		if (hint > 0 && (hint & (hint - 1)) == 0 && hint >= minSize)
			return hint;
		return minSize;
	}

	int32_t SweepDeconvolver::findAlignmentOffset(const float* recorded,
												  int32_t recordedLength,
												  const float* reference,
												  int32_t referenceLength,
												  int32_t maxLag)
	{
		if (!recorded || !reference || recordedLength <= 0 || referenceLength <= 0)
			return 0;

		maxLag = std::min(maxLag, recordedLength - 1);
		maxLag = std::min(maxLag, referenceLength);

		float bestCorr = -1.0f;
		int32_t bestOffset = 0;

		const int32_t compareLen = std::min(referenceLength, recordedLength);

		/* Comparo en valor absoluto: un equipo que invierte da el pico negativo */
		for (int32_t lag = 0; lag <= maxLag; ++lag)
		{
			float corr = 0.0f;
			for (int32_t i = 0; i < compareLen - lag; ++i)
				corr += recorded[static_cast<size_t>(lag + i)] * reference[static_cast<size_t>(i)];
			corr = std::fabs(corr);

			if (corr > bestCorr)
			{
				bestCorr = corr;
				bestOffset = lag;
			}
		}

		return bestOffset;
	}

	/*
	 * bandWeight
	 *
	 * Pseudocodigo:
	 * 1. Peso 1 dentro de la banda del barrido
	 * 2. Bajar suave en los bordes y cortar en 0 fuera, sin dejar cola ultrasonica
	 */
	float SweepDeconvolver::bandWeight(double frequencyHz, double sampleRate)
	{
		const double nyquist = sampleRate * 0.5;
		const double lo = SWEEP_FREQ_START_HZ;
		const double hi = std::min(static_cast<double>(SWEEP_FREQ_END_HZ), nyquist * 0.95);
		const double loEdge = lo * 0.5;
		const double hiKnee = hi * 0.95;

		if (frequencyHz <= loEdge || frequencyHz >= hi || frequencyHz >= nyquist)
			return 0.0f;
		if (frequencyHz >= lo && frequencyHz <= hiKnee)
			return 1.0f;

		if (frequencyHz < lo)
		{
			const double t = (frequencyHz - loEdge) / (lo - loEdge);
			return static_cast<float>(0.5 * (1.0 - std::cos(kPi * t)));
		}

		const double t = (frequencyHz - hiKnee) / (hi - hiKnee);
		return static_cast<float>(0.5 * (1.0 + std::cos(kPi * t)));
	}

	/*
	 * deconvolve
	 *
	 * Pseudocodigo:
	 * 1. Alinear la grabacion contra la referencia en un margen corto
	 * 2. Dividir espectros con un piso bajo, para no apagar los agudos del barrido
	 * 3. Rotar para dejar el pico en DECONV_PRE_ROLL, con lo previo al pico entero
	 */
	bool SweepDeconvolver::deconvolve(const float* recorded,
									  int32_t recordedLength,
									  const float* reference,
									  int32_t referenceLength,
									  std::vector<float>& irOut,
									  int32_t fftSizeHint,
									  double sampleRate,
									  int32_t maxAlignLag)
	{
		if (!recorded || !reference || recordedLength <= 0 || referenceLength <= 0)
			return false;

		/* maxAlignLag < 0 busca en todo el barrido. Un tope chico sirve si ya esta alineado. */
		const int32_t lagLimit = (maxAlignLag >= 0) ? maxAlignLag : referenceLength;
		const int32_t alignOffset =
			findAlignmentOffset(recorded, recordedLength, reference, referenceLength, lagLimit);

		const int32_t alignedLength = recordedLength - alignOffset;
		if (alignedLength <= 0)
			return false;

		const int32_t fftSize = chooseFftSize(alignedLength, referenceLength, fftSizeHint);

		FFTProcessor fft;
		if (!fft.prepare(fftSize))
			return false;

		const int32_t numBins = fft.getNumBins();
		std::vector<float> recPadded(static_cast<size_t>(fftSize), 0.0f);
		std::vector<float> refPadded(static_cast<size_t>(fftSize), 0.0f);

		const int32_t recCopy = std::min(alignedLength, fftSize);
		const int32_t refCopy = std::min(referenceLength, fftSize);
		std::memcpy(recPadded.data(), recorded + alignOffset, static_cast<size_t>(recCopy) * sizeof(float));
		std::memcpy(refPadded.data(), reference, static_cast<size_t>(refCopy) * sizeof(float));

		std::vector<std::complex<float>> recSpec(static_cast<size_t>(numBins));
		std::vector<std::complex<float>> refSpec(static_cast<size_t>(numBins));
		std::vector<std::complex<float>> irSpec(static_cast<size_t>(numBins));

		fft.forward(recPadded.data(), recSpec.data(), fftSize);
		fft.forward(refPadded.data(), refSpec.data(), fftSize);

		/* Piso bajo respecto del pico: un epsilon grande come los agudos del barrido */
		float maxRefNorm = 0.0f;
		for (int32_t bin = 0; bin < numBins; ++bin)
			maxRefNorm = std::max(maxRefNorm, std::norm(refSpec[static_cast<size_t>(bin)]));

		const float epsilon = std::max(maxRefNorm * DECONV_REGULARIZATION, 1e-20f);
		const double binHz = (sampleRate > 0.0 ? sampleRate : SAMPLE_RATE_DEFAULT) /
							 static_cast<double>(fftSize);

		for (int32_t bin = 0; bin < numBins; ++bin)
		{
			const std::complex<float>& den = refSpec[static_cast<size_t>(bin)];
			const float denom = std::norm(den) + epsilon;
			const float weight = bandWeight(static_cast<double>(bin) * binHz, sampleRate);
			irSpec[static_cast<size_t>(bin)] =
				(recSpec[static_cast<size_t>(bin)] * std::conj(den)) * (weight / denom);
		}

		irOut.assign(static_cast<size_t>(fftSize), 0.0f);
		fft.inverse(irSpec.data(), irOut.data(), fftSize);
		/* El impulso puede quedar partido entre el final y el inicio (es circular):
		   lo roto para que el pico caiga en DECONV_PRE_ROLL y no se pierda lo previo */
		int32_t peakIndex = 0;
		float peakAbs = 0.0f;
		for (int32_t i = 0; i < fftSize; ++i)
		{
			const float magnitude = std::fabs(irOut[static_cast<size_t>(i)]);
			if (magnitude > peakAbs)
			{
				peakAbs = magnitude;
				peakIndex = i;
			}
		}
		if (fftSize > 2 * DECONV_PRE_ROLL)
		{
			const int32_t shift = (peakIndex - DECONV_PRE_ROLL + fftSize) % fftSize;
			std::rotate(irOut.begin(), irOut.begin() + shift, irOut.end());
		}

		int32_t effectiveLength = std::min(alignedLength, fftSize);
		effectiveLength = std::min(effectiveLength, MAX_IR_LENGTH);
		irOut.resize(static_cast<size_t>(effectiveLength));

		return !irOut.empty();
	}

} // DevicesForge
