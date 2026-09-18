#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "../plugin/DevicesForge.h"

namespace DevicesForge
{

	enum class CaptureState : int32_t
	{
		Monitoring = 0,
		RecordingPost = 1,
		Complete = 2
	};

	struct CaptureMetadata
	{
		double sampleRate = SAMPLE_RATE_DEFAULT;
		int64_t triggerSampleIndex = 0;
		int32_t preSamples = 0;
		int32_t postSamples = 0;
		SignalType signalType = SignalType::SineSweep;
		float signalDurationSec = SIGNAL_DURATION_DEFAULT;
	};

	class RingCaptureBuffer
	{
	public:
		void prepare(double sampleRate, int32_t numChannels);
		void reset();

		void push(const float* const* inputs, int32_t numChannels, int32_t numSamples);

		bool trigger(int32_t preSamples, int32_t postSamples, const CaptureMetadata& metadata);

		CaptureState getState() const { return state; }
		bool isComplete() const { return state == CaptureState::Complete; }

		const float* getCapturedMono() const;
		int32_t getCapturedLength() const { return capturedLength; }
		const CaptureMetadata& getMetadata() const { return metadata; }

		float getInputPeak() const { return inputPeak; }

	private:
		float readRingMono(int64_t absoluteIndex) const;
		void writeRingSample(float left, float right);
		void appendPostSample(float mono);

		double sampleRate { SAMPLE_RATE_DEFAULT };
		int32_t channelCount { NUM_CHANNELS };
		int32_t capacitySamples { 0 };

		std::vector<float> ringLeft;
		std::vector<float> ringRight;
		std::vector<float> capturedMono;

		int64_t totalSamplesWritten { 0 };
		int32_t postRemaining { 0 };
		int32_t capturedLength { 0 };

		CaptureState state { CaptureState::Monitoring };
		CaptureMetadata metadata {};
		float inputPeak { 0.0f };
	};

	inline float capturePostDurationSeconds(SignalType type, float signalDurationSec)
	{
		const float tail = CAPTURE_POST_TAIL_SEC;
		if (type == SignalType::Dirac)
			return std::max(1.0f, 1.0f + tail);

		return std::max(1.0f, signalDurationSec + tail);
	}

	inline int32_t secondsToSamples(double sampleRate, float seconds)
	{
		if (sampleRate <= 0.0 || seconds <= 0.0f)
			return 0;
		return static_cast<int32_t>(std::llround(static_cast<double>(seconds) * sampleRate));
	}

} //  DevicesForge
