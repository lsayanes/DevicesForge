#include <gtest/gtest.h>
#include <clocale>
#include <cmath>
#include <complex>
#include <cstdio>
#include <fstream>
#include <memory>
#include <vector>

#include "FFTProcessor.h"
#include "IRManager.h"
#include "DynamicConvolver.h"
#include "SignalGenerator.h"
#include "RingCaptureBuffer.h"
#include "IRPostProcessor.h"
#include "SweepDeconvolver.h"
#include "IRExporter.h"
#include "WavIO.h"
#include "IRResampler.h"
#include "CaptureFile.h"
#include "OfflineCapturer.h"

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

// IR con el pico corrido (cinta, captura con pre-roll): el seco se retrasa
// hasta el pico, asi al 50% no hay filtro peine.
TEST_F(DynamicConvolverTest, DryFollowsIrPeakSoMixDoesNotComb) {
    const int32_t peak = 30;
    std::vector<float> ir(64, 0.0f);
    ir[peak] = 1.0f;
    convolver->getIRManager().setLevelIR(0, ir, 0.0f);
    convolver->setMix(0.5f);

    const int32_t latency = convolver->getLatencySamples();
    const int32_t total = latency * 4;
    std::vector<float> input(total), output(total);
    for (int32_t i = 0; i < total; ++i)
        input[i] = (i % 7 == 0) ? 1.0f : -0.3f;
    output = input;

    float* bufs[1] = { output.data() };
    ASSERT_TRUE(convolver->process(bufs, 1, total, 0));
    EXPECT_EQ(convolver->getDryOffsetSamples(), peak);

    for (int32_t i = 0; i < total - latency - peak; ++i)
        EXPECT_NEAR(output[i + latency + peak], input[i], 1e-4f) << "sample " << i;
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

TEST_F(SignalGeneratorTest, ToneIsOneKilohertz) {
    std::vector<float> block(48000, 0.0f);
    gen->renderTone(block.data(), 48000, 1000.0f);

    int32_t crossings = 0;
    for (int32_t i = 1; i < 48000; ++i) {
        if (block[static_cast<size_t>(i - 1)] < 0.0f && block[static_cast<size_t>(i)] >= 0.0f)
            ++crossings;
    }
    EXPECT_NEAR(crossings, 1000, 2);

    float peak = 0.0f;
    for (float s : block)
        peak = std::max(peak, std::abs(s));
    EXPECT_NEAR(peak, 1.0f, 0.01f);
}

TEST_F(SignalGeneratorTest, LoopKeepsPlayingPastOneDuration) {
    gen->setType(SignalType::SineSweep);
    gen->setDuration(0.5f);
    gen->startLoop();
    ASSERT_TRUE(gen->isLooping());
    ASSERT_TRUE(gen->isPlaying());

    std::vector<float> block(48000, 0.0f);
    gen->render(block.data(), 48000);
    EXPECT_TRUE(gen->isPlaying());
    EXPECT_TRUE(gen->isLooping());

    float peak = 0.0f;
    for (float s : block)
        peak = std::max(peak, std::abs(s));
    EXPECT_GT(peak, 0.5f);

    gen->stop();
    EXPECT_FALSE(gen->isPlaying());
    EXPECT_FALSE(gen->isLooping());
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

TEST(IRExporterTest, RemembersLastBrowseDirectory) {
    const std::string fakeFile = IRExporter::defaultLibraryDirectory() + "/subdir/IR.float.wav";
    IRExporter::rememberBrowseFile(fakeFile);
    EXPECT_EQ(IRExporter::rememberedBrowseDirectory(),
              IRExporter::defaultLibraryDirectory() + "/subdir");
}

TEST(IRExporterTest, WavFloat32RoundTripPreservesSamples) {
    const std::string path = IRExporter::defaultLibraryDirectory() + "/unit_roundtrip.float.wav";
    const std::vector<float> original = {0.0f, 0.25f, -0.5f, 0.75f, -1.0f};
    ASSERT_TRUE(IRExporter::saveWavFloat32(path, original.data(),
                                          static_cast<int32_t>(original.size()), 48000.0));

    std::vector<float> loaded;
    double sampleRate = 0.0;
    ASSERT_TRUE(IRExporter::loadWavFloat32(path, loaded, sampleRate));
    EXPECT_NEAR(sampleRate, 48000.0, 0.5);
    ASSERT_EQ(loaded.size(), original.size());
    for (size_t i = 0; i < original.size(); ++i)
        EXPECT_NEAR(loaded[i], original[i], 1.0e-6f);

    std::remove(path.c_str());
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
    EXPECT_NEAR(peakIndex, DECONV_PRE_ROLL, 2);
    EXPECT_GT(peakValue, 0.0f);
}

// La grabacion adelantada deja el pico al final del FFT. Hay que traerlo
// a DECONV_PRE_ROLL: si se recorta, la IR que queda es el residuo grave.
TEST(SweepDeconvolverTest, EarlyRecordingStaysNearStart) {
    SignalGenerator gen;
    gen.prepare(48000.0);
    gen.setType(SignalType::SineSweep);
    gen.setDuration(0.5f);
    gen.start();

    const float* ref = gen.getReference();
    const int32_t refLen = gen.getReferenceLength();
    ASSERT_GT(refLen, 400);

    const int32_t lead = 200;
    std::vector<float> recorded(static_cast<size_t>(refLen), 0.0f);
    for (int32_t i = 0; i < refLen - lead; ++i)
        recorded[static_cast<size_t>(i)] = ref[static_cast<size_t>(i + lead)];

    std::vector<float> ir;
    ASSERT_TRUE(SweepDeconvolver::deconvolve(recorded.data(), refLen, ref, refLen, ir, 0, 48000.0, 0));

    int32_t peakIndex = 0;
    float peakValue = 0.0f;
    const int32_t scan = std::min(static_cast<int32_t>(ir.size()), 4096);
    for (int32_t i = 0; i < scan; ++i) {
        const float value = std::abs(ir[static_cast<size_t>(i)]);
        if (value > peakValue) {
            peakValue = value;
            peakIndex = i;
        }
    }
    EXPECT_GT(peakValue, 0.05f);
    EXPECT_NEAR(peakIndex, DECONV_PRE_ROLL, 2);
}

// ============================================================================
// WavIO, remuestreo y .dfcapture
// ============================================================================

TEST(WavIOTest, Pcm24RoundTripKeepsLevel) {
    WavBuffer in;
    in.sampleRate = 48000.0;
    in.channels = {{0.0f, 0.5f, -0.25f, 0.125f}};
    const std::string path = IRExporter::defaultLibraryDirectory() + "/unit_pcm24.wav";
    ASSERT_TRUE(WavIO::save(path, in, 24));

    WavBuffer out;
    ASSERT_TRUE(WavIO::load(path, out));
    EXPECT_NEAR(out.sampleRate, 48000.0, 0.5);
    ASSERT_EQ(out.channels.size(), 1u);
    ASSERT_EQ(out.channels[0].size(), in.channels[0].size());
    for (size_t i = 0; i < in.channels[0].size(); ++i)
        EXPECT_NEAR(out.channels[0][i], in.channels[0][i], 2.0e-6f);
    std::remove(path.c_str());
}

TEST(WavIOTest, StereoPcm16KeepsBothChannels) {
    WavBuffer in;
    in.sampleRate = 44100.0;
    in.channels = {{0.25f, -0.5f}, {-0.75f, 0.125f}};
    const std::string path = IRExporter::defaultLibraryDirectory() + "/unit_pcm16_st.wav";
    ASSERT_TRUE(WavIO::save(path, in, 16));

    WavBuffer out;
    ASSERT_TRUE(WavIO::load(path, out));
    ASSERT_EQ(out.channels.size(), 2u);
    for (size_t c = 0; c < 2; ++c)
        for (size_t i = 0; i < 2; ++i)
            EXPECT_NEAR(out.channels[c][i], in.channels[c][i], 1.0f / 32768.0f);
    std::remove(path.c_str());
}

TEST(WavIOTest, LoadsExporterPcm24LittleEndian) {
    const std::vector<float> ir = {0.0f, -0.5f, 0.25f};
    IRExportRequest request;
    request.samples = ir.data();
    request.numSamples = static_cast<int32_t>(ir.size());
    request.sampleRate = 44100.0;
    request.format = IRExportFormat::WavPcm24;
    request.filePath = IRExporter::defaultLibraryDirectory() + "/unit_export_pcm24.wav";
    ASSERT_TRUE(IRExporter::exportBuffer(request));

    WavBuffer wav;
    ASSERT_TRUE(WavIO::load(request.filePath, wav));
    ASSERT_EQ(wav.channels.size(), 1u);
    ASSERT_EQ(wav.channels[0].size(), ir.size());
    for (size_t i = 0; i < ir.size(); ++i)
        EXPECT_NEAR(wav.channels[0][i], ir[i], 2.0e-6f);
    std::remove(request.filePath.c_str());
}

TEST(IRResamplerTest, ImpulseMovesWithRate) {
    std::vector<float> input(1000, 0.0f);
    input[200] = 1.0f;
    std::vector<float> output;
    ASSERT_TRUE(IRResampler::resample(input.data(), 1000, 44100.0, output, 48000.0));

    int32_t peak = 0;
    float peakValue = 0.0f;
    for (int32_t i = 0; i < static_cast<int32_t>(output.size()); ++i) {
        const float value = std::abs(output[static_cast<size_t>(i)]);
        if (value > peakValue) {
            peakValue = value;
            peak = i;
        }
    }
    const int32_t expected = static_cast<int32_t>(std::lround(200.0 * 48000.0 / 44100.0));
    EXPECT_NEAR(peak, expected, 1);
    EXPECT_GT(peakValue, 0.5f);
}

TEST(IRResamplerTest, ConstantLevelIsPreserved) {
    const std::vector<float> input(400, 0.5f);
    std::vector<float> output;
    ASSERT_TRUE(IRResampler::resample(input.data(), static_cast<int32_t>(input.size()),
                                      44100.0, output, 48000.0));
    ASSERT_GT(output.size(), 80u);
    for (size_t i = 40; i + 40 < output.size(); ++i)
        EXPECT_NEAR(output[i], 0.5f, 0.01f);
}

TEST(CaptureFileTest, RoundTripAndRejectsBadChecksum) {
    CaptureData data;
    data.name = "Canal 1";
    data.device = "Consola";
    data.author = "ls";
    data.date = "2026-10-16";
    data.notes = "toma limpia";
    data.sampleRate = 48000.0;
    data.sendDb = -6.0f;
    data.channels = {{0.0f, 1.0f, -0.5f}};
    const std::string path = IRExporter::defaultLibraryDirectory() + "/unit." + kCaptureExtension;
    ASSERT_TRUE(CaptureFile::save(path, data));

    CaptureData loaded;
    ASSERT_TRUE(CaptureFile::load(path, loaded));
    EXPECT_EQ(loaded.name, data.name);
    EXPECT_EQ(loaded.device, data.device);
    EXPECT_NEAR(loaded.sendDb, -6.0f, 1.0e-6f);
    ASSERT_EQ(loaded.channels.size(), 1u);
    ASSERT_EQ(loaded.channels[0].size(), 3u);
    EXPECT_FLOAT_EQ(loaded.channels[0][1], 1.0f);

    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    ASSERT_TRUE(file.good());
    file.seekp(-1, std::ios::end);
    char byte = 0;
    file.seekg(-1, std::ios::end);
    file.read(&byte, 1);
    byte = static_cast<char>(byte ^ 0x5A);
    file.seekp(-1, std::ios::end);
    file.write(&byte, 1);
    file.close();
    EXPECT_FALSE(CaptureFile::load(path, loaded));
    std::remove(path.c_str());
}

TEST(CaptureFileTest, BandNormalizeScalesImpulseToUnity) {
    std::vector<float> ir(256, 0.0f);
    ir[0] = 0.25f;
    float gainDb = 0.0f;
    ASSERT_TRUE(normalizeBandUnity(ir, 48000.0, gainDb));
    EXPECT_NEAR(ir[0], 1.0f, 1.0e-3f);
    EXPECT_NEAR(gainDb, static_cast<float>(20.0 * std::log10(0.25)), 0.05f);
}

// ============================================================================
// Capturador offline
// ============================================================================

namespace {

constexpr int32_t kEchoDelay = 180;

int32_t peakIndexOf(const std::vector<float>& ir, int32_t from, int32_t to) {
    from = std::max(0, from);
    to = std::min(to, static_cast<int32_t>(ir.size()));
    int32_t at = from;
    float best = 0.0f;
    for (int32_t i = from; i < to; ++i) {
        const float value = std::abs(ir[static_cast<size_t>(i)]);
        if (value > best) {
            best = value;
            at = i;
        }
    }
    return at;
}

void applyDirectPlusEcho(const std::vector<float>& in, std::vector<float>& out) {
    out.assign(in.size() + static_cast<size_t>(kEchoDelay), 0.0f);
    for (size_t i = 0; i < in.size(); ++i) {
        out[i] += in[i];
        out[i + static_cast<size_t>(kEchoDelay)] += 0.5f * in[i];
    }
}

float shapeCorrelation(const std::vector<float>& a, const std::vector<float>& b) {
    const int32_t count = std::min(static_cast<int32_t>(std::min(a.size(), b.size())), 1024);
    double dot = 0.0;
    double energyA = 0.0;
    double energyB = 0.0;
    for (int32_t i = 0; i < count; ++i) {
        const double av = a[static_cast<size_t>(i)];
        const double bv = b[static_cast<size_t>(i)];
        dot += av * bv;
        energyA += av * av;
        energyB += bv * bv;
    }
    if (energyA <= 0.0 || energyB <= 0.0)
        return 0.0f;
    return static_cast<float>(dot / std::sqrt(energyA * energyB));
}

} // namespace

TEST(OfflineCapturerTest, RecoversDelayedIrWithSpeedAndNoise) {
    SweepSpec spec;
    spec.sampleRate = 48000.0;
    spec.durationSeconds = 1.0f;
    spec.repeats = 2;

    std::vector<float> sweep;
    SweepLayout layout;
    ASSERT_TRUE(OfflineCapturer::makeSweep(spec, sweep, layout));
    ASSERT_GT(layout.syncSpan, 0);

    std::vector<float> through;
    applyDirectPlusEcho(sweep, through);

    AnalyzeOptions cleanOptions;
    cleanOptions.sweep = spec;
    cleanOptions.name = "limpia";
    CaptureData cleanCapture;
    QualityReport cleanReport;
    ASSERT_TRUE(OfflineCapturer::analyze(through.data(), static_cast<int32_t>(through.size()),
                                         spec.sampleRate, cleanOptions, cleanCapture, cleanReport));
    ASSERT_EQ(cleanCapture.channels.size(), 1u);
    const std::vector<float>& cleanIr = cleanCapture.channels[0];
    const int32_t directPeak = peakIndexOf(cleanIr, 0, 120);
    const int32_t echoPeak = peakIndexOf(cleanIr, directPeak + 120, directPeak + 240);
    EXPECT_LT(directPeak, 8);
    EXPECT_NEAR(echoPeak - directPeak, kEchoDelay, 30);
    EXPECT_GT(std::abs(cleanIr[static_cast<size_t>(echoPeak)]),
              std::abs(cleanIr[static_cast<size_t>(directPeak)]) * 0.15f);
    EXPECT_NEAR(cleanReport.speedRatio, 1.0, 0.002);
    EXPECT_EQ(cleanReport.repeatsUsed, spec.repeats);

    std::vector<float> sped;
    ASSERT_TRUE(IRResampler::resample(through.data(), static_cast<int32_t>(through.size()),
                                      spec.sampleRate, sped, spec.sampleRate * 0.995));
    uint32_t noise = 0x123456u;
    for (float& sample : sped) {
        noise = noise * 1664525u + 1013904223u;
        const float unit = static_cast<float>(static_cast<int32_t>(noise >> 8) % 2000 - 1000) / 1000.0f;
        sample += unit * 0.002f;
    }

    AnalyzeOptions dirtyOptions;
    dirtyOptions.sweep = spec;
    dirtyOptions.name = "cinta";
    CaptureData dirtyCapture;
    QualityReport dirtyReport;
    ASSERT_TRUE(OfflineCapturer::analyze(sped.data(), static_cast<int32_t>(sped.size()),
                                         spec.sampleRate, dirtyOptions, dirtyCapture, dirtyReport))
        << dirtyReport.text;
    EXPECT_NEAR(dirtyReport.speedRatio, 0.995, 0.003);
    EXPECT_EQ(dirtyReport.repeatsUsed, spec.repeats);
    EXPECT_NEAR(peakIndexOf(dirtyCapture.channels[0], directPeak + 120, directPeak + 240), echoPeak, 8);
    EXPECT_GT(shapeCorrelation(cleanCapture.channels[0], dirtyCapture.channels[0]), 0.9f);
    EXPECT_NE(dirtyReport.text.find("wow_flutter"), std::string::npos);
}

// Un equipo que invierte la polaridad tiene que quedar con el pico positivo:
// si no, el Mix del plugin resta el seco de la IR y se pierden los graves.
TEST(OfflineCapturerTest, InvertedDeviceGetsPositivePeak) {
    SweepSpec spec;
    spec.sampleRate = 48000.0;
    spec.durationSeconds = 0.5f;
    spec.repeats = 1;

    std::vector<float> sweep;
    SweepLayout layout;
    ASSERT_TRUE(OfflineCapturer::makeSweep(spec, sweep, layout));

    /* Sync en fase y barrido invertido, corrido 30 muestras */
    std::vector<float> through(sweep.size() + 64, 0.0f);
    const int32_t sweepFrom = layout.leadSilence + layout.syncLength;
    const int32_t sweepTo = layout.leadSilence + layout.syncSpan;
    for (int32_t i = 0; i < static_cast<int32_t>(sweep.size()); ++i) {
        const bool inSweeps = i >= sweepFrom && i < sweepTo;
        const int32_t at = inSweeps ? i + 30 : i;
        through[static_cast<size_t>(at)] += inSweeps ? -sweep[static_cast<size_t>(i)] : sweep[static_cast<size_t>(i)];
    }

    AnalyzeOptions options;
    options.sweep = spec;
    CaptureData capture;
    QualityReport report;
    ASSERT_TRUE(OfflineCapturer::analyze(through.data(), static_cast<int32_t>(through.size()),
                                         spec.sampleRate, options, capture, report))
        << report.text;
    const std::vector<float>& ir = capture.channels[0];
    const int32_t peak = peakIndexOf(ir, 0, 256);
    EXPECT_EQ(peak, report.peakIndex);
    EXPECT_GT(ir[static_cast<size_t>(peak)], 0.0f);
    EXPECT_TRUE(report.polarityInverted);
    EXPECT_NE(report.text.find("polarity=invertida_corregida"), std::string::npos);
}

// Cinta con wow: cada pasada llega con otro retraso. Sumar las IR hace un
// peine (12 muestras -> pozo en 2 kHz). Promediando |H| la respuesta queda plana.
TEST(OfflineCapturerTest, PassesWithDifferentDelayDoNotComb) {
    SweepSpec spec;
    spec.sampleRate = 48000.0;
    spec.durationSeconds = 0.5f;
    spec.repeats = 2;

    std::vector<float> sweep;
    SweepLayout layout;
    ASSERT_TRUE(OfflineCapturer::makeSweep(spec, sweep, layout));

    /* Corro solo la segunda pasada 12 muestras */
    const int32_t shift = 12;
    const int32_t secondFrom = layout.leadSilence + layout.syncLength + layout.gapLength +
                               layout.sweepLength + layout.gapLength;
    const int32_t secondTo = secondFrom + layout.sweepLength;
    std::vector<float> through(sweep.size() + 64, 0.0f);
    for (int32_t i = 0; i < static_cast<int32_t>(sweep.size()); ++i) {
        const int32_t at = (i >= secondFrom && i < secondTo) ? i + shift : i;
        through[static_cast<size_t>(at)] += sweep[static_cast<size_t>(i)];
    }

    AnalyzeOptions options;
    options.sweep = spec;
    CaptureData capture;
    QualityReport report;
    ASSERT_TRUE(OfflineCapturer::analyze(through.data(), static_cast<int32_t>(through.size()),
                                         spec.sampleRate, options, capture, report))
        << report.text;

    /* Comparo la energia en 1,5-2,5 kHz contra 500-1000 Hz */
    const int32_t fftSize = 32768;
    FFTProcessor fft;
    ASSERT_TRUE(fft.prepare(fftSize));
    std::vector<float> padded(static_cast<size_t>(fftSize), 0.0f);
    const std::vector<float>& ir = capture.channels[0];
    std::copy(ir.begin(), ir.begin() + std::min(ir.size(), padded.size()), padded.begin());
    std::vector<std::complex<float>> spec2(static_cast<size_t>(fft.getNumBins()));
    fft.forward(padded.data(), spec2.data(), fftSize);
    auto bandDb = [&](double lo, double hi) {
        const int32_t from = static_cast<int32_t>(lo * fftSize / 48000.0);
        const int32_t to = static_cast<int32_t>(hi * fftSize / 48000.0);
        double energy = 0.0;
        for (int32_t k = from; k < to; ++k)
            energy += std::norm(spec2[static_cast<size_t>(k)]);
        return 10.0 * std::log10(energy / (to - from));
    };
    EXPECT_NEAR(bandDb(1500.0, 2500.0) - bandDb(500.0, 1000.0), 0.0, 1.0);
    EXPECT_LT(report.peakIndex, 8);
}

// El grafico del Capturador: una curva por pasada y la de la IR, relativas a 1 kHz.
// La segunda pasada pasa por y = x + 0,5 x[n-1] (pierde agudos): su curva tiene que
// seguir ese filtro y la IR quedar en el promedio de potencia de las dos.
TEST(OfflineCapturerTest, BandResponseFollowsEachPass) {
    SweepSpec spec;
    spec.sampleRate = 48000.0;
    spec.durationSeconds = 0.5f;
    spec.repeats = 2;

    std::vector<float> sweep;
    SweepLayout layout;
    ASSERT_TRUE(OfflineCapturer::makeSweep(spec, sweep, layout));

    /* Filtro solo la segunda pasada */
    const int32_t secondFrom = layout.leadSilence + layout.syncLength + layout.gapLength +
                               layout.sweepLength + layout.gapLength;
    const int32_t secondTo = secondFrom + layout.sweepLength;
    std::vector<float> through(sweep);
    for (int32_t i = secondFrom; i <= secondTo; ++i)
        through[static_cast<size_t>(i)] += 0.5f * sweep[static_cast<size_t>(i - 1)];

    AnalyzeOptions options;
    options.sweep = spec;
    CaptureData capture;
    QualityReport report;
    ASSERT_TRUE(OfflineCapturer::analyze(through.data(), static_cast<int32_t>(through.size()),
                                         spec.sampleRate, options, capture, report))
        << report.text;

    ASSERT_EQ(report.bandHz.size(), 58u);
    EXPECT_NEAR(report.bandHz.front(), 24.8f, 0.1f);
    EXPECT_LT(report.bandHz.back(), 20000.0f);
    ASSERT_EQ(report.passBandsDb.size(), 2u);
    ASSERT_EQ(report.passBandsDb[0].size(), report.bandHz.size());
    ASSERT_EQ(report.passBandsDb[1].size(), report.bandHz.size());
    ASSERT_EQ(report.irBandsDb.size(), report.bandHz.size());

    /* |1 + 0,5 e^-jw|^2 del filtro de la segunda pasada */
    auto filterPower = [&](double hz) {
        const double w = 2.0 * 3.14159265358979 * hz / spec.sampleRate;
        return 1.25 + std::cos(w);
    };
    const double ref = filterPower(1000.0);
    for (size_t b = 0; b < report.bandHz.size(); ++b) {
        const double hz = report.bandHz[b];
        if (hz < 40.0 || hz > 16000.0)
            continue;
        const double second = 10.0 * std::log10(filterPower(hz) / ref);
        const double average = 10.0 * std::log10((1.0 + filterPower(hz)) / (1.0 + ref));
        EXPECT_NEAR(report.passBandsDb[0][b], 0.0, 0.3) << hz << " Hz";
        EXPECT_NEAR(report.passBandsDb[1][b], second, 0.3) << hz << " Hz";
        EXPECT_NEAR(report.irBandsDb[b], average, 0.3) << hz << " Hz";
    }
}

// El informe HTML acompaña a la captura: curvas, tabla y el nombre escapado.
TEST(OfflineCapturerTest, HtmlReportShowsBandsAndEscapesText) {
    CaptureData capture;
    capture.name = "A<B>&C";
    capture.device = "cinta";
    capture.sampleRate = 48000.0;
    capture.channels.assign(1, std::vector<float>(144, 0.0f));
    capture.channels[0][1] = 0.5f;

    QualityReport report;
    report.speedRatio = 1.00005;
    report.polarityInverted = true;
    report.peakIndex = 1;
    report.bandHz = {1000.0f, 8000.0f};
    report.passBandsDb = {{0.0f, -4.0f}, {0.0f, 4.0f}};
    report.irBandsDb = {0.0f, 0.5f};
    report.text = "quality=ok\n";

    /* Con locale en espanol (como la app Qt) el SVG tiene que seguir con punto decimal */
    const std::string path = "/tmp/df-informe-test.informe.html";
    const bool spanish = std::setlocale(LC_NUMERIC, "es_ES.UTF-8") != nullptr;
    const bool written = OfflineCapturer::writeHtmlReport(path, capture, report);
    std::setlocale(LC_NUMERIC, "C");
    ASSERT_TRUE(written);
    std::ifstream in(path);
    const std::string html((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_TRUE(spanish) << "no hay locale es_ES.UTF-8: el chequeo de punto decimal no aplica";
    EXPECT_NE(html.find("x1=\"46.00\""), std::string::npos);
    EXPECT_EQ(html.find("46,00"), std::string::npos);
    EXPECT_NE(html.find("A&lt;B&gt;&amp;C"), std::string::npos);
    EXPECT_EQ(html.find("A<B>"), std::string::npos);
    EXPECT_NE(html.find("Repetición 1"), std::string::npos);
    EXPECT_NE(html.find("Invertida"), std::string::npos);
    EXPECT_NE(html.find("+4,0"), std::string::npos);
    EXPECT_NE(html.find("-4,0"), std::string::npos);
    EXPECT_NE(html.find("8,0 dB"), std::string::npos);
    EXPECT_NE(html.find("quality=ok"), std::string::npos);
    std::remove(path.c_str());
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
