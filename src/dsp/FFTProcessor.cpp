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
		
		return workArea != nullptr;
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

} //DevicesForge
