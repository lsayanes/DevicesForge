#include <gtest/gtest.h>
#include <cmath>
#include <complex>
#include <fstream>
#include <memory>
#include <vector>

#include "dsp/FFTProcessor.h"
#include "dsp/IRManager.h"
#include "dsp/DynamicConvolver.h"
#include "dsp/SignalGenerator.h"
#include "dsp/RingCaptureBuffer.h"
#include "dsp/IRPostProcessor.h"
#include "dsp/SweepDeconvolver.h"
#include "dsp/IRExporter.h"

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
        input[i] = std::sin(kTwoPiF * 440.0f * i / 48000.0f);
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
    EXPECT_EQ(convolver->getIRManager().getNumLevels(), 1);
}

TEST_F(DynamicConvolverTest, NoIRLeavesBufferUntouched) {
    std::vector<float> left(256, 0.5f);
    float* bufs[1] = { left.data() };

    EXPECT_FALSE(convolver->process(bufs, 1, 256, 0));
    for (float v : left)
        EXPECT_FLOAT_EQ(v, 0.5f);
}

// Con IR = delta (1,0,0,...) y mix=1 la salida es la entrada retrasada
// exactamente getLatencySamples() (bloque interno del overlap-save).
TEST_F(DynamicConvolverTest, DeltaIRIsIdentityWithLatency) {
    std::vector<float> delta(64, 0.0f);
    delta[0] = 1.0f;
    convolver->getIRManager().setLevelIR(0, delta, 0.0f);
    convolver->setMix(1.0f);

    const int32_t latency = convolver->getLatencySamples();
    const int32_t total = latency * 4;
    std::vector<float> input(total), output(total);
    for (int32_t i = 0; i < total; ++i)
        input[i] = std::sin(0.05f * static_cast<float>(i));
    output = input;

    // Procesar en bloques chicos (simula al host)
    const int32_t hostBlock = 128;
    for (int32_t offset = 0; offset < total; offset += hostBlock) {
        float* bufs[1] = { output.data() + offset };
        ASSERT_TRUE(convolver->process(bufs, 1,
                                       std::min(hostBlock, total - offset), 0));
    }

    for (int32_t i = 0; i < total - latency; ++i)
        EXPECT_NEAR(output[i + latency], input[i], 1e-3f) << "sample " << i;
}

// Mix = 0 → solo dry (también retrasado por el FIFO interno).
TEST_F(DynamicConvolverTest, MixZeroOutputsDelayedDry) {
    std::vector<float> ir(32, 0.0f);
    ir[0] = 0.25f;  // IR atenuada: si se colara wet, cambia la amplitud
    convolver->getIRManager().setLevelIR(0, ir, 0.0f);
    convolver->setMix(0.0f);

    const int32_t latency = convolver->getLatencySamples();
    const int32_t total = latency * 3;
    std::vector<float> input(total), output(total);
    for (int32_t i = 0; i < total; ++i)
        input[i] = (i % 7 == 0) ? 1.0f : -0.3f;
    output = input;

    float* bufs[1] = { output.data() };
    ASSERT_TRUE(convolver->process(bufs, 1, total, 0));

    for (int32_t i = 0; i < total - latency; ++i)
        EXPECT_NEAR(output[i + latency], input[i], 1e-4f) << "sample " << i;
}

