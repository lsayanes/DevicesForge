#pragma once

#include <vector>
#include <complex>
#include <cstdint>

namespace DevicesForge 
{

	class FFTProcessor 
	{
	public:
		FFTProcessor();
		~FFTProcessor();

		bool prepare(int32_t fftSize);
		void reset();

		// Forward FFT: time domain -> frequency domain
		void forward(const float* timeIn, std::complex<float>* freqOut, int32_t numSamples);

		// Inverse FFT: frequency domain -> time domain
		void inverse(const std::complex<float>* freqIn, float* timeOut, int32_t numSamples);

		// Complex multiply (for convolution)
		void complexMultiply(const std::complex<float>* a, 
							const std::complex<float>* b,
							std::complex<float>* result, 
							int32_t numBins);

		inline int32_t getFFTSize() const { return fftSize; }
		inline int32_t getNumBins() const { return fftSize / 2 + 1; }

	private:
		int32_t fftSize { 0 };
		void* setup { nullptr };
		float* workArea {nullptr };
	};

} //  DevicesForge
