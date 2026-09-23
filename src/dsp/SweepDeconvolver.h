#pragma once

#include <cstdint>
#include <vector>

#include "../plugin/DevicesForge.h"

namespace DevicesForge
{

	class SweepDeconvolver
	{
	public:
		/** sampleRate se usa para limitar la IR a la banda del sweep. */
		static bool deconvolve(const float* recorded,
							   int32_t recordedLength,
							   const float* reference,
							   int32_t referenceLength,
							   std::vector<float>& irOut,
							   int32_t fftSizeHint = 0,
							   double sampleRate = SAMPLE_RATE_DEFAULT);

	private:
		/** Peso 0..1 por bin: 1 dentro de la banda del sweep, 0 fuera,
		    con transición suave para no introducir ringing. */
		static float bandWeight(double frequencyHz, double sampleRate);

		static int32_t chooseFftSize(int32_t recordedLength, int32_t referenceLength, int32_t hint);
		static int32_t findAlignmentOffset(const float* recorded,
										   int32_t recordedLength,
										   const float* reference,
										   int32_t referenceLength,
										   int32_t maxLag);
	};

} // DevicesForge