// IR más larga que la FFT (varias particiones): comparar contra convolución directa.
TEST_F(DynamicConvolverTest, LongIRMatchesDirectConvolution) {
    const int32_t irLength = 1500;  // fftSize=1024, bloque=512 → 3 particiones
    std::vector<float> ir(irLength);
    unsigned seed = 1234;
    auto nextRand = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return (static_cast<float>(seed >> 8) / 8388608.0f) - 1.0f;
    };
    for (auto& v : ir)
        v = nextRand() * 0.1f;
    ir[0] = 1.0f;

    convolver->getIRManager().setLevelIR(0, ir, 0.0f);
    convolver->setMix(1.0f);

    const int32_t latency = convolver->getLatencySamples();
    const int32_t inputLen = 2048;
    std::vector<float> input(inputLen);
    for (auto& v : input)
        v = nextRand() * 0.5f;

    // Referencia: convolución directa
    const int32_t total = inputLen + latency + irLength;
    std::vector<float> expected(total, 0.0f);
    for (int32_t i = 0; i < inputLen; ++i)
        for (int32_t j = 0; j < irLength; ++j)
            expected[i + j] += input[i] * ir[j];

    std::vector<float> stream(total, 0.0f);
    std::copy(input.begin(), input.end(), stream.begin());

    const int32_t hostBlock = 100;  // bloque que no divide al interno
    for (int32_t offset = 0; offset < total; offset += hostBlock) {
        float* bufs[1] = { stream.data() + offset };
        ASSERT_TRUE(convolver->process(bufs, 1,
                                       std::min(hostBlock, total - offset), 0));
    }

    for (int32_t i = 0; i < inputLen + irLength - 1; ++i)
        EXPECT_NEAR(stream[i + latency], expected[i], 5e-3f) << "sample " << i;
}

