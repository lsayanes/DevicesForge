#include <gtest/gtest.h>
#include <cmath>
#include <complex>
#include <memory>
#include <vector>

#include "dsp/FFTProcessor.h"
#include "dsp/IRManager.h"
#include "dsp/DynamicConvolver.h"
#include "dsp/SignalGenerator.h"

using namespace DevicesForge;

// ============================================================================
// FFTProcessor Tests
// ============================================================================

class FFTProcessorTest : public ::testing::Test {
protected:
    void SetUp() override {
        fft = std::make_unique<FFTProcessor>();
    }

    void TearDown() override {
        fft.reset();
    }

    std::unique_ptr<FFTProcessor> fft;
};

TEST_F(FFTProcessorTest, PrepareValidSize) {
    EXPECT_TRUE(fft->prepare(1024));
    EXPECT_EQ(fft->getFFTSize(), 1024);
    EXPECT_EQ(fft->getNumBins(), 513);
}

TEST_F(FFTProcessorTest, PrepareInvalidSize) {
    EXPECT_FALSE(fft->prepare(100));  // Not power of 2
    EXPECT_FALSE(fft->prepare(0));
    EXPECT_FALSE(fft->prepare(-1));
}

TEST_F(FFTProcessorTest, ForwardInverse) {
    ASSERT_TRUE(fft->prepare(1024));

    // Create test signal
    std::vector<float> input(1024);
    for (int i = 0; i < 1024; ++i) {
        input[i] = std::sin(2.0f * M_PI * 440.0f * i / 48000.0f);
    }

    // Forward FFT
    std::vector<std::complex<float>> freqDomain(513);
    fft->forward(input.data(), freqDomain.data(), 1024);

    // Inverse FFT
    std::vector<float> output(1024);
    fft->inverse(freqDomain.data(), output.data(), 1024);

    // Verify roundtrip (allow for scaling differences)
    float maxError = 0.0f;
    for (int i = 0; i < 1024; ++i) {
        float error = std::abs(input[i] - output[i]);
        maxError = std::max(maxError, error);
    }
    EXPECT_LT(maxError, 0.01f);
}

TEST_F(FFTProcessorTest, ComplexMultiply) {
    ASSERT_TRUE(fft->prepare(1024));

    int numBins = 513;
    std::vector<std::complex<float>> a(numBins, {1.0f, 0.0f});
    std::vector<std::complex<float>> b(numBins, {2.0f, 0.0f});
    std::vector<std::complex<float>> result(numBins);

    fft->complexMultiply(a.data(), b.data(), result.data(), numBins);

    for (int i = 0; i < numBins; ++i) {
        EXPECT_FLOAT_EQ(result[i].real(), 2.0f);
        EXPECT_FLOAT_EQ(result[i].imag(), 0.0f);
    }
}

// ============================================================================
// IRManager Tests
// ============================================================================

class IRManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        irManager = std::make_unique<IRManager>();
    }

    void TearDown() override {
        irManager.reset();
    }

    std::unique_ptr<IRManager> irManager;
};

TEST_F(IRManagerTest, GenerateTestIR) {
    irManager->generateTestIR(1024, 1000.0f);

    EXPECT_EQ(irManager->getNumLevels(), 1);
    EXPECT_EQ(irManager->getIRLength(0), 1024);
    EXPECT_NE(irManager->getIR(0), nullptr);
}

TEST_F(IRManagerTest, Clear) {
    irManager->generateTestIR(1024);
    EXPECT_EQ(irManager->getNumLevels(), 1);

    irManager->clear();
    EXPECT_EQ(irManager->getNumLevels(), 0);
}

TEST_F(IRManagerTest, MultipleLevels) {
    irManager->generateTestIR(512);
    irManager->generateTestIR(512);

    EXPECT_TRUE(irManager->hasMultipleLevels());
    EXPECT_EQ(irManager->getNumLevels(), 2);
}

// ============================================================================
// DynamicConvolver Tests
// ============================================================================

class DynamicConvolverTest : public ::testing::Test {
protected:
    void SetUp() override {
        convolver = std::make_unique<DynamicConvolver>();
        convolver->prepare(48000.0, 1024);
    }

    void TearDown() override {
        convolver.reset();
    }

    std::unique_ptr<DynamicConvolver> convolver;
};

TEST_F(DynamicConvolverTest, SetMix) {
    convolver->setMix(0.5f);
    // No assertion needed - just verify no crash
}

TEST_F(DynamicConvolverTest, LoadIR) {
    // Generate a test IR (prepare already generated one, so we'll have 2)
    convolver->getIRManager().generateTestIR(1024);
    
    // Loading from generated IR should work (2 levels total)
    EXPECT_EQ(convolver->getIRManager().getNumLevels(), 2);
}

// ============================================================================
// SignalGenerator Tests
// ============================================================================

