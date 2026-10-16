#include "WavIO.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace DevicesForge
{

	namespace
	{

		constexpr uint32_t kMaxChunkBytes = 256u * 1024u * 1024u;
		constexpr int32_t kMaxChannels = 8;

		bool readExact(std::ifstream& file, void* dst, size_t bytes)
		{
			file.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(bytes));
			return static_cast<bool>(file);
		}

		int32_t pcm24ToInt(const uint8_t b[3])
		{
			int32_t value = static_cast<int32_t>(b[0]) |
							(static_cast<int32_t>(b[1]) << 8) |
							(static_cast<int32_t>(b[2]) << 16);
			if ((value & 0x800000) != 0)
				value |= ~0xFFFFFF;
			return value;
		}

	} // namespace

	bool WavIO::load(const std::string& path, WavBuffer& out)
	{
		out = WavBuffer {};

		std::ifstream file(path, std::ios::binary);
		if (!file)
			return false;

		/* Verifico RIFF/WAVE */
		char riff[12] = {};
		if (!readExact(file, riff, 12) || std::memcmp(riff, "RIFF", 4) != 0 ||
			std::memcmp(riff + 8, "WAVE", 4) != 0)
			return false;

		int16_t audioFormat = 0;
		int16_t numChannels = 0;
		int16_t bitsPerSample = 0;
		int32_t sampleRate = 0;
		std::vector<char> dataChunk;
		bool haveFmt = false;
		bool haveData = false;

		/* Leo fmt y data, saltando el resto */
		while (file)
		{
			char chunkId[4] = {};
			uint32_t chunkSize = 0;
			if (!readExact(file, chunkId, 4) || !readExact(file, &chunkSize, 4))
				break;
			if (chunkSize > kMaxChunkBytes)
				return false;

			if (std::memcmp(chunkId, "fmt ", 4) == 0)
			{
				std::vector<char> fmt(chunkSize);
				if (chunkSize < 16 || !readExact(file, fmt.data(), chunkSize))
					return false;
				std::memcpy(&audioFormat, fmt.data(), 2);
				std::memcpy(&numChannels, fmt.data() + 2, 2);
				std::memcpy(&sampleRate, fmt.data() + 4, 4);
				std::memcpy(&bitsPerSample, fmt.data() + 14, 2);
				if (audioFormat == static_cast<int16_t>(0xFFFE) && chunkSize >= 40)
					std::memcpy(&audioFormat, fmt.data() + 24, 2);
				haveFmt = true;
			}
			else if (std::memcmp(chunkId, "data", 4) == 0)
			{
				dataChunk.resize(chunkSize);
				if (chunkSize > 0 && !readExact(file, dataChunk.data(), chunkSize))
					return false;
				haveData = true;
			}
			else
			{
				file.seekg(static_cast<std::streamoff>(chunkSize), std::ios::cur);
			}

			if ((chunkSize & 1u) != 0)
				file.seekg(1, std::ios::cur);
		}

		const bool pcm16 = audioFormat == 1 && bitsPerSample == 16;
		const bool pcm24 = audioFormat == 1 && bitsPerSample == 24;
		const bool pcmFloat = audioFormat == 3 && bitsPerSample == 32;
		if (!haveFmt || !haveData || numChannels < 1 || numChannels > kMaxChannels ||
			(!pcm16 && !pcm24 && !pcmFloat))
			return false;

		/* Separo los canales y paso cada muestra a float */
		const int32_t bytesPerSample = bitsPerSample / 8;
		const int32_t frameBytes = numChannels * bytesPerSample;
		const int32_t numFrames = static_cast<int32_t>(dataChunk.size()) / frameBytes;
		if (numFrames <= 0)
			return false;

		out.sampleRate = sampleRate > 0 ? static_cast<double>(sampleRate) : 0.0;
		out.channels.assign(static_cast<size_t>(numChannels),
							std::vector<float>(static_cast<size_t>(numFrames)));
		const auto* raw = reinterpret_cast<const uint8_t*>(dataChunk.data());

		for (int32_t i = 0; i < numFrames; ++i)
		{
			for (int32_t c = 0; c < numChannels; ++c)
			{
				const uint8_t* sample = raw + static_cast<size_t>((i * numChannels + c) * bytesPerSample);
				float value = 0.0f;
				if (pcm16)
				{
					int16_t pcm = 0;
					std::memcpy(&pcm, sample, 2);
					value = static_cast<float>(pcm) / 32768.0f;
				}
				else if (pcm24)
				{
					value = static_cast<float>(pcm24ToInt(sample)) / 8388608.0f;
				}
				else
				{
					std::memcpy(&value, sample, 4);
				}
				out.channels[static_cast<size_t>(c)][static_cast<size_t>(i)] = value;
			}
		}

		return out.sampleRate > 0.0;
	}

	bool WavIO::save(const std::string& path, const WavBuffer& in, int32_t bitsPerSample)
	{
		if (in.channels.empty() || in.channels[0].empty() || in.sampleRate <= 0.0)
			return false;
		if (bitsPerSample != 16 && bitsPerSample != 24 && bitsPerSample != 32)
			return false;

		const int32_t numChannels = static_cast<int32_t>(in.channels.size());
		const int32_t numFrames = static_cast<int32_t>(in.channels[0].size());
		if (numChannels < 1 || numChannels > kMaxChannels || numFrames <= 0)
			return false;
		for (const auto& channel : in.channels)
		{
			if (static_cast<int32_t>(channel.size()) != numFrames)
				return false;
		}

		std::ofstream file(path, std::ios::binary);
		if (!file)
			return false;

		/* Armo la cabecera canonica de 44 bytes */
		const int32_t bytesPerSample = bitsPerSample / 8;
		const int32_t dataSize = numFrames * numChannels * bytesPerSample;
		const int32_t fileSize = 36 + dataSize;
		const int32_t sampleRate = static_cast<int32_t>(std::lround(in.sampleRate));
		const int16_t audioFormat = bitsPerSample == 32 ? 3 : 1;
		const int16_t channels = static_cast<int16_t>(numChannels);
		const int32_t byteRate = sampleRate * numChannels * bytesPerSample;
		const int16_t blockAlign = static_cast<int16_t>(numChannels * bytesPerSample);
		const int16_t bits = static_cast<int16_t>(bitsPerSample);
		const int32_t fmtSize = 16;

		char header[44] = {};
		std::memcpy(header, "RIFF", 4);
		std::memcpy(header + 4, &fileSize, 4);
		std::memcpy(header + 8, "WAVE", 4);
		std::memcpy(header + 12, "fmt ", 4);
		std::memcpy(header + 16, &fmtSize, 4);
		std::memcpy(header + 20, &audioFormat, 2);
		std::memcpy(header + 22, &channels, 2);
		std::memcpy(header + 24, &sampleRate, 4);
		std::memcpy(header + 28, &byteRate, 4);
		std::memcpy(header + 32, &blockAlign, 2);
		std::memcpy(header + 34, &bits, 2);
		std::memcpy(header + 36, "data", 4);
		std::memcpy(header + 40, &dataSize, 4);
		file.write(header, 44);

		/* Intercalo y cuantizo */
		for (int32_t i = 0; i < numFrames; ++i)
		{
			for (int32_t c = 0; c < numChannels; ++c)
			{
				const float sample = std::clamp(
					in.channels[static_cast<size_t>(c)][static_cast<size_t>(i)], -1.0f, 1.0f);
				if (bitsPerSample == 16)
				{
					const int16_t pcm = static_cast<int16_t>(std::lround(sample * 32767.0f));
					file.write(reinterpret_cast<const char*>(&pcm), 2);
				}
				else if (bitsPerSample == 24)
				{
					const int32_t pcm = static_cast<int32_t>(std::lround(sample * 8388607.0f));
					const uint8_t bytes[3] = {
						static_cast<uint8_t>(pcm & 0xFF),
						static_cast<uint8_t>((pcm >> 8) & 0xFF),
						static_cast<uint8_t>((pcm >> 16) & 0xFF),
					};
					file.write(reinterpret_cast<const char*>(bytes), 3);
				}
				else
				{
					file.write(reinterpret_cast<const char*>(&sample), 4);
				}
			}
		}

		return file.good();
	}

} // namespace DevicesForge
