#include "DynamicConvolver.h"
#include <cmath>
#include <algorithm>
#include <cstring>

namespace DevicesForge 
{

    DynamicConvolver::DynamicConvolver() 
    {
        fftProcessor = std::make_unique<FFTProcessor>();
    }

    DynamicConvolver::~DynamicConvolver() 
    {
        reset();
    }

    void DynamicConvolver::prepare(double sampleRate, int32_t fftSize) 
    {

        this->sampleRate = sampleRate;
        this->fftSize = fftSize;

        // Prepare FFT
        fftProcessor->prepare(fftSize);

        // Allocate buffers
        int32_t numBins = fftSize / 2 + 1;
        irSpectrum.resize(numBins);
        inputSpectrum.resize(numBins);
        outputSpectrum.resize(numBins);
        overlapBuffer.resize(fftSize, 0.0f);
    }

    void DynamicConvolver::reset() 
    {
        fftProcessor->reset();
        irSpectrum.clear();
        inputSpectrum.clear();
        outputSpectrum.clear();
        overlapBuffer.clear();
    }

    void DynamicConvolver::process(float** channelBuffers, int32_t numChannels,
                                int32_t numSamples, int32_t irIndex) 
    {
        for (int32_t channel = 0; channel < numChannels; ++channel) 
            processChannel(channelBuffers[channel], numSamples, irIndex);
    }

    void DynamicConvolver::processChannel(float* data, int32_t numSamples,
                                        int32_t irIndex) 
    {
    
        const float* ir = irManager.getIR(irIndex);
        if (!ir) 
            return;

        int32_t irLength = irManager.getIRLength(irIndex);
        if (irLength <= 0) 
            return;

        // Simple overlap-add convolution
        int32_t numBins = fftSize / 2 + 1;

        // Forward FFT of IR (only once, could be cached)
        fftProcessor->forward(ir, reinterpret_cast<std::complex<float>*>(irSpectrum.data()),
                            irLength);

        // Process in blocks
        for (int32_t offset = 0; offset < numSamples; offset += fftSize) 
        {
            int32_t blockSamples = std::min(fftSize, numSamples - offset);

            // Zero-pad input block
            std::vector<float> paddedInput(fftSize, 0.0f);
            std::memcpy(paddedInput.data(), data + offset, 
                    blockSamples * sizeof(float));

            // Forward FFT of input
            fftProcessor->forward(paddedInput.data(),
                                reinterpret_cast<std::complex<float>*>(inputSpectrum.data()),
                                fftSize);

            // Complex multiply in frequency domain
            fftProcessor->complexMultiply(
                irSpectrum.data(),
                inputSpectrum.data(),
                outputSpectrum.data(),
                numBins
            );

            // Inverse FFT
            std::vector<float> outputBlock(fftSize, 0.0f);
            fftProcessor->inverse(
                reinterpret_cast<const std::complex<float>*>(outputSpectrum.data()),
                outputBlock.data(),
                fftSize
            );

            // Overlap-add
            for (int32_t i = 0; i < fftSize && (offset + i) < numSamples; i++) 
            {
                data[offset + i] = data[offset + i] * (1.0f - mix) +
                                (outputBlock[i] + overlapBuffer[i]) * mix;
            }

            // Store overlap
            std::memcpy(overlapBuffer.data(), outputBlock.data() + (fftSize - overlapBuffer.size()),
                    overlapBuffer.size() * sizeof(float));
        }
    }

    float DynamicConvolver::detectLevel(const float* data, int32_t numSamples) 
	{
        float sumSquares = 0.0f;
        for (int32_t i = 0; i < numSamples; ++i) 
            sumSquares += data[i] * data[i];

        float rms = std::sqrt(sumSquares / numSamples);
        float levelDB = 20.0f * std::log10(std::max(rms, 1e-10f));
        
		return levelDB;
    }

    bool DynamicConvolver::loadIR(const std::string& filePath, int32_t levelIndex) 
	{
        return irManager.loadIR(filePath, levelIndex);
    }

    void DynamicConvolver::setCaptureLevels(const std::vector<float>& levels) 
    {
        // This would configure the multi-level capture system
        // For now, just store the levels
    }

} // DevicesForge
