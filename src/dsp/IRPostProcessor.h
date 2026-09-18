#pragma once

#include <cstdint>
#include <vector>

#include "../plugin/DevicesForge.h"

namespace DevicesForge
{

	enum class IRWindowType : int32_t
	{
		Hanning = 0,
		Kaiser = 1
	};

	struct IRPostProcessSettings
	{
		IRWindowType windowType { IRWindowType::Hanning };
		float kaiserBeta { IR_KAISER_BETA_DEFAULT };
		float normalizePeakTarget { IR_NORMALIZE_PEAK_TARGET };
		bool applyWindow { true };
		bool applyNormalize { true };
	};

	class IRPostProcessor
	{
	public:
		static void applyHanning(float* data, int32_t length);
		static void applyKaiser(float* data, int32_t length, float beta);
		static void applyTailFade(float* data, int32_t length, float tailFraction);

		static float normalizePeak(float* data, int32_t length, float targetPeak);

		static void processInPlace(float* data, int32_t length, const IRPostProcessSettings& settings);
		static void process(std::vector<float>& ir, const IRPostProcessSettings& settings);
	};

} // namespace DevicesForge
