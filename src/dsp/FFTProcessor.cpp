#include "FFTProcessor.h"
#include <pffft/pffft.h>
#include <cstring>
#include <cmath>

namespace DevicesForge 
{

	FFTProcessor::FFTProcessor() {}

	FFTProcessor::~FFTProcessor() 
	{
		reset();
	}

	bool FFTProcessor::prepare(int32_t size) 
	{
		reset();

		// Validate FFT size (must be power of 2)
		if (size <= 0 || (size & (size - 1)) != 0) 
			return false;		

		fftSize = size;
		setup = pffft_new_setup(fftSize, PFFFT_REAL);
		
		if (!setup) 
			return false;

		// Allocate work area (aligned memory)
		workArea = static_cast<float*>(pffft_aligned_malloc(fftSize * sizeof(float)));
		scratchTime = static_cast<float*>(pffft_aligned_malloc(fftSize * sizeof(float)));
		scratchFreq = static_cast<float*>(pffft_aligned_malloc(fftSize * sizeof(float)));

		return workArea != nullptr && scratchTime != nullptr && scratchFreq != nullptr;
	}

	void FFTProcessor::reset() 
	{
		if (setup) 
		{
			pffft_destroy_setup(static_cast<PFFFT_Setup*>(setup));
			setup = nullptr;
		}

		if (workArea) 
		{
			pffft_aligned_free(workArea);
			workArea = nullptr;
		}

		if (scratchTime)
		{
			pffft_aligned_free(scratchTime);
			scratchTime = nullptr;
		}

		if (scratchFreq)
		{
			pffft_aligned_free(scratchFreq);
			scratchFreq = nullptr;
		}

		fftSize = 0;
	}

	void FFTProcessor::forward(const float* timeIn, 
							std::complex<float>* freqOut, 
							int32_t numSamples) 
	{
		if (!setup || !workArea) 
			return;
		
		int32_t samplesToProcess = (numSamples < fftSize) ? numSamples : fftSize;

		// Copy input to aligned buffer
		float* alignedInput = static_cast<float*>(pffft_aligned_malloc(fftSize * sizeof(float)));
		std::memset(alignedInput, 0, fftSize * sizeof(float));
		std::memcpy(alignedInput, timeIn, samplesToProcess * sizeof(float));

		// Perform forward FFT
		pffft_transform_ordered(static_cast<PFFFT_Setup*>(setup),
							alignedInput,
							reinterpret_cast<float*>(freqOut),
							workArea,
							PFFFT_FORWARD);

		pffft_aligned_free(alignedInput);
	}

	void FFTProcessor::inverse(const std::complex<float>* freqIn,
							float* timeOut,
							int32_t numSamples) 
	{
		if (!setup || !workArea) 
			return;

		int32_t samplesToProcess = (numSamples < fftSize) ? numSamples : fftSize;

		// Perform inverse FFT
		pffft_transform_ordered(static_cast<PFFFT_Setup*>(setup),
							reinterpret_cast<const float*>(freqIn),
							timeOut,
							workArea,
							PFFFT_BACKWARD);

		// Scale by 1/N
		float scale = 1.0f / static_cast<float>(fftSize);
		for (int32_t i = 0; i < samplesToProcess; i++) 
			timeOut[i] *= scale;
	}

	void FFTProcessor::complexMultiply(const std::complex<float>* a,
									const std::complex<float>* b,
									std::complex<float>* result,
									int32_t numBins) 
	{
		for (int32_t i = 0; i < numBins; i++) 
			result[i] = a[i] * b[i];
	}

	void FFTProcessor::forwardRaw(const float* timeIn, int32_t numSamples, float* freqOut)
	{
		if (!setup || !workArea || !scratchTime || !scratchFreq)
			return;

		const int32_t n = (numSamples < fftSize) ? numSamples : fftSize;
		std::memset(scratchTime, 0, fftSize * sizeof(float));
		std::memcpy(scratchTime, timeIn, n * sizeof(float));

		pffft_transform(static_cast<PFFFT_Setup*>(setup), scratchTime, scratchFreq,
						workArea, PFFFT_FORWARD);

		std::memcpy(freqOut, scratchFreq, fftSize * sizeof(float));
	}

	void FFTProcessor::inverseRaw(const float* freqIn, float* timeOut)
	{
		if (!setup || !workArea || !scratchTime || !scratchFreq)
			return;

		std::memcpy(scratchFreq, freqIn, fftSize * sizeof(float));

		pffft_transform(static_cast<PFFFT_Setup*>(setup), scratchFreq, scratchTime,
						workArea, PFFFT_BACKWARD);

		const float scale = 1.0f / static_cast<float>(fftSize);
		for (int32_t i = 0; i < fftSize; ++i)
			timeOut[i] = scratchTime[i] * scale;
	}

	void FFTProcessor::convolveAccumulate(const float* specA, const float* specB,
										float* accum) const
	{
		if (!setup)
			return;

		pffft_zconvolve_accumulate(static_cast<PFFFT_Setup*>(setup), specA, specB,
								accum, 1.0f);
	}

} //DevicesForge