class SignalGeneratorTest : public ::testing::Test {
protected:
    void SetUp() override {
        gen = std::make_unique<SignalGenerator>();
        gen->prepare(48000.0);
    }

    static int countZeroCrossings(const float* x, int n) {
        int count = 0;
        for (int i = 1; i < n; ++i) {
            if ((x[i - 1] < 0.0f && x[i] >= 0.0f) || (x[i - 1] >= 0.0f && x[i] < 0.0f)) {
                ++count;
            }
        }
        return count;
    }

    std::unique_ptr<SignalGenerator> gen;
};

TEST_F(SignalGeneratorTest, SweepDurationMatchesSampleCount) {
    gen->setType(SignalType::SineSweep);
    gen->setDuration(1.0f);
    gen->start();

    ASSERT_EQ(gen->getReferenceLength(), 48000);
    EXPECT_TRUE(gen->isPlaying());

    std::vector<float> block(256);
    int32_t rendered = 0;
    while (gen->isPlaying()) {
        gen->render(block.data(), 256);
        rendered += 256;
    }

    EXPECT_FALSE(gen->isPlaying());
    EXPECT_GE(rendered, 48000);
    EXPECT_LT(rendered, 48000 + 256);
}

TEST_F(SignalGeneratorTest, SweepRisesInFrequency) {
    gen->setType(SignalType::SineSweep);
    gen->setDuration(1.0f);
    gen->start();

    const float* ir = gen->getReference();
    ASSERT_NE(ir, nullptr);

    const int window = 4800; // 100 ms
    const int firstStart = 2400;
    const int lastStart = 48000 - 2400 - window;
    const int firstZC = countZeroCrossings(ir + firstStart, window);
    const int lastZC = countZeroCrossings(ir + lastStart, window);
    EXPECT_GT(lastZC, firstZC * 4);
}

TEST_F(SignalGeneratorTest, DiracIsSingleSample) {
    gen->setType(SignalType::Dirac);
    gen->setDuration(2.0f);
    gen->start();

    ASSERT_EQ(gen->getReferenceLength(), 1);
    ASSERT_NE(gen->getReference(), nullptr);
    EXPECT_FLOAT_EQ(gen->getReference()[0], 1.0f);

    std::vector<float> block(64, 99.0f);
    gen->render(block.data(), 64);
    EXPECT_FLOAT_EQ(block[0], 1.0f);
    for (int i = 1; i < 64; ++i) {
        EXPECT_FLOAT_EQ(block[i], 0.0f);
    }
    EXPECT_FALSE(gen->isPlaying());
}

TEST_F(SignalGeneratorTest, PinkHasMoreLowEnergyThanHigh) {
    gen->setType(SignalType::PinkNoise);
    gen->setDuration(1.0f);
    gen->start();

    ASSERT_EQ(gen->getReferenceLength(), 48000);
    const float* ir = gen->getReference();
    ASSERT_NE(ir, nullptr);

    FFTProcessor fft;
    ASSERT_TRUE(fft.prepare(4096));
    std::vector<std::complex<float>> spectrum(2049);
    fft.forward(ir, spectrum.data(), 4096);

    double lowEnergy = 0.0;
    double highEnergy = 0.0;
    const double binHz = 48000.0 / 4096.0;
    for (int i = 1; i < 2049; ++i) {
        const double mag2 = static_cast<double>(std::norm(spectrum[static_cast<size_t>(i)]));
        const double hz = static_cast<double>(i) * binHz;
        if (hz >= 20.0 && hz <= 500.0)
            lowEnergy += mag2;
        else if (hz >= 8000.0 && hz <= 16000.0)
            highEnergy += mag2;
    }

    EXPECT_GT(lowEnergy, highEnergy);
}

TEST_F(SignalGeneratorTest, MLSIsBipolarWithPeriod) {
    gen->setType(SignalType::MLS);
    gen->setDuration(3.0f);
    gen->start();

    const int32_t length = gen->getReferenceLength();
    ASSERT_GT(length, MLS_PERIOD * 2);
    const float* ir = gen->getReference();
    ASSERT_NE(ir, nullptr);

    for (int32_t i = 0; i < length; ++i) {
        EXPECT_TRUE(ir[i] == 1.0f || ir[i] == -1.0f);
    }

    int matches = 0;
    for (int32_t i = 0; i < MLS_PERIOD; ++i) {
        if (ir[i] == ir[i + MLS_PERIOD])
            ++matches;
    }
    EXPECT_EQ(matches, MLS_PERIOD);
}

TEST_F(SignalGeneratorTest, IdleRenderIsSilence) {
    std::vector<float> block(32, 1.0f);
    gen->render(block.data(), 32);
    for (float s : block) {
        EXPECT_FLOAT_EQ(s, 0.0f);
    }
    EXPECT_FALSE(gen->isPlaying());
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
