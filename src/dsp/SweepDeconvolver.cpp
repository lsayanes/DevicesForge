#include "SweepDeconvolver.h"

#include "../plugin/DevicesForge.h"
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

		for (int32_t lag = 0; lag <= maxLag; ++lag)
		{
			float corr = 0.0f;
			for (int32_t i = 0; i < compareLen - lag; ++i)
				corr += recorded[static_cast<size_t>(lag + i)] * reference[static_cast<size_t>(i)];

			if (corr > bestCorr)
			{
				bestCorr = corr;
				bestOffset = lag;
			}
		}

		return bestOffset;
	}

	bool SweepDeconvolver::deconvolve(const float* recorded,
									  int32_t recordedLength,
									  const float* reference,
									  int32_t referenceLength,
									  std::vector<float>& irOut,
									  int32_t fftSizeHint)
	{
		if (!recorded || !reference || recordedLength <= 0 || referenceLength <= 0)
			return false;

		const int32_t alignOffset =
			findAlignmentOffset(recorded, recordedLength, reference, referenceLength, referenceLength);

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

		constexpr float kEpsilon = 1e-8f;
		for (int32_t bin = 0; bin < numBins; ++bin)
		{
			const std::complex<float>& den = refSpec[static_cast<size_t>(bin)];
			const float denom = std::norm(den) + kEpsilon;
			irSpec[static_cast<size_t>(bin)] = (recSpec[static_cast<size_t>(bin)] * std::conj(den)) / denom;
		}

		irOut.assign(static_cast<size_t>(fftSize), 0.0f);
		fft.inverse(irSpec.data(), irOut.data(), fftSize);

		int32_t effectiveLength = std::min(alignedLength, fftSize);
		effectiveLength = std::min(effectiveLength, MAX_IR_LENGTH);
		irOut.resize(static_cast<size_t>(effectiveLength));

		return !irOut.empty();
	}

} // DevicesForge
