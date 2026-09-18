#include "RingCaptureBuffer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace DevicesForge
{

	void RingCaptureBuffer::prepare(double newSampleRate, int32_t numChannels)
	{
		sampleRate = (newSampleRate > 0.0) ? newSampleRate : SAMPLE_RATE_DEFAULT;
		channelCount = std::max(1, numChannels);
		capacitySamples = secondsToSamples(sampleRate, CAPTURE_MONITOR_SEC);
		if (capacitySamples < 1)
			capacitySamples = 1;

		ringLeft.assign(static_cast<size_t>(capacitySamples), 0.0f);
		ringRight.assign(static_cast<size_t>(capacitySamples), 0.0f);
		capturedMono.clear();

		totalSamplesWritten = 0;
		postRemaining = 0;
		capturedLength = 0;
		state = CaptureState::Monitoring;
		inputPeak = 0.0f;
		metadata = CaptureMetadata {};
		metadata.sampleRate = sampleRate;
	}

	void RingCaptureBuffer::reset()
	{
		totalSamplesWritten = 0;
		postRemaining = 0;
		capturedLength = 0;
		state = CaptureState::Monitoring;
		capturedMono.clear();
		inputPeak = 0.0f;
		std::fill(ringLeft.begin(), ringLeft.end(), 0.0f);
		std::fill(ringRight.begin(), ringRight.end(), 0.0f);
	}

	void RingCaptureBuffer::writeRingSample(float left, float right)
	{
		const int32_t index = static_cast<int32_t>(totalSamplesWritten % static_cast<int64_t>(capacitySamples));
		ringLeft[static_cast<size_t>(index)] = left;
		ringRight[static_cast<size_t>(index)] = right;
		++totalSamplesWritten;
	}

	float RingCaptureBuffer::readRingMono(int64_t absoluteIndex) const
	{
		if (absoluteIndex < 0 || absoluteIndex >= totalSamplesWritten)
			return 0.0f;

		const int64_t oldestKept = totalSamplesWritten - static_cast<int64_t>(capacitySamples);
		if (absoluteIndex < oldestKept)
			return 0.0f;

		const int32_t index = static_cast<int32_t>(absoluteIndex % static_cast<int64_t>(capacitySamples));
		const float left = ringLeft[static_cast<size_t>(index)];
		const float right = ringRight[static_cast<size_t>(index)];
		return 0.5f * (left + right);
	}

	void RingCaptureBuffer::appendPostSample(float mono)
	{
		if (state != CaptureState::RecordingPost || postRemaining <= 0)
			return;

		capturedMono.push_back(mono);
		++capturedLength;
		--postRemaining;

		if (postRemaining <= 0)
			state = CaptureState::Complete;
	}

	void RingCaptureBuffer::push(const float* const* inputs, int32_t numChannels, int32_t numSamples)
	{
		if (numSamples <= 0)
			return;

		inputPeak = 0.0f;

		for (int32_t i = 0; i < numSamples; ++i)
		{
			const float left = (inputs && numChannels > 0 && inputs[0]) ? inputs[0][i] : 0.0f;
			const float right = (inputs && numChannels > 1 && inputs[1]) ? inputs[1][i] : left;
			const float mono = 0.5f * (left + right);

			inputPeak = std::max(inputPeak, std::abs(left));
			inputPeak = std::max(inputPeak, std::abs(right));

			writeRingSample(left, right);

			if (state == CaptureState::RecordingPost)
				appendPostSample(mono);
		}
	}

	bool RingCaptureBuffer::trigger(int32_t preSamples, int32_t postSamples, const CaptureMetadata& meta)
	{
		if (preSamples < 0)
			preSamples = 0;
		
		if (postSamples < 1)
			return false;

		metadata = meta;
		metadata.sampleRate = sampleRate;
		metadata.preSamples = preSamples;
		metadata.postSamples = postSamples;
		metadata.triggerSampleIndex = totalSamplesWritten;

		capturedMono.clear();
		capturedMono.reserve(static_cast<size_t>(preSamples + postSamples));
		capturedLength = 0;

		const int64_t triggerIndex = totalSamplesWritten;
		for (int32_t i = 0; i < preSamples; ++i)
		{
			const int64_t absIndex = triggerIndex - static_cast<int64_t>(preSamples) + static_cast<int64_t>(i);
			capturedMono.push_back(readRingMono(absIndex));
		}
		capturedLength = preSamples;

		postRemaining = postSamples;
		state = CaptureState::RecordingPost;

		return true;
	}

	const float* RingCaptureBuffer::getCapturedMono() const
	{
		if (capturedLength <= 0 || capturedMono.empty())
			return nullptr;
		return capturedMono.data();
	}

} // DevicesForge
