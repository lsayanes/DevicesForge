#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cmath>
#include <cstring>
#include <fstream>
#include <string>

#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "base/source/fstreamer.h"
#include "base/source/fstring.h"

#include "plugin/DevicesForge.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

void generateSine(float* buffer, int32 numSamples, float freq, float sampleRate, float& phase) {
    for (int32 i = 0; i < numSamples; ++i) {
        buffer[i] = std::sin(phase);
        phase += DevicesForge::kTwoPiF * freq / sampleRate;
        if (phase > DevicesForge::kTwoPiF) phase -= DevicesForge::kTwoPiF;
    }
}

bool writeWav(const char* filename, const std::vector<float>& data, int32 numChannels, int32 sampleRate) {
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) return false;

    int32 numSamples = static_cast<int32_t>(data.size());
    int32 dataSize = numSamples * sizeof(float);
    int32 fileSize = 36 + dataSize;

    char header[44] = {0};
    header[0]='R'; header[1]='I'; header[2]='F'; header[3]='F';
    std::memcpy(header+4, &fileSize, 4);
    header[8]='W'; header[9]='A'; header[10]='V'; header[11]='E';
    header[12]='f'; header[13]='m'; header[14]='t'; header[15]=' ';
    int32 chunkSize = 16;
    std::memcpy(header+16, &chunkSize, 4);
    int16 audioFormat = 3;
    int16 ch = numChannels;
    int32 byteRate = sampleRate * numChannels * (int32)sizeof(float);
    int16 blockAlign = numChannels * (int16)sizeof(float);
    int16 bps = 32;
    std::memcpy(header+20, &audioFormat, 2);
    std::memcpy(header+22, &ch, 2);
    std::memcpy(header+24, &sampleRate, 4);
    std::memcpy(header+28, &byteRate, 4);
    std::memcpy(header+32, &blockAlign, 2);
    std::memcpy(header+34, &bps, 2);
    header[36]='d'; header[37]='a'; header[38]='t'; header[39]='a';
    std::memcpy(header+40, &dataSize, 4);

    file.write(header, 44);
    file.write(reinterpret_cast<const char*>(data.data()), dataSize);
    return true;
}

// ============================================================================
// Loopback self-test: Generate ON, feed output back to input (perfect loop).
// Validates capture + deconvolution + export without any DAW routing.
// ============================================================================
static int runLoopbackTest(IAudioProcessor* audioProc, double sampleRate, int32 blockSize) {
    fprintf(stderr, "\n=== LOOPBACK TEST (out -> in, software) ===\n");

    const int32 totalSamples = (int32)(sampleRate * 3.0); // 3 s: sweep 1s + pre/post + margen

    std::vector<float> loopL(blockSize, 0.f), loopR(blockSize, 0.f);
    std::vector<float> outL(blockSize, 0.f), outR(blockSize, 0.f);

    // Generate ON at block 0 (edge Off->On)
    ParameterChanges genOn;
    {
        int32 qi;
        if (auto* q = genOn.addParameterData(DevicesForge::PluginParamIDs::GENERATE, qi)) {
            int32 pi; q->addPoint(0, 1.0, pi);
        }
    }

    float inputPeakSeen = 0.f;

    for (int32 i = 0; i < totalSamples; i += blockSize) {
        const int32 n = std::min(blockSize, totalSamples - i);

        std::vector<float*> inPtrs = { loopL.data(), loopR.data() };
        std::vector<float*> outPtrs = { outL.data(), outR.data() };

        AudioBusBuffers inBus{};
        inBus.numChannels = 2;
        inBus.channelBuffers32 = inPtrs.data();

        AudioBusBuffers outBus{};
        outBus.numChannels = 2;
        outBus.channelBuffers32 = outPtrs.data();

        ProcessData data{};
        data.numInputs = 1;
        data.numOutputs = 1;
        data.inputs = &inBus;
        data.outputs = &outBus;
        data.numSamples = n;
        if (i == 0) data.inputParameterChanges = &genOn;

        if (audioProc->process(data) != kResultOk) {
            fprintf(stderr, "process() failed at sample %d\n", i);
            return 1;
        }

        // Feedback: this block's output is next block's input (1-block latency loop)
        std::memcpy(loopL.data(), outL.data(), (size_t)n * sizeof(float));
        std::memcpy(loopR.data(), outR.data(), (size_t)n * sizeof(float));

        for (int32 s = 0; s < n; ++s)
            inputPeakSeen = std::max(inputPeakSeen, std::abs(loopL[s]));
    }

    fprintf(stderr, "Loop input peak seen by host: %.4f\n", inputPeakSeen);

    const char* home = std::getenv("HOME");
    const std::string dir = std::string(home ? home : "") + "/Documents/DevicesForge/exports/latest";

    auto fileSize = [](const std::string& p) -> long {
        std::ifstream f(p, std::ios::binary | std::ios::ate);
        return f ? (long)f.tellg() : -1;
    };

    const std::string files[] = { "/capture_raw.float.wav", "/IR.wav", "/IR.float.wav",
                                  "/IR.aiff", "/IR.dfir", "/capture_log.txt" };
    bool irOk = true;
    fprintf(stderr, "\nExported files in %s:\n", dir.c_str());
    for (const auto& f : files) {
        const long sz = fileSize(dir + f);
        fprintf(stderr, "  %-24s %s (%ld bytes)\n", f.c_str() + 1,
                sz > 0 ? "OK" : "MISSING", sz);
        if (sz <= 0 && f != "/capture_log.txt")
            irOk = false;
    }

    std::ifstream log(dir + "/capture_log.txt");
    if (log) {
        fprintf(stderr, "\ncapture_log.txt:\n");
        std::string line;
        while (std::getline(log, line))
            fprintf(stderr, "  %s\n", line.c_str());
    }

    if (inputPeakSeen < 0.01f) {
        fprintf(stderr, "\nFAIL: la salida del plugin no llego al loop (Generate no sono).\n");
        return 1;
    }
    if (!irOk) {
        fprintf(stderr, "\nFAIL: faltan archivos IR (pipeline no completo).\n");
        return 1;
    }

    fprintf(stderr, "\nLOOPBACK TEST: OK — captura + deconvolucion + export funcionan.\n");
    fprintf(stderr, "Si en Cubase capture_raw sigue en silencio, el problema es el routing\n");
    fprintf(stderr, "(la pista del plugin no recibe la entrada fisica; activar Monitor).\n");
    return 0;
}

