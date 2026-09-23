#include "IRExporter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <cerrno>
#include <cstdlib>
#include <sys/stat.h>

namespace DevicesForge
{

	namespace
	{
		bool ensureDirectoryExists(const std::string& dirPath)
		{
			if (dirPath.empty())
				return false;

			std::string built;
			for (size_t i = 0; i < dirPath.size(); ++i)
			{
				const char c = dirPath[i];
				built.push_back(c);
				if (c == '/' || c == '\\')
				{
					if (built.size() <= 1)
						continue;
#if defined(_WIN32)
					if (_mkdir(built.c_str()) != 0 && errno != EEXIST)
						return false;
#else
					if (mkdir(built.c_str(), 0755) != 0 && errno != EEXIST)
						return false;
#endif
				}
			}

#if defined(_WIN32)
			return _mkdir(dirPath.c_str()) == 0 || errno == EEXIST;
#else
			return mkdir(dirPath.c_str(), 0755) == 0 || errno == EEXIST;
#endif
		}

		bool ensureParentDir(const std::string& filePath)
		{
			const auto pos = filePath.find_last_of("/\\");
			if (pos == std::string::npos)
				return true;
			return ensureDirectoryExists(filePath.substr(0, pos));
		}

		void writeAiff80BitRate(int32_t sampleRate, uint8_t out[10])
		{
			std::memset(out, 0, 10);
			if (sampleRate == 96000)
			{
				const uint8_t rate96000[10] = {0x40, 0x0F, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
				std::memcpy(out, rate96000, 10);
				return;
			}
			if (sampleRate == 48000)
			{
				const uint8_t rate48000[10] = {0x40, 0x0E, 0xAC, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
				std::memcpy(out, rate48000, 10);
				return;
			}
			if (sampleRate == 44100)
			{
				const uint8_t rate44100[10] = {0x40, 0x0E, 0xAC, 0x23, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
				std::memcpy(out, rate44100, 10);
				return;
			}
		}

		void writeU16Be(uint8_t* p, uint16_t v)
		{
			p[0] = static_cast<uint8_t>((v >> 8) & 0xFF);
			p[1] = static_cast<uint8_t>(v & 0xFF);
		}

		void writeU32Be(uint8_t* p, uint32_t v)
		{
			p[0] = static_cast<uint8_t>((v >> 24) & 0xFF);
			p[1] = static_cast<uint8_t>((v >> 16) & 0xFF);
			p[2] = static_cast<uint8_t>((v >> 8) & 0xFF);
			p[3] = static_cast<uint8_t>(v & 0xFF);
		}
	}

	std::string IRExporter::defaultExportDirectory()
	{
		const char* home = std::getenv("HOME");
#if defined(_WIN32)
		if (!home)
			home = std::getenv("USERPROFILE");
#endif
		if (!home)
			return "DevicesForge/exports";

		return std::string(home) + "/Documents/DevicesForge/exports";
	}

	std::string IRExporter::formatExtension(IRExportFormat format)
	{
		switch (format)
		{
			case IRExportFormat::WavFloat32:
				return ".float.wav";
			case IRExportFormat::AiffPcm24_96k:
				return ".aiff";
			case IRExportFormat::BinaryDFIR:
				return ".dfir";
			case IRExportFormat::WavPcm24:
			default:
				return ".wav";
		}
	}

	void IRExporter::floatToPcm24(float sample, uint8_t out[3])
	{
		sample = std::clamp(sample, -1.0f, 1.0f);
		const int32_t pcm = static_cast<int32_t>(std::lround(sample * 8388607.0f));
		out[0] = static_cast<uint8_t>((pcm >> 16) & 0xFF);
		out[1] = static_cast<uint8_t>((pcm >> 8) & 0xFF);
		out[2] = static_cast<uint8_t>(pcm & 0xFF);
	}

	void IRExporter::resampleLinear(const float* input,
									 int32_t inputLength,
									 double inputRate,
									 std::vector<float>& output,
									 double outputRate)
	{
		if (!input || inputLength <= 0 || inputRate <= 0.0 || outputRate <= 0.0)
		{
			output.clear();
			return;
		}

		if (std::abs(inputRate - outputRate) < 0.5)
		{
			output.assign(input, input + inputLength);
			return;
		}

		const int32_t outputLength =
			static_cast<int32_t>(std::llround(static_cast<double>(inputLength) * outputRate / inputRate));
		if (outputLength <= 0)
		{
			output.clear();
			return;
		}

		output.resize(static_cast<size_t>(outputLength));
		for (int32_t i = 0; i < outputLength; ++i)
		{
			const double srcPos = static_cast<double>(i) * inputRate / outputRate;
			const int32_t idx = static_cast<int32_t>(srcPos);
			const float frac = static_cast<float>(srcPos - static_cast<double>(idx));
			const float s0 = input[std::min(idx, inputLength - 1)];
			const float s1 = input[std::min(idx + 1, inputLength - 1)];
			output[static_cast<size_t>(i)] = s0 + (s1 - s0) * frac;
		}
	}

	bool IRExporter::writeWavFloat32(const std::string& path,
									 const float* samples,
									 int32_t numSamples,
									 int32_t sampleRate)
	{
		if (!samples || numSamples <= 0)
			return false;

		if (!ensureParentDir(path))
			return false;

		std::ofstream file(path, std::ios::binary);
		if (!file)
			return false;

		const int32_t dataSize = numSamples * static_cast<int32_t>(sizeof(float));
		const int32_t fileSize = 36 + dataSize;

		char header[44] = {0};
		header[0] = 'R';
		header[1] = 'I';
		header[2] = 'F';
		header[3] = 'F';
		std::memcpy(header + 4, &fileSize, 4);
		header[8] = 'W';
		header[9] = 'A';
		header[10] = 'V';
		header[11] = 'E';
		header[12] = 'f';
		header[13] = 'm';
		header[14] = 't';
		header[15] = ' ';
		int32_t chunkSize = 16;
		std::memcpy(header + 16, &chunkSize, 4);
		int16_t audioFormat = 3;
		int16_t numChannels = 1;
		int32_t byteRate = sampleRate * numChannels * static_cast<int32_t>(sizeof(float));
		int16_t blockAlign = numChannels * static_cast<int16_t>(sizeof(float));
		int16_t bitsPerSample = 32;
		std::memcpy(header + 20, &audioFormat, 2);
		std::memcpy(header + 22, &numChannels, 2);
		std::memcpy(header + 24, &sampleRate, 4);
		std::memcpy(header + 28, &byteRate, 4);
		std::memcpy(header + 32, &blockAlign, 2);
		std::memcpy(header + 34, &bitsPerSample, 2);
		header[36] = 'd';
		header[37] = 'a';
		header[38] = 't';
		header[39] = 'a';
		std::memcpy(header + 40, &dataSize, 4);

		file.write(header, 44);
		file.write(reinterpret_cast<const char*>(samples), dataSize);
		return file.good();
	}

	bool IRExporter::writeWavPcm24(const std::string& path,
									 const float* samples,
									 int32_t numSamples,
									 int32_t sampleRate)
	{
		if (!samples || numSamples <= 0)
			return false;

		if (!ensureParentDir(path))
			return false;

		std::ofstream file(path, std::ios::binary);
		if (!file)
			return false;

		const int32_t bytesPerSample = 3;
		const int32_t dataSize = numSamples * bytesPerSample;
		const int32_t fileSize = 36 + dataSize;

		char header[44] = {0};
		header[0] = 'R';
		header[1] = 'I';
		header[2] = 'F';
		header[3] = 'F';
		std::memcpy(header + 4, &fileSize, 4);
		header[8] = 'W';
		header[9] = 'A';
		header[10] = 'V';
		header[11] = 'E';
		header[12] = 'f';
		header[13] = 'm';
		header[14] = 't';
		header[15] = ' ';
		int32_t chunkSize = 16;
		std::memcpy(header + 16, &chunkSize, 4);
		int16_t audioFormat = 1;
		int16_t numChannels = 1;
		int32_t byteRate = sampleRate * numChannels * bytesPerSample;
		int16_t blockAlign = numChannels * static_cast<int16_t>(bytesPerSample);
		int16_t bitsPerSample = 24;
		std::memcpy(header + 20, &audioFormat, 2);
		std::memcpy(header + 22, &numChannels, 2);
		std::memcpy(header + 24, &sampleRate, 4);
		std::memcpy(header + 28, &byteRate, 4);
		std::memcpy(header + 32, &blockAlign, 2);
		std::memcpy(header + 34, &bitsPerSample, 2);
		header[36] = 'd';
		header[37] = 'a';
		header[38] = 't';
		header[39] = 'a';
		std::memcpy(header + 40, &dataSize, 4);
		file.write(header, 44);

		for (int32_t i = 0; i < numSamples; ++i)
		{
			uint8_t pcm[3];
			floatToPcm24(samples[static_cast<size_t>(i)], pcm);
			file.write(reinterpret_cast<const char*>(pcm), 3);
		}

		return file.good();
	}

	bool IRExporter::writeAiffPcm24(const std::string& path,
									  const float* samples,
									  int32_t numSamples,
									  int32_t sourceSampleRate,
									  int32_t targetSampleRate)
	{
		if (!samples || numSamples <= 0)
			return false;

		std::vector<float> resampled;
		resampleLinear(samples, numSamples, static_cast<double>(sourceSampleRate), resampled,
					   static_cast<double>(targetSampleRate));
		if (resampled.empty())
			return false;

		if (!ensureParentDir(path))
			return false;

		std::ofstream file(path, std::ios::binary);
		if (!file)
			return false;

		const int32_t numFrames = static_cast<int32_t>(resampled.size());
		const int32_t ssndDataSize = numFrames * 3;
		const int32_t commChunkSize = 18;
		const int32_t ssndChunkSize = 8 + ssndDataSize;
		const int32_t formSize = 4 + (8 + commChunkSize) + (8 + ssndChunkSize);

		file.write("FORM", 4);
		uint8_t u32[4];
		writeU32Be(u32, static_cast<uint32_t>(formSize));
		file.write(reinterpret_cast<const char*>(u32), 4);
		file.write("AIFF", 4);

		file.write("COMM", 4);
		writeU32Be(u32, static_cast<uint32_t>(commChunkSize));
		file.write(reinterpret_cast<const char*>(u32), 4);
		writeU16Be(u32, 1);
		file.write(reinterpret_cast<const char*>(u32), 2);
		writeU32Be(u32, static_cast<uint32_t>(numFrames));
		file.write(reinterpret_cast<const char*>(u32), 4);
		writeU16Be(u32, 24);
		file.write(reinterpret_cast<const char*>(u32), 2);
		uint8_t rateBytes[10];
		writeAiff80BitRate(targetSampleRate, rateBytes);
		file.write(reinterpret_cast<const char*>(rateBytes), 10);

		file.write("SSND", 4);
		writeU32Be(u32, static_cast<uint32_t>(ssndChunkSize));
		file.write(reinterpret_cast<const char*>(u32), 4);
		writeU32Be(u32, 0);
		file.write(reinterpret_cast<const char*>(u32), 4);
		writeU32Be(u32, 0);
		file.write(reinterpret_cast<const char*>(u32), 4);

		for (float sample : resampled)
		{
			uint8_t pcm[3];
			floatToPcm24(sample, pcm);
			file.write(reinterpret_cast<const char*>(pcm), 3);
		}

		return file.good();
	}

	bool IRExporter::writeBinary(const std::string& path,
								 const float* samples,
								 int32_t numSamples,
								 int32_t sampleRate)
	{
		if (!samples || numSamples <= 0)
			return false;

		if (!ensureParentDir(path))
			return false;

		std::ofstream file(path, std::ios::binary);
		if (!file)
			return false;

		IRBinaryHeader header {};
		header.sampleRate = static_cast<uint32_t>(sampleRate);
		header.numSamples = static_cast<uint32_t>(numSamples);
		file.write(reinterpret_cast<const char*>(&header), sizeof(header));
		file.write(reinterpret_cast<const char*>(samples),
				   static_cast<std::streamsize>(numSamples) * static_cast<std::streamsize>(sizeof(float)));
		return file.good();
	}

	bool IRExporter::exportBuffer(const IRExportRequest& request)
	{
		if (!request.samples || request.numSamples <= 0 || request.filePath.empty())
			return false;

		const int32_t sampleRate = static_cast<int32_t>(std::lround(request.sampleRate));

		switch (request.format)
		{
			case IRExportFormat::WavFloat32:
				return writeWavFloat32(request.filePath, request.samples, request.numSamples, sampleRate);
			case IRExportFormat::AiffPcm24_96k:
				return writeAiffPcm24(request.filePath, request.samples, request.numSamples, sampleRate,
									AIFF_EXPORT_SAMPLE_RATE);
			case IRExportFormat::BinaryDFIR:
				return writeBinary(request.filePath, request.samples, request.numSamples, sampleRate);
			case IRExportFormat::WavPcm24:
			default:
				return writeWavPcm24(request.filePath, request.samples, request.numSamples, sampleRate);
		}
	}

	bool IRExporter::exportAllFormats(const std::string& directoryBaseName,
									  const float* samples,
									  int32_t numSamples,
									  double sampleRate,
									  std::string* firstError)
	{
		if (!samples || numSamples <= 0)
			return false;

		const std::string dir = defaultExportDirectory() + "/" + directoryBaseName;
		if (!ensureDirectoryExists(dir))
			return false;

		const IRExportFormat formats[] = {IRExportFormat::WavPcm24, IRExportFormat::WavFloat32,
										  IRExportFormat::AiffPcm24_96k, IRExportFormat::BinaryDFIR};

		bool anyOk = false;
		for (IRExportFormat format : formats)
		{
			IRExportRequest req;
			req.samples = samples;
			req.numSamples = numSamples;
			req.sampleRate = sampleRate;
			req.format = format;
			req.filePath = dir + "/IR" + formatExtension(format);

			if (exportBuffer(req))
				anyOk = true;
			else if (firstError && firstError->empty())
				*firstError = req.filePath;
		}

		return anyOk;
	}

	bool IRExporter::exportCaptureRawWavFloat(const std::string& filePath,
											  const float* samples,
											  int32_t numSamples,
											  double sampleRate)
	{
		const int32_t sr = static_cast<int32_t>(std::lround(sampleRate));
		return writeWavFloat32(filePath, samples, numSamples, sr);
	}

	std::string IRExporter::sessionDirectory(const std::string& directoryBaseName)
	{
		return defaultExportDirectory() + "/" + directoryBaseName;
	}

	void IRExporter::removeExportedIRFiles(const std::string& directoryBaseName)
	{
		const std::string dir = sessionDirectory(directoryBaseName);
		const IRExportFormat formats[] = {IRExportFormat::WavPcm24, IRExportFormat::WavFloat32,
										  IRExportFormat::AiffPcm24_96k, IRExportFormat::BinaryDFIR};
		for (IRExportFormat format : formats)
			std::remove((dir + "/IR" + formatExtension(format)).c_str());
	}

	void IRExporter::clearSessionDirectory(const std::string& directoryBaseName)
	{
		removeExportedIRFiles(directoryBaseName);
		const std::string dir = sessionDirectory(directoryBaseName);
		std::remove((dir + "/capture_raw.float.wav").c_str());
		std::remove((dir + "/capture_log.txt").c_str());
	}

	bool IRExporter::writeTextFile(const std::string& filePath, const std::string& contents)
	{
		if (!ensureParentDir(filePath))
			return false;
		std::ofstream file(filePath);
		if (!file)
			return false;
		file << contents;
		return file.good();
	}

} // namespace DevicesForge
