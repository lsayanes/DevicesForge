#include "VuMeter.h"

#include <algorithm>
#include <cmath>

namespace DevicesForge
{

	void VuMeter::prepare(double rate, float referenceDbfs)
	{
		/* Guardo el paso de tiempo y la ganancia hasta 0 VU */
		sampleRate = rate > 0.0 ? rate : 48000.0;
		dt = 1.0 / sampleRate;
		const double reference = std::pow(10.0, static_cast<double>(referenceDbfs) / 20.0);
		drive = VU_RECTIFIED_TO_RMS / reference;
		reset();
	}

	void VuMeter::reset()
	{
		position = 0.0;
		velocity = 0.0;
		peak = 0.0f;
	}

	void VuMeter::process(const float* const* channels, int32_t numChannels, int32_t numSamples)
	{
		if (!channels || numChannels <= 0 || numSamples <= 0)
			return;

		const double omega2 = VU_NATURAL_RAD_S * VU_NATURAL_RAD_S;
		const double brake = 2.0 * VU_DAMPING * VU_NATURAL_RAD_S;
		const double top = VU_NEEDLE_MAX;
		float blockPeak = 0.0f;

		for (int32_t i = 0; i < numSamples; ++i)
		{
			/* Tomo el canal mas alto: con un solo lado la lectura no baja 6 dB */
			float level = 0.0f;
			for (int32_t c = 0; c < numChannels; ++c)
			{
				if (channels[c])
					level = std::max(level, std::abs(channels[c][i]));
			}
			blockPeak = std::max(blockPeak, level);

			/* Muevo la aguja (Euler semi-implicito: estable con dt de audio) */
			velocity += dt * (omega2 * (drive * level - position) - brake * velocity);
			position += dt * velocity;

			/* La freno en los topes como el instrumento */
			if (position < 0.0)
			{
				position = 0.0;
				velocity = std::max(velocity, 0.0);
			}
			else if (position > top)
			{
				position = top;
				velocity = std::min(velocity, 0.0);
			}
		}

		/* Retengo el pico con caida exponencial */
		const float release = static_cast<float>(
			std::exp(-static_cast<double>(numSamples) / (sampleRate * VU_PEAK_RELEASE_SEC)));
		peak = std::max(blockPeak, peak * release);
	}

} // namespace DevicesForge
