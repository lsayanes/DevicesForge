#include "SignalGenerator.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>

namespace DevicesForge
{

	namespace
	{
		constexpr double kPi = 3.14159265358979323846;
		constexpr float kSweepFadeSeconds = 0.010f;

		int32_t durationToSamples(float seconds, double sampleRate)
		{
			if (seconds <= 0.0f || sampleRate <= 0.0)
				return 0;
			return static_cast<int32_t>(std::llround(static_cast<double>(seconds) * sampleRate));
		}
	}

	void SignalGenerator::prepare(double newSampleRate)
	{
		sampleRate = (newSampleRate > 0.0) ? newSampleRate : SAMPLE_RATE_DEFAULT;
		const int32_t maxSamples = durationToSamples(SIGNAL_DURATION_MAX, sampleRate) + 16;
		reference.assign(static_cast<size_t>(std::max(maxSamples, MLS_PERIOD + 1)), 0.0f);
		referenceLength = 0;
		playIndex = 0;
		playing = false;
	}

	void SignalGenerator::reset()
	{
		playing = false;
		playIndex = 0;
	}

	void SignalGenerator::setType(SignalType newType)
	{
		type = newType;
	}

	void SignalGenerator::setDuration(float seconds)
	{
		durationSeconds = std::clamp(seconds, SIGNAL_DURATION_MIN, SIGNAL_DURATION_MAX);
	}

	void SignalGenerator::start()
	{
		if (reference.empty())
			prepare(sampleRate);

		buildReference();
		playIndex = 0;
		playing = referenceLength > 0;
	}

	void SignalGenerator::render(float* out, int32_t numSamples)
	{
		if (!out || numSamples <= 0)
			return;

		if (!playing || referenceLength <= 0)
		{
			std::memset(out, 0, static_cast<size_t>(numSamples) * sizeof(float));
			return;
		}

		int32_t i = 0;
		while (i < numSamples && playIndex < referenceLength)
		{
			out[i++] = reference[static_cast<size_t>(playIndex++)];
		}

		if (i < numSamples)
			std::memset(out + i, 0, static_cast<size_t>(numSamples - i) * sizeof(float));

		if (playIndex >= referenceLength)
			playing = false;
	}

	const float* SignalGenerator::getReference() const
	{
		if (referenceLength <= 0 || reference.empty())
			return nullptr;
		return reference.data();
	}

	void SignalGenerator::buildReference()
	{
		if (type == SignalType::Dirac)
		{
			generateDirac();
			return;
		}

		int32_t numSamples = durationToSamples(durationSeconds, sampleRate);
		if (numSamples < 1)
			numSamples = 1;

		if (static_cast<int32_t>(reference.size()) < numSamples)
			reference.resize(static_cast<size_t>(numSamples), 0.0f);

		switch (type)
		{
			case SignalType::SineSweep:
				generateSweep(numSamples);
				break;
			case SignalType::PinkNoise:
				generatePink(numSamples);
				break;
			case SignalType::MLS:
				generateMLS(numSamples);
				break;
			case SignalType::Dirac:
			default:
				generateDirac();
				break;
		}
	}

	void SignalGenerator::generateSweep(int32_t numSamples)
	{
		referenceLength = numSamples;

		const double f1 = static_cast<double>(SWEEP_FREQ_START_HZ);
		double f2 = static_cast<double>(SWEEP_FREQ_END_HZ);
		const double nyquistCap = sampleRate * 0.45;
		if (f2 > nyquistCap)
			f2 = nyquistCap;

		const double T = static_cast<double>(numSamples) / sampleRate;
		const double R = std::log(f2 / f1);
		const double K = 2.0 * kPi * f1 * T / R;

		for (int32_t i = 0; i < numSamples; ++i)
		{
			const double t = static_cast<double>(i) / sampleRate;
			const double phase = K * (std::exp((t / T) * R) - 1.0);
			reference[static_cast<size_t>(i)] = static_cast<float>(std::sin(phase));
		}

		applyFade(numSamples, kSweepFadeSeconds);
	}

	void SignalGenerator::generateDirac()
	{
		if (reference.empty())
			reference.resize(1, 0.0f);

		reference[0] = 1.0f;
		referenceLength = 1;
	}

	void SignalGenerator::generatePink(int32_t numSamples)
	{
		referenceLength = numSamples;

		// Paul Kellet "economy" pink filter on white noise (fixed seed for repeatable captures).
		std::mt19937 rng(0xDF20u);
		std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

		float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f, b3 = 0.0f, b4 = 0.0f, b5 = 0.0f, b6 = 0.0f;
		float peak = 0.0f;

		for (int32_t i = 0; i < numSamples; ++i)
		{
			const float white = dist(rng);
			b0 = 0.99886f * b0 + white * 0.0555179f;
			b1 = 0.99332f * b1 + white * 0.0750759f;
			b2 = 0.96900f * b2 + white * 0.1538520f;
			b3 = 0.86650f * b3 + white * 0.3104856f;
			b4 = 0.55000f * b4 + white * 0.5329522f;
			b5 = -0.7616f * b5 - white * 0.0168980f;
			const float pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362f;
			b6 = white * 0.115926f;
			reference[static_cast<size_t>(i)] = pink;
			peak = std::max(peak, std::abs(pink));
		}

		if (peak > 0.0f)
		{
			const float scale = 0.95f / peak;
			for (int32_t i = 0; i < numSamples; ++i)
				reference[static_cast<size_t>(i)] *= scale;
		}
	}

	void SignalGenerator::generateMLS(int32_t numSamples)
	{
		referenceLength = numSamples;

		// 16-bit Galois LFSR, polynomial 0xB400 (period 65535).
		uint16_t lfsr = 0xACE1u;
		if (lfsr == 0)
			lfsr = 1;

		for (int32_t i = 0; i < numSamples; ++i)
		{
			const uint16_t lsb = static_cast<uint16_t>(lfsr & 1u);
			lfsr = static_cast<uint16_t>(lfsr >> 1);
			if (lsb)
				lfsr = static_cast<uint16_t>(lfsr ^ 0xB400u);
			reference[static_cast<size_t>(i)] = lsb ? 1.0f : -1.0f;
		}
	}

	void SignalGenerator::applyFade(int32_t numSamples, float fadeSeconds)
	{
		int32_t fadeSamples = durationToSamples(fadeSeconds, sampleRate);
		if (fadeSamples < 1)
			fadeSamples = 1;
		if (fadeSamples * 2 > numSamples)
			fadeSamples = std::max(1, numSamples / 10);

		for (int32_t i = 0; i < fadeSamples; ++i)
		{
			const float env = 0.5f * (1.0f - static_cast<float>(std::cos(kPi * static_cast<double>(i) /
																		static_cast<double>(fadeSamples))));
			reference[static_cast<size_t>(i)] *= env;
			reference[static_cast<size_t>(numSamples - 1 - i)] *= env;
		}
	}

} // namespace DevicesForge
