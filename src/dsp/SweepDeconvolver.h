#pragma once

#include <cstdint>
#include <vector>

namespace DevicesForge
{

	class SweepDeconvolver
	{
	public:
		static bool deconvolve(const float* recorded,
							   int32_t recordedLength,
							   const float* reference,
							   int32_t referenceLength,
							   std::vector<float>& irOut,
							   int32_t fftSizeHint = 0);

	private:
		static int32_t chooseFftSize(int32_t recordedLength, int32_t referenceLength, int32_t hint);
		static int32_t findAlignmentOffset(const float* recorded,
										   int32_t recordedLength,
										   const float* reference,
										   int32_t referenceLength,
										   int32_t maxLag);
	};

} // DevicesForge
