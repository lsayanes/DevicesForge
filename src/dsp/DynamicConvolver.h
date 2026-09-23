#pragma once

#include <vector>
#include <memory>
#include <cstdint>
#include "FFTProcessor.h"
#include "IRManager.h"
#include "../plugin/DevicesForge.h"

namespace DevicesForge 
{

    /**
     * Convolución uniforme particionada (overlap-save) para emular el
     * dispositivo capturado en tiempo real.
     *
     * - fftSize N → bloque interno B = N/2; latencia = B samples.
     * - IR particionada en chunks de B; espectros cacheados hasta que la IR
     *   cambie (IRManager::getRevision) o cambie irIndex.
     * - Estado (FIFO, FDL, historia) independiente por canal.
     * - Mix dry/wet en el dominio del tiempo con dry retrasado B samples,
     *   así dry y wet quedan alineados (sin comb filtering).
     *
     * Ver docs/DYNAMIC-CONVOLUTION-MIX.md.
     */
    class DynamicConvolver 
    {
    public:
        DynamicConvolver();
        ~DynamicConvolver();

        void prepare(double sampleRate, int32_t fftSize);
        void reset();

        /** Procesa in-place. Devuelve false (buffers intactos) si no hay IR.
            irIndex se limita a los niveles disponibles en IRManager. */
        bool process(float** channelBuffers, int32_t numChannels,
                     int32_t numSamples, int32_t irIndex);

        // Set mix (dry/wet), 0..1
        void setMix(float mix) { this->mix = mix; }

        /** Latencia del path convolucionado, en samples (= bloque interno). */
        int32_t getLatencySamples() const { return blockSize; }

        // Load IR from file
        bool loadIR(const std::string& filePath, int32_t levelIndex = 0);

        // IR Management
        inline IRManager& getIRManager() { return irManager; }

    private:
        struct ChannelState
        {
            std::vector<float> inFifo;               // B samples (dry del bloque actual)
            std::vector<float> outFifo;              // B samples listos para salida
            std::vector<float> timeBuf;              // N samples: [bloque previo | actual]
            std::vector<std::vector<float>> fdl;     // frequency-delay line: P espectros raw
            int32_t fdlIndex { 0 };
        };

        void rebuildPartitions(int32_t irIndex);
        void processBlock(ChannelState& channel);

        double sampleRate { 48000.0 };
        int32_t fftSize { CONV_FFT_SIZE };
        int32_t blockSize { CONV_FFT_SIZE / 2 };
        float mix { 1.0f };
        int32_t fifoPos { 0 };

        std::vector<ChannelState> channels;
        std::vector<std::vector<float>> irPartitions;  // P espectros raw de la IR
        int32_t cachedIrIndex { -1 };
        uint64_t cachedRevision { ~0ull };

        std::vector<float> accumBuf;   // acumulador en frecuencia (N floats)
        std::vector<float> ifftBuf;    // salida temporal de la IFFT (N floats)

        IRManager irManager;
        std::unique_ptr<FFTProcessor> fftProcessor;
    };

} // DevicesForge