int main(int argc, char* argv[]) {
    fprintf(stderr, "=== DevicesForge - Test Host ===\n\n");

    bool loopbackMode = false;
    std::string pluginPath;
    for (int a = 1; a < argc; ++a) {
        if (std::strcmp(argv[a], "--loopback") == 0)
            loopbackMode = true;
        else
            pluginPath = argv[a];
    }
    if (pluginPath.empty()) {
        const char* home = std::getenv("HOME");
        pluginPath = std::string(home ? home : "") +
                     "/Library/Audio/Plug-Ins/VST3/DevicesForge.vst3";
    }

    fprintf(stderr, "Loading: %s\n", pluginPath.c_str());

    HostApplication hostApp;
    PluginContextFactory::instance().setPluginContext(&hostApp);

    std::string errorDesc;
    auto module = VST3::Hosting::Module::create(pluginPath, errorDesc);
    if (!module) {
        fprintf(stderr, "Failed to load: %s\n", errorDesc.c_str());
        return 1;
    }
    fprintf(stderr, "Module loaded.\n");

    auto& factory = module->getFactory();
    auto fi = factory.info();
    fprintf(stderr, "Vendor: %s\n", fi.vendor().c_str());

    auto cis = factory.classInfos();
    fprintf(stderr, "Classes: %zu\n", cis.size());
    for (auto& ci : cis) {
        fprintf(stderr, "  - %s [%s]\n", ci.name().c_str(), ci.category().c_str());
    }

    const VST3::Hosting::ClassInfo* procClass = nullptr;
    for (auto& ci : cis) {
        if (ci.category() == kVstAudioEffectClass) {
            procClass = &ci;
            break;
        }
    }
    if (!procClass) {
        fprintf(stderr, "No audio effect found!\n");
        return 1;
    }

    fprintf(stderr, "Creating PlugProvider...\n");
    PlugProvider plugProvider(factory, *procClass, true);
    if (!plugProvider.initialize()) {
        fprintf(stderr, "PlugProvider init failed!\n");
        return 1;
    }
    fprintf(stderr, "PlugProvider initialized.\n");

    IComponent* component = plugProvider.getComponentPtr();
    if (!component) {
        fprintf(stderr, "No component!\n");
        return 1;
    }

    IAudioProcessor* audioProc = nullptr;
    if (component->queryInterface(IAudioProcessor::iid, (void**)&audioProc) != kResultOk) {
        fprintf(stderr, "No IAudioProcessor!\n");
        return 1;
    }
    fprintf(stderr, "Got audio processor.\n");

    const double sampleRate = 48000.0;
    const int32 blockSize = 512;
    const int32 totalSamples = (int32)(sampleRate * 2); // 2 seconds

    ProcessSetup setup;
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = blockSize;
    setup.sampleRate = sampleRate;

    if (audioProc->setupProcessing(setup) != kResultOk) {
        fprintf(stderr, "setupProcessing failed!\n");
        return 1;
    }

    if (component->setActive(true) != kResultOk) {
        fprintf(stderr, "setActive failed!\n");
        return 1;
    }

    audioProc->setProcessing(true);
    fprintf(stderr, "Processing started.\n");

    if (loopbackMode) {
        const int rc = runLoopbackTest(audioProc, sampleRate, blockSize);
        audioProc->setProcessing(false);
        component->setActive(false);
        audioProc->release();
        return rc;
    }

    // Generate input audio
    std::vector<float> inputL(totalSamples), inputR(totalSamples, 0.f);
    std::vector<float> outputL(totalSamples, 0.f), outputR(totalSamples, 0.f);
    float phase = 0.f;
    generateSine(inputL.data(), totalSamples, 440.f, (float)sampleRate, phase);

    // Allocate channel buffer pointer arrays
    std::vector<float*> inChPtrs = { inputL.data(), inputR.data() };
    std::vector<float*> outChPtrs = { outputL.data(), outputR.data() };

    // Setup AudioBusBuffers with proper channel pointer arrays
    AudioBusBuffers inBus{};
    inBus.numChannels = 2;
    inBus.channelBuffers32 = inChPtrs.data();

    AudioBusBuffers outBus{};
    outBus.numChannels = 2;
    outBus.channelBuffers32 = outChPtrs.data();

    // Parameter changes
    ParameterChanges paramChanges;
    {
        int32 qi;
        if (auto* q = paramChanges.addParameterData(1002, qi)) {
            int32 pi; q->addPoint(0, 1.0, pi); // Mix 100%
        }
    }
    {
        int32 qi;
        if (auto* q = paramChanges.addParameterData(1003, qi)) {
            int32 pi; q->addPoint(0, 0.5, pi); // Gain 0dB
        }
    }

    fprintf(stderr, "Processing %d samples...\n", totalSamples);

    for (int32 i = 0; i < totalSamples; i += blockSize) {
        int32 n = std::min(blockSize, totalSamples - i);

        std::vector<float*> inChPtrs = { inputL.data() + i, inputR.data() + i };
        std::vector<float*> outChPtrs = { outputL.data() + i, outputR.data() + i };

        AudioBusBuffers inBus{};
        inBus.numChannels = 2;
        inBus.channelBuffers32 = inChPtrs.data();

        AudioBusBuffers outBus{};
        outBus.numChannels = 2;
        outBus.channelBuffers32 = outChPtrs.data();

        ProcessData data{};
        data.numInputs = 1;
        data.numOutputs = 1;
        data.inputs = &inBus;
        data.outputs = &outBus;
        data.numSamples = n;
        if (i == 0) data.inputParameterChanges = &paramChanges;

        audioProc->process(data);
    }

    fprintf(stderr, "Processing done.\n");

    // Print parameters
    IEditController* ctrl = plugProvider.getControllerPtr();
    if (ctrl) {
        int32 np = ctrl->getParameterCount();
        fprintf(stderr, "\nParameters (%d):\n", np);
        for (int32 i = 0; i < np; ++i) {
            ParameterInfo pi;
            if (ctrl->getParameterInfo(i, pi) == kResultOk) {
                char title[128];
                UString(pi.title, USTRINGSIZE(pi.title)).toAscii(title, 128);
                fprintf(stderr, "  [%d] %s (default: %.2f)\n", pi.id, title, pi.defaultNormalizedValue);
            }
        }
    }

    // Write output
    std::vector<float> stereo(totalSamples * 2);
    for (int32 i = 0; i < totalSamples; ++i) {
        stereo[i*2] = outputL[i];
        stereo[i*2+1] = outputR[i];
    }

    if (writeWav("test_output.wav", stereo, 2, (int32)sampleRate)) {
        fprintf(stderr, "\nOutput: test_output.wav\n");
    }

    // Print first few samples to verify
    fprintf(stderr, "\nFirst 8 output samples (L):\n  ");
    for (int32 i = 0; i < 8 && i < totalSamples; ++i) {
        fprintf(stderr, "%.6f ", outputL[i]);
    }
    fprintf(stderr, "\n");

    audioProc->setProcessing(false);
    component->setActive(false);
    audioProc->release();

    fprintf(stderr, "\nAll done!\n");
    return 0;
}