// Estéreo: canales independientes (impulso solo en L no debe aparecer en R).
TEST_F(DynamicConvolverTest, StereoChannelsAreIndependent) {
    std::vector<float> delta(16, 0.0f);
    delta[0] = 1.0f;
    convolver->getIRManager().setLevelIR(0, delta, 0.0f);
    convolver->setMix(1.0f);

    const int32_t latency = convolver->getLatencySamples();
    const int32_t total = latency * 3;
    std::vector<float> left(total, 0.0f), right(total, 0.0f);
    left[10] = 1.0f;

    float* bufs[2] = { left.data(), right.data() };
    ASSERT_TRUE(convolver->process(bufs, 2, total, 0));

    EXPECT_NEAR(left[10 + latency], 1.0f, 1e-3f);
    for (float v : right)
        EXPECT_NEAR(v, 0.0f, 1e-5f);
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
// RingCaptureBuffer Tests
// ============================================================================

class RingCaptureBufferTest : public ::testing::Test {
protected:
    void SetUp() override {
        capture = std::make_unique<RingCaptureBuffer>();
        capture->prepare(48000.0, 2);
    }

    void pushConstant(int32_t numSamples, float value) {
        std::vector<float> left(static_cast<size_t>(numSamples), value);
        std::vector<float> right = left;
        const float* ptrs[2] = { left.data(), right.data() };
        capture->push(ptrs, 2, numSamples);
    }

    std::unique_ptr<RingCaptureBuffer> capture;
};

TEST_F(RingCaptureBufferTest, PreTriggerContent) {
    pushConstant(200, 1.0f);

    DevicesForge::CaptureMetadata meta {};
    ASSERT_TRUE(capture->trigger(50, 100, meta));

    pushConstant(100, 2.0f);

    ASSERT_TRUE(capture->isComplete());
    ASSERT_EQ(capture->getCapturedLength(), 150);
    const float* mono = capture->getCapturedMono();
    ASSERT_NE(mono, nullptr);
    for (int32_t i = 0; i < 50; ++i) {
        EXPECT_FLOAT_EQ(mono[i], 1.0f);
    }
    for (int32_t i = 50; i < 150; ++i) {
        EXPECT_FLOAT_EQ(mono[i], 2.0f);
    }
}

TEST_F(RingCaptureBufferTest, PostLengthAndComplete) {
    DevicesForge::CaptureMetadata meta {};
    ASSERT_TRUE(capture->trigger(5, 10, meta));
    EXPECT_FALSE(capture->isComplete());

    pushConstant(10, 0.25f);
    EXPECT_TRUE(capture->isComplete());
    EXPECT_EQ(capture->getCapturedLength(), 15);
}

TEST_F(RingCaptureBufferTest, ResetClearsCapture) {
    DevicesForge::CaptureMetadata meta {};
    capture->trigger(0, 4, meta);
    pushConstant(4, 1.0f);
    ASSERT_TRUE(capture->isComplete());

    capture->reset();
    EXPECT_EQ(capture->getState(), CaptureState::Monitoring);
    EXPECT_EQ(capture->getCapturedLength(), 0);
    EXPECT_EQ(capture->getCapturedMono(), nullptr);
}

TEST_F(RingCaptureBufferTest, StereoToMonoAverage) {
    std::vector<float> left = { 1.0f };
    std::vector<float> right = { -1.0f };
    const float* ptrs[2] = { left.data(), right.data() };

    DevicesForge::CaptureMetadata meta {};
    capture->trigger(0, 1, meta);
    capture->push(ptrs, 2, 1);

    ASSERT_TRUE(capture->isComplete());
    ASSERT_FLOAT_EQ(capture->getCapturedMono()[0], 0.0f);
}

// ============================================================================
// IRPostProcessor Tests
// ============================================================================

TEST(IRPostProcessorTest, HanningAttenuatesEnds) {
    std::vector<float> ir(101, 1.0f);
    IRPostProcessor::applyHanning(ir.data(), static_cast<int32_t>(ir.size()));
    EXPECT_NEAR(ir.front(), 0.0f, 1e-5f);
    EXPECT_NEAR(ir.back(), 0.0f, 1e-5f);
    EXPECT_GT(ir[50], 0.9f);
}

TEST(IRPostProcessorTest, TailFadeKeepsAttack) {
    std::vector<float> ir(100, 1.0f);
    IRPostProcessor::applyTailFade(ir.data(), static_cast<int32_t>(ir.size()), 0.25f);
    EXPECT_FLOAT_EQ(ir.front(), 1.0f);
    EXPECT_NEAR(ir.back(), 0.0f, 1e-5f);
    EXPECT_FLOAT_EQ(ir[50], 1.0f);
}

TEST(IRPostProcessorTest, NormalizePeakToTarget) {
    std::vector<float> ir = { 0.2f, -0.4f, 0.1f };
    const float peakBefore = IRPostProcessor::normalizePeak(ir.data(), static_cast<int32_t>(ir.size()), 1.0f);
    EXPECT_FLOAT_EQ(peakBefore, 0.4f);
    EXPECT_FLOAT_EQ(ir[1], -1.0f);
}

TEST(IRPostProcessorTest, ProcessAppliesWindowAndNorm) {
    std::vector<float> ir(64, 1.0f);
    IRPostProcessSettings settings;
    settings.windowType = IRWindowType::Hanning;
    IRPostProcessor::process(ir, settings);
    float peak = 0.0f;
    for (float s : ir)
        peak = std::max(peak, std::abs(s));
    EXPECT_NEAR(peak, 1.0f, 1e-5f);
    EXPECT_FLOAT_EQ(ir.front(), 1.0f);
    EXPECT_NEAR(ir.back(), 0.0f, 1e-5f);
}

// ============================================================================
// SweepDeconvolver Tests
// ============================================================================

TEST(IRExporterTest, WritesAllFormatsToTempDir) {
    const std::string base = "unit_test";
    std::vector<float> ir = {0.0f, 0.5f, -1.0f, 0.25f};

    ASSERT_TRUE(IRExporter::exportAllFormats(base, ir.data(), static_cast<int32_t>(ir.size()), 48000.0));

    IRExportRequest req;
    req.samples = ir.data();
    req.numSamples = static_cast<int32_t>(ir.size());
    req.sampleRate = 48000.0;
    req.format = IRExportFormat::WavPcm24;
    req.filePath = IRExporter::defaultExportDirectory() + "/" + base + "/IR.wav";

    std::ifstream check(req.filePath, std::ios::binary);
    ASSERT_TRUE(check.good());
    char riff[4] = {};
    check.read(riff, 4);
    EXPECT_EQ(riff[0], 'R');
    EXPECT_EQ(riff[1], 'I');
    EXPECT_EQ(riff[2], 'F');
    EXPECT_EQ(riff[3], 'F');
}

TEST(IRExporterTest, ClearSessionDirectoryRemovesKnownFiles) {
    const std::string base = "unit_test_clear";
    std::vector<float> ir = {1.0f, 0.0f};
    ASSERT_TRUE(IRExporter::exportAllFormats(base, ir.data(), 2, 48000.0));
    const std::string dir = IRExporter::sessionDirectory(base);
    ASSERT_TRUE(IRExporter::writeTextFile(dir + "/capture_log.txt", "test=1\n"));
    ASSERT_TRUE(IRExporter::exportCaptureRawWavFloat(dir + "/capture_raw.float.wav",
                                                     ir.data(), 2, 48000.0));

    IRExporter::clearSessionDirectory(base);

    std::ifstream check(dir + "/IR.wav");
    EXPECT_FALSE(check.good());
    std::ifstream log(dir + "/capture_log.txt");
    EXPECT_FALSE(log.good());
}

// Regresión: dividir por el espectro del sweep fuera de su banda amplificaba
// ruido ultrasónico hasta concentrar el 100% de la energía sobre 20 kHz,
// dejando la banda audible ~44 dB abajo (IR inaudible al convolucionar).
TEST(SweepDeconvolverTest, EnergyStaysInSweepBand) {
    SignalGenerator gen;
    gen.prepare(48000.0);
    gen.setType(SignalType::SineSweep);
    gen.setDuration(1.0f);
    gen.start();

    const float* ref = gen.getReference();
    const int32_t refLen = gen.getReferenceLength();
    ASSERT_GT(refLen, 0);

    std::vector<float> ir;
    ASSERT_TRUE(SweepDeconvolver::deconvolve(ref, refLen, ref, refLen, ir, 0, 48000.0));

    const int32_t fftSize = 65536;
    FFTProcessor fft;
    ASSERT_TRUE(fft.prepare(fftSize));
    std::vector<float> padded(static_cast<size_t>(fftSize), 0.0f);
    const int32_t copy = std::min(static_cast<int32_t>(ir.size()), fftSize);
    std::copy(ir.begin(), ir.begin() + copy, padded.begin());

    std::vector<std::complex<float>> spec(static_cast<size_t>(fft.getNumBins()));
    fft.forward(padded.data(), spec.data(), fftSize);

    const double binHz = 48000.0 / static_cast<double>(fftSize);
    double audible = 0.0, ultrasonic = 0.0;
    for (int32_t bin = 0; bin < fft.getNumBins(); ++bin) {
        const double freq = bin * binHz;
        const double energy = std::norm(spec[static_cast<size_t>(bin)]);
        if (freq >= 20.0 && freq <= 20000.0)
            audible += energy;
        else if (freq > 20000.0)
            ultrasonic += energy;
    }

    ASSERT_GT(audible, 0.0);
    EXPECT_LT(ultrasonic / audible, 0.05) << "energia fuera de banda domina la IR";
}

// Con excitación de banda ancha (sweep) y grabación idéntica a la referencia,
// la IR debe ser una delta limitada en banda al comienzo.
TEST(SweepDeconvolverTest, IdentityRecordedMatchesReference) {
    SignalGenerator gen;
    gen.prepare(48000.0);
    gen.setType(SignalType::SineSweep);
    gen.setDuration(1.0f);
    gen.start();

    const float* ref = gen.getReference();
    const int32_t refLen = gen.getReferenceLength();
    ASSERT_GT(refLen, 0);

    std::vector<float> ir;
    ASSERT_TRUE(SweepDeconvolver::deconvolve(ref, refLen, ref, refLen, ir, 0, 48000.0));
    ASSERT_GT(ir.size(), 0u);

    int32_t peakIndex = 0;
    float peakValue = 0.0f;
    for (int32_t i = 0; i < static_cast<int32_t>(ir.size()); ++i) {
        const float v = std::abs(ir[static_cast<size_t>(i)]);
        if (v > peakValue) {
            peakValue = v;
            peakIndex = i;
        }
    }
    EXPECT_LT(peakIndex, 32);
    EXPECT_GT(peakValue, 0.0f);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
