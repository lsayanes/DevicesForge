#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace DevicesForge 
{

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

	namespace PluginParamIDs 
	{
		constexpr uint32_t IR_SELECT = 1001;
		constexpr uint32_t MIX = 1002;
		constexpr uint32_t OUTPUT_GAIN = 1003;
		constexpr uint32_t AI_DENOISE = 1004;
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
