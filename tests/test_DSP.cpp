#include <gtest/gtest.h>
#include <cmath>
#include <vector>

#include "dsp/FFTProcessor.h"
#include "dsp/IRManager.h"
#include "dsp/DynamicConvolver.h"

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
// Main
// ============================================================================

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
