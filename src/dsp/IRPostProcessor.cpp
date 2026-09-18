#include "IRPostProcessor.h"

#include <algorithm>
#include <cmath>

namespace DevicesForge
{

	namespace
	{
		double besselI0(double x)
		{
			double sum = 1.0;
			double term = 1.0;
			for (int k = 1; k <= 20; ++k)
			{
				term *= (x * x) / (4.0 * static_cast<double>(k) * static_cast<double>(k));
				sum += term;
			}
			return sum;
		}
	}

	void IRPostProcessor::applyHanning(float* data, int32_t length)
	{
		if (!data || length <= 0)
			return;
		if (length == 1)
		{
			data[0] = 0.0f;
			return;
		}

		for (int32_t n = 0; n < length; ++n)
		{
			const double w = 0.5 * (1.0 - std::cos(2.0 * kPi * static_cast<double>(n) /
												  static_cast<double>(length - 1)));
			data[static_cast<size_t>(n)] *= static_cast<float>(w);
		}
	}

	void IRPostProcessor::applyKaiser(float* data, int32_t length, float beta)
	{
		if (!data || length <= 0)
			return;
		if (length == 1)
		{
			data[0] = 0.0f;
			return;
		}

		const double denom = besselI0(static_cast<double>(beta));
		const double alpha = (length - 1) * 0.5;

		for (int32_t n = 0; n < length; ++n)
		{
			const double x = static_cast<double>(n) - alpha;
			const double arg = beta * std::sqrt(std::max(0.0, 1.0 - (x / alpha) * (x / alpha)));
			const double w = besselI0(arg) / denom;
			data[static_cast<size_t>(n)] *= static_cast<float>(w);
		}
	}

	float IRPostProcessor::normalizePeak(float* data, int32_t length, float targetPeak)
	{
		if (!data || length <= 0 || targetPeak <= 0.0f)
			return 0.0f;

		float peak = 0.0f;
		for (int32_t i = 0; i < length; ++i)
			peak = std::max(peak, std::abs(data[static_cast<size_t>(i)]));

		if (peak <= 0.0f)
			return 0.0f;

		const float scale = targetPeak / peak;
		for (int32_t i = 0; i < length; ++i)
			data[static_cast<size_t>(i)] *= scale;

		return peak;
	}

	void IRPostProcessor::processInPlace(float* data, int32_t length, const IRPostProcessSettings& settings)
	{
		if (!data || length <= 0)
			return;

		if (settings.applyWindow)
		{
			switch (settings.windowType)
			{
				case IRWindowType::Kaiser:
					applyKaiser(data, length, settings.kaiserBeta);
					break;
				case IRWindowType::Hanning:
				default:
					applyHanning(data, length);
					break;
			}
		}

		if (settings.applyNormalize)
			normalizePeak(data, length, settings.normalizePeakTarget);
	}

	void IRPostProcessor::process(std::vector<float>& ir, const IRPostProcessSettings& settings)
	{
		if (ir.empty())
			return;
		processInPlace(ir.data(), static_cast<int32_t>(ir.size()), settings);
	}

} //  DevicesForge
