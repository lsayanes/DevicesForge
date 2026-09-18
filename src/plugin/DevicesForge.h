#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace DevicesForge 
{

	/** Pi and common multiples (DSP, tests, host). */
	constexpr double kPi = 3.14159265358979323846;
	constexpr float kPiF = static_cast<float>(kPi);
	constexpr double kTwoPi = 2.0 * kPi;
	constexpr float kTwoPiF = static_cast<float>(kTwoPi);

	constexpr const char* PLUGIN_NAME = "DevicesForge";
	constexpr const char* PLUGIN_VENDOR = "lsayanes & DevicesForge";
	constexpr const int32_t PLUGIN_VERSION_MAJOR = 1;
	constexpr const int32_t PLUGIN_VERSION_MINOR = 0;
	constexpr const int32_t PLUGIN_VERSION_PATCH = 0;

	constexpr double SAMPLE_RATE_DEFAULT = 48000.0;
	constexpr int32_t NUM_CHANNELS = 2;
	constexpr int32_t MAX_IR_LENGTH = 65536;
	constexpr int32_t FFT_SIZE = 4096;

	constexpr int32_t NUM_CAPTURE_LEVELS = 4;
	constexpr float CAPTURE_LEVELS[NUM_CAPTURE_LEVELS] = {
		-24.0f,  // Quiet
		-12.0f,  // Low
		0.0f,   // Nominal
		+6.0f    // Hot
	};

	enum class SignalType : int32_t
	{
		SineSweep = 0,
		Dirac = 1,
		PinkNoise = 2,
		MLS = 3
	};

	constexpr int32_t NUM_SIGNAL_TYPES = 4;
	constexpr float SIGNAL_DURATION_MIN = 0.5f;
	constexpr float SIGNAL_DURATION_MAX = 5.0f;
	constexpr float SIGNAL_DURATION_DEFAULT = 1.0f;
	constexpr float SWEEP_FREQ_START_HZ = 20.0f;
	constexpr float SWEEP_FREQ_END_HZ = 20000.0f;
	constexpr int32_t MLS_REGISTER_BITS = 16;
	constexpr int32_t MLS_PERIOD = (1 << MLS_REGISTER_BITS) - 1;

	constexpr float CAPTURE_PRE_SEC = 0.100f;
	constexpr float CAPTURE_POST_TAIL_SEC = 0.250f;
	constexpr float CAPTURE_MONITOR_SEC =
		CAPTURE_PRE_SEC + SIGNAL_DURATION_MAX + CAPTURE_POST_TAIL_SEC + 0.5f;

	constexpr float IR_NORMALIZE_PEAK_TARGET = 1.0f;
	constexpr float IR_KAISER_BETA_DEFAULT = 5.0f;

	inline float signalDurationNormalizedDefault()
	{
		return (SIGNAL_DURATION_DEFAULT - SIGNAL_DURATION_MIN) /
			   (SIGNAL_DURATION_MAX - SIGNAL_DURATION_MIN);
	}

	inline float normalizedToDuration(float normalized)
	{
		if (normalized < 0.0f) normalized = 0.0f;
		if (normalized > 1.0f) normalized = 1.0f;
		return SIGNAL_DURATION_MIN + normalized * (SIGNAL_DURATION_MAX - SIGNAL_DURATION_MIN);
	}

	inline SignalType normalizedToSignalType(float normalized)
	{
		int32_t index = static_cast<int32_t>(normalized * static_cast<float>(NUM_SIGNAL_TYPES - 1) + 0.5f);
		if (index < 0) index = 0;
		if (index >= NUM_SIGNAL_TYPES) index = NUM_SIGNAL_TYPES - 1;
		return static_cast<SignalType>(index);
	}

	namespace PluginParamIDs 
	{
		constexpr uint32_t IR_SELECT = 1001;
		constexpr uint32_t MIX = 1002;
		constexpr uint32_t OUTPUT_GAIN = 1003;
		constexpr uint32_t AI_DENOISE = 1004;
		constexpr uint32_t SIGNAL_TYPE = 1005;
		constexpr uint32_t SIGNAL_DURATION = 1006;
		constexpr uint32_t GENERATE = 1007;
		constexpr uint32_t EXPORT_FORMAT = 1008;
		constexpr uint32_t EXPORT = 1009;
	}

	struct IRMetadata 
	{
		std::string name;
		std::string console;
		std::string channel;
		float levelDB = 0.0f;
		int32_t sampleRate = 48000;
		int32_t length = 0;
	};

	struct CaptureSession 
	{
		bool active = false;
		int32_t currentLevel = 0;
		int32_t totalLevels = NUM_CAPTURE_LEVELS;
		std::vector<std::vector<float>> capturedIRs;
	};

} // DevicesForge
