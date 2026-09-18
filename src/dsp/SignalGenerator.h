#pragma once

#include <cstdint>
#include <vector>

#include "../plugin/DevicesForge.h"

namespace DevicesForge
{

	class SignalGenerator
	{
	public:
		SignalGenerator() = default;
		~SignalGenerator() = default;

		void prepare(double sampleRate);
		void reset();

		void setType(SignalType type);
		void setDuration(float seconds);

		void start();
		bool isPlaying() const { return playing; }

		void render(float* out, int32_t numSamples);

		const float* getReference() const;
		int32_t getReferenceLength() const { return referenceLength; }

		SignalType getType() const { return type; }
		float getDuration() const { return durationSeconds; }
		double getSampleRate() const { return sampleRate; }

	private:
		void buildReference();
		void generateSweep(int32_t numSamples);
		void generateDirac();
		void generatePink(int32_t numSamples);
		void generateMLS(int32_t numSamples);
		void applyFade(int32_t numSamples, float fadeSeconds);

		double sampleRate { SAMPLE_RATE_DEFAULT };
		SignalType type { SignalType::SineSweep };
		float durationSeconds { SIGNAL_DURATION_DEFAULT };

		std::vector<float> reference;
		int32_t referenceLength { 0 };
		int32_t playIndex { 0 };
		bool playing { false };
	};

} // DevicesForge
