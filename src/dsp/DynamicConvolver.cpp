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
        this->blockSize = fftSize / 2;

        fftProcessor->prepare(fftSize);

        channels.assign(NUM_CHANNELS, ChannelState {});
        for (auto& channel : channels)
        {
            channel.inFifo.assign(blockSize, 0.0f);
            channel.outFifo.assign(blockSize, 0.0f);
            channel.timeBuf.assign(fftSize, 0.0f);
            channel.fdl.clear();
            channel.fdlIndex = 0;
        }

        fifoPos = 0;
        accumBuf.assign(fftSize, 0.0f);
        ifftBuf.assign(fftSize, 0.0f);

        irPartitions.clear();
        cachedIrIndex = -1;
        cachedRevision = ~0ull;
    }

    void DynamicConvolver::reset() 
    {
        fftProcessor->reset();
        channels.clear();
        irPartitions.clear();
        accumBuf.clear();
        ifftBuf.clear();
        fifoPos = 0;
        cachedIrIndex = -1;
        cachedRevision = ~0ull;
    }

    bool DynamicConvolver::process(float** channelBuffers, int32_t numChannels,
                                int32_t numSamples, int32_t irIndex) 
    {
        if (!fftProcessor || blockSize <= 0 || channels.empty() || !channelBuffers)
            return false;

        const int32_t numLevels = irManager.getNumLevels();
        if (numLevels <= 0)
            return false;

        if (irIndex < 0) irIndex = 0;
        if (irIndex >= numLevels) irIndex = numLevels - 1;

        if (irIndex != cachedIrIndex || irManager.getRevision() != cachedRevision)
            rebuildPartitions(irIndex);

        if (irPartitions.empty())
            return false;

        if (numChannels > static_cast<int32_t>(channels.size()))
            numChannels = static_cast<int32_t>(channels.size());
        if (numChannels <= 0)
            return false;

        for (int32_t i = 0; i < numSamples; ++i)
        {
            for (int32_t ch = 0; ch < numChannels; ++ch)
            {
                if (!channelBuffers[ch])
                    continue;

                ChannelState& channel = channels[ch];
                const float dry = channelBuffers[ch][i];
                channelBuffers[ch][i] = channel.outFifo[fifoPos];
                channel.inFifo[fifoPos] = dry;
            }

            if (++fifoPos == blockSize)
            {
                for (int32_t ch = 0; ch < numChannels; ++ch)
                    processBlock(channels[ch]);
                fifoPos = 0;
            }
        }

        return true;
    }

    void DynamicConvolver::processBlock(ChannelState& channel)
    {
        const int32_t B = blockSize;
        const int32_t numPartitions = static_cast<int32_t>(irPartitions.size());

        // Overlap-save: [bloque previo | bloque actual]
        std::memmove(channel.timeBuf.data(), channel.timeBuf.data() + B,
                     static_cast<size_t>(B) * sizeof(float));
        std::memcpy(channel.timeBuf.data() + B, channel.inFifo.data(),
                    static_cast<size_t>(B) * sizeof(float));

        if (static_cast<int32_t>(channel.fdl.size()) != numPartitions)
        {
            channel.fdl.assign(numPartitions, std::vector<float>(fftSize, 0.0f));
            channel.fdlIndex = 0;
        }

        channel.fdlIndex = (channel.fdlIndex + 1) % numPartitions;
        fftProcessor->forwardRaw(channel.timeBuf.data(), fftSize,
                                 channel.fdl[channel.fdlIndex].data());

        std::fill(accumBuf.begin(), accumBuf.end(), 0.0f);
        for (int32_t p = 0; p < numPartitions; ++p)
        {
            const int32_t slot = (channel.fdlIndex - p + numPartitions) % numPartitions;
            fftProcessor->convolveAccumulate(channel.fdl[slot].data(),
                                             irPartitions[p].data(),
                                             accumBuf.data());
        }

        fftProcessor->inverseRaw(accumBuf.data(), ifftBuf.data());

        // Wet válido = últimos B samples; dry retrasado B para quedar alineado.
        const float wetGain = mix;
        const float dryGain = 1.0f - mix;
        for (int32_t i = 0; i < B; ++i)
            channel.outFifo[i] = channel.inFifo[i] * dryGain + ifftBuf[B + i] * wetGain;
    }

    void DynamicConvolver::rebuildPartitions(int32_t irIndex)
    {
        irPartitions.clear();
        cachedIrIndex = irIndex;
        cachedRevision = irManager.getRevision();

        const float* ir = irManager.getIR(irIndex);
        int32_t irLength = irManager.getIRLength(irIndex);
        if (!ir || irLength <= 0 || blockSize <= 0)
            return;

        if (irLength > MAX_IR_LENGTH)
            irLength = MAX_IR_LENGTH;

        const int32_t numPartitions = (irLength + blockSize - 1) / blockSize;
        irPartitions.resize(numPartitions);
        for (int32_t p = 0; p < numPartitions; ++p)
        {
            const int32_t start = p * blockSize;
            const int32_t chunk = std::min(blockSize, irLength - start);
            irPartitions[p].assign(fftSize, 0.0f);
            fftProcessor->forwardRaw(ir + start, chunk, irPartitions[p].data());
        }

        for (auto& channel : channels)
        {
            channel.fdl.assign(numPartitions, std::vector<float>(fftSize, 0.0f));
            channel.fdlIndex = 0;
        }
    }

    bool DynamicConvolver::loadIR(const std::string& filePath, int32_t levelIndex) 
	{
        return irManager.loadIR(filePath, levelIndex);
    }

} // DevicesForge
