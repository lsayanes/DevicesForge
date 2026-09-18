#pragma once

#include <vector>
#include <memory>
#include <cstdint>
#include "FFTProcessor.h"
#include "IRManager.h"
#include "../plugin/DevicesForge.h"

namespace DevicesForge 
{

    class DynamicConvolver 
    {
    public:
        DynamicConvolver();
        ~DynamicConvolver();

        void prepare(double sampleRate, int32_t fftSize);
        void reset();

        // Process audio buffer (raw float arrays)
        void process(float** channelBuffers, int32_t numChannels, int32_t numSamples, int32_t irIndex);

        // Set mix (dry/wet)
        void setMix(float mix) { this->mix = mix; }

        // Load IR from file
        bool loadIR(const std::string& filePath, int32_t levelIndex = 0);

        // Multi-level support
        void setCaptureLevels(const std::vector<float>& levels);
        float getCurrentLevel() const { return currentLevelDB; }

        // Enable/disable level-dependent processing
        void setLevelDependent(bool enabled) { levelDependent = enabled; }
        bool isLevelDependent() const { return levelDependent; }

        // IR Management
        inline IRManager& getIRManager() { return irManager; }

    private:
        double sampleRate { 48000.0 };
        int32_t fftSize { FFT_SIZE };
        float mix { 1.0f };
        float currentLevelDB { 0.0f };
        bool levelDependent { false };

        IRManager irManager;
        std::unique_ptr<FFTProcessor> fftProcessor;

        // Convolution buffers
        std::vector<std::complex<float>> irSpectrum;
        std::vector<std::complex<float>> inputSpectrum;
        std::vector<std::complex<float>> outputSpectrum;
        std::vector<float> overlapBuffer;

        // Level detection
        float detectLevel(const float* data, int32_t numSamples);

        // Process single channel
        void processChannel(float* data, int32_t numSamples, int32_t irIndex);
    };

} // DevicesForge
