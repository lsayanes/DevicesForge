#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../plugin/DevicesForge.h"

namespace DevicesForge
{

	enum class IRExportFormat : int32_t
	{
		WavPcm24 = 0,
		WavFloat32 = 1,
		AiffPcm24_96k = 2,
		BinaryDFIR = 3
	};

	constexpr int32_t NUM_EXPORT_FORMATS = 4;
	constexpr int32_t AIFF_EXPORT_SAMPLE_RATE = 96000;

	inline IRExportFormat normalizedToExportFormat(float normalized)
	{
		int32_t index =
			static_cast<int32_t>(normalized * static_cast<float>(NUM_EXPORT_FORMATS - 1) + 0.5f);
		if (index < 0)
			index = 0;
		if (index >= NUM_EXPORT_FORMATS)
			index = NUM_EXPORT_FORMATS - 1;
		return static_cast<IRExportFormat>(index);
	}

	struct IRExportRequest
	{
		const float* samples = nullptr;
		int32_t numSamples = 0;
		double sampleRate = SAMPLE_RATE_DEFAULT;
		IRExportFormat format = IRExportFormat::WavPcm24;
		std::string filePath;
	};

#pragma pack(push, 1)
	struct IRBinaryHeader
	{
		char magic[4] {'D', 'F', 'I', 'R'};
		uint32_t version { 1 };
		uint32_t sampleRate { 48000 };
		uint32_t numSamples { 0 };
		uint32_t flags { 0 };
	};
#pragma pack(pop)

	class IRExporter
	{
	public:
		static std::string defaultExportDirectory();

		static bool exportBuffer(const IRExportRequest& request);
		static bool exportAllFormats(const std::string& directoryBaseName,
								   const float* samples,
								   int32_t numSamples,
								   double sampleRate,
								   std::string* firstError = nullptr);

		static bool exportCaptureRawWavFloat(const std::string& filePath,
											const float* samples,
											int32_t numSamples,
											double sampleRate);

		static std::string formatExtension(IRExportFormat format);
		static std::string sessionDirectory(const std::string& directoryBaseName);
		static void removeExportedIRFiles(const std::string& directoryBaseName);
		static bool writeTextFile(const std::string& filePath, const std::string& contents);

	private:
		static bool writeWavFloat32(const std::string& path,
									const float* samples,
									int32_t numSamples,
									int32_t sampleRate);
		static bool writeWavPcm24(const std::string& path,
								  const float* samples,
								  int32_t numSamples,
								  int32_t sampleRate);
		static bool writeAiffPcm24(const std::string& path,
								   const float* samples,
								   int32_t numSamples,
								   int32_t sourceSampleRate,
								   int32_t targetSampleRate);
		static bool writeBinary(const std::string& path,
								const float* samples,
								int32_t numSamples,
								int32_t sampleRate);

		static void resampleLinear(const float* input,
								   int32_t inputLength,
								   double inputRate,
								   std::vector<float>& output,
								   double outputRate);
		static void floatToPcm24(float sample, uint8_t out[3]);
	};

} // namespace DevicesForge
