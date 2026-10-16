#include "IRResampler.h"

#include "DFConstants.h"

#include <algorithm>
#include <cmath>

namespace DevicesForge
{

	namespace
	{

		constexpr int32_t kHalfTaps = 16;

		float sinc(float x)
		{
			if (std::fabs(x) < 1.0e-6f)
				return 1.0f;
			const float pix = kPiF * x;
			return std::sin(pix) / pix;
		}

	} // namespace

	bool IRResampler::resample(const float* input,
							   int32_t inputLength,
							   double inputRate,
							   std::vector<float>& output,
							   double outputRate)
	{
		output.clear();
		if (!input || inputLength <= 0 || inputRate <= 0.0 || outputRate <= 0.0)
			return false;

		/* Copio si la tasa ya es la del destino */
		if (std::abs(inputRate - outputRate) < 0.5)
		{
			output.assign(input, input + inputLength);
			return true;
		}

		const double outLenD = static_cast<double>(inputLength) * outputRate / inputRate;
		if (outLenD < 1.0 || outLenD > static_cast<double>(48000 * 60 * 30))
			return false;
		const int32_t outputLength = static_cast<int32_t>(std::llround(outLenD));

		/* Corte: al bajar de tasa, el sinc achica el ancho de banda */
		const float cutoff = static_cast<float>(std::min(1.0, outputRate / inputRate));
		output.assign(static_cast<size_t>(outputLength), 0.0f);

		for (int32_t i = 0; i < outputLength; ++i)
		{
			const float srcPos = static_cast<float>(static_cast<double>(i) * inputRate / outputRate);
			const int32_t center = static_cast<int32_t>(std::floor(srcPos));
			float acc = 0.0f;
			float weightSum = 0.0f;

			/* Sumo el sinc de Hann alrededor de la posicion fuente */
			for (int32_t k = -kHalfTaps; k <= kHalfTaps; ++k)
			{
				const int32_t idx = center + k;
				const float dist = srcPos - static_cast<float>(idx);
				const float windowPos = (static_cast<float>(kHalfTaps) - dist) / static_cast<float>(2 * kHalfTaps);
				if (windowPos <= 0.0f || windowPos >= 1.0f)
					continue;
				const float hann = 0.5f * (1.0f - std::cos(kTwoPiF * windowPos));
				const float weight = sinc(dist * cutoff) * cutoff * hann;
				weightSum += weight;
				if (idx >= 0 && idx < inputLength)
					acc += input[idx] * weight;
			}

			output[static_cast<size_t>(i)] = (weightSum != 0.0f) ? acc / weightSum : 0.0f;
		}

		return true;
	}

} // namespace DevicesForge
