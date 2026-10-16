#include "CaptureFile.h"

#include "DFConstants.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <vector>

namespace DevicesForge
{

	namespace
	{

		constexpr char kMagic[4] = {'D', 'F', 'C', 'P'};
		constexpr uint32_t kVersion = 1;
		constexpr uint32_t kFlagBandNormalized = 1u;
		constexpr uint32_t kMaxTextBytes = 4096;

#pragma pack(push, 1)
		struct CaptureHeader
		{
			char magic[4];
			uint32_t version;
			uint32_t sampleRate;
			uint32_t channelCount;
			uint32_t numSamples;
			float sendDb;
			float bandGainDb;
			uint32_t flags;
			uint32_t crc32;
		};
#pragma pack(pop)

		uint32_t crc32(const uint8_t* data, size_t size)
		{
			uint32_t crc = 0xFFFFFFFFu;
			for (size_t i = 0; i < size; ++i)
			{
				crc ^= data[i];
				for (int bit = 0; bit < 8; ++bit)
				{
					const uint32_t mask = -(crc & 1u);
					crc = (crc >> 1) ^ (0xEDB88320u & mask);
				}
			}
			return ~crc;
		}

		bool writeText(std::ofstream& file, const std::string& text)
		{
			if (text.size() > kMaxTextBytes)
				return false;
			const uint32_t length = static_cast<uint32_t>(text.size());
			file.write(reinterpret_cast<const char*>(&length), 4);
			if (length > 0)
				file.write(text.data(), static_cast<std::streamsize>(length));
			return static_cast<bool>(file);
		}

		bool readText(std::ifstream& file, std::string& text)
		{
			uint32_t length = 0;
			file.read(reinterpret_cast<char*>(&length), 4);
			if (!file || length > kMaxTextBytes)
				return false;
			text.assign(length, '\0');
			if (length > 0)
				file.read(text.data(), static_cast<std::streamsize>(length));
			return static_cast<bool>(file);
		}

		struct Biquad
		{
			float b0 = 1.0f;
			float b1 = 0.0f;
			float b2 = 0.0f;
			float a1 = 0.0f;
			float a2 = 0.0f;
			float z1 = 0.0f;
			float z2 = 0.0f;

			float process(float x)
			{
				const float y = b0 * x + z1;
				z1 = b1 * x - a1 * y + z2;
				z2 = b2 * x - a2 * y;
				return y;
			}
		};

		Biquad makeHighpass(double frequencyHz, double sampleRate)
		{
			const double w0 = kTwoPi * frequencyHz / sampleRate;
			const double cosw = std::cos(w0);
			const double alpha = std::sin(w0) / (2.0 * 0.7071067811865476);
			const double a0 = 1.0 + alpha;
			Biquad filter;
			filter.b0 = static_cast<float>(((1.0 + cosw) / 2.0) / a0);
			filter.b1 = static_cast<float>((-(1.0 + cosw)) / a0);
			filter.b2 = filter.b0;
			filter.a1 = static_cast<float>((-2.0 * cosw) / a0);
			filter.a2 = static_cast<float>((1.0 - alpha) / a0);
			return filter;
		}

		Biquad makeLowpass(double frequencyHz, double sampleRate)
		{
			const double w0 = kTwoPi * frequencyHz / sampleRate;
			const double cosw = std::cos(w0);
			const double alpha = std::sin(w0) / (2.0 * 0.7071067811865476);
			const double a0 = 1.0 + alpha;
			Biquad filter;
			filter.b0 = static_cast<float>(((1.0 - cosw) / 2.0) / a0);
			filter.b1 = static_cast<float>((1.0 - cosw) / a0);
			filter.b2 = filter.b0;
			filter.a1 = static_cast<float>((-2.0 * cosw) / a0);
			filter.a2 = static_cast<float>((1.0 - alpha) / a0);
			return filter;
		}

		double bandRms(const float* input, int32_t length, double sampleRate)
		{
			const double highHz = std::min(5000.0, sampleRate * 0.45);
			Biquad highpass = makeHighpass(200.0, sampleRate);
			Biquad lowpass = makeLowpass(highHz, sampleRate);
			double energy = 0.0;
			for (int32_t i = 0; i < length; ++i)
			{
				const float y = lowpass.process(highpass.process(input[i]));
				energy += static_cast<double>(y) * static_cast<double>(y);
			}
			return std::sqrt(energy / static_cast<double>(length));
		}

	} // namespace

	bool normalizeBandUnity(std::vector<float>& ir, double sampleRate, float& gainDb)
	{
		gainDb = 0.0f;
		if (ir.empty() || sampleRate <= 400.0)
			return false;

		/* Mido la IR y un impulso con el mismo filtro de banda */
		std::vector<float> impulse(ir.size(), 0.0f);
		impulse[0] = 1.0f;
		const double irRms = bandRms(ir.data(), static_cast<int32_t>(ir.size()), sampleRate);
		const double refRms = bandRms(impulse.data(), static_cast<int32_t>(impulse.size()), sampleRate);
		if (refRms <= 1.0e-12 || irRms <= 1.0e-12)
			return false;

		const double gain = irRms / refRms;
		const float scale = static_cast<float>(1.0 / gain);
		for (float& sample : ir)
			sample *= scale;

		gainDb = static_cast<float>(20.0 * std::log10(gain));
		return true;
	}

	bool CaptureFile::save(const std::string& path, const CaptureData& data)
	{
		const int32_t channelCount = static_cast<int32_t>(data.channels.size());
		if (channelCount != 1 && channelCount != 2 && channelCount != 4)
			return false;
		if (data.channels[0].empty() || data.sampleRate <= 0.0)
			return false;

		const int32_t numSamples = static_cast<int32_t>(data.channels[0].size());
		for (const auto& channel : data.channels)
		{
			if (static_cast<int32_t>(channel.size()) != numSamples)
				return false;
		}

		/* Junto las muestras y calculo el CRC antes de abrir el archivo */
		std::vector<float> payload(static_cast<size_t>(channelCount * numSamples));
		for (int32_t c = 0; c < channelCount; ++c)
		{
			std::memcpy(payload.data() + static_cast<size_t>(c * numSamples),
						data.channels[static_cast<size_t>(c)].data(),
						static_cast<size_t>(numSamples) * sizeof(float));
		}
		const auto* bytes = reinterpret_cast<const uint8_t*>(payload.data());
		const size_t byteCount = payload.size() * sizeof(float);

		CaptureHeader header {};
		std::memcpy(header.magic, kMagic, 4);
		header.version = kVersion;
		header.sampleRate = static_cast<uint32_t>(std::lround(data.sampleRate));
		header.channelCount = static_cast<uint32_t>(channelCount);
		header.numSamples = static_cast<uint32_t>(numSamples);
		header.sendDb = data.sendDb;
		header.bandGainDb = data.bandGainDb;
		header.flags = data.bandNormalized ? kFlagBandNormalized : 0u;
		header.crc32 = crc32(bytes, byteCount);

		std::ofstream file(path, std::ios::binary);
		if (!file)
			return false;

		file.write(reinterpret_cast<const char*>(&header), sizeof(header));
		if (!writeText(file, data.name) || !writeText(file, data.device) ||
			!writeText(file, data.author) || !writeText(file, data.date) ||
			!writeText(file, data.notes))
			return false;

		file.write(reinterpret_cast<const char*>(payload.data()),
				   static_cast<std::streamsize>(byteCount));
		return file.good();
	}

	bool CaptureFile::load(const std::string& path, CaptureData& data)
	{
		data = CaptureData {};

		std::ifstream file(path, std::ios::binary);
		if (!file)
			return false;

		CaptureHeader header {};
		file.read(reinterpret_cast<char*>(&header), sizeof(header));
		if (!file || std::memcmp(header.magic, kMagic, 4) != 0 || header.version != kVersion)
			return false;
		if (header.channelCount != 1 && header.channelCount != 2 && header.channelCount != 4)
			return false;
		if (header.numSamples == 0 || header.numSamples > static_cast<uint32_t>(IR_STATE_MAX_SAMPLES))
			return false;

		if (!readText(file, data.name) || !readText(file, data.device) ||
			!readText(file, data.author) || !readText(file, data.date) || !readText(file, data.notes))
			return false;

		/* Leo las muestras y rechazo el archivo si el CRC no cierra */
		const size_t count = static_cast<size_t>(header.channelCount) * header.numSamples;
		std::vector<float> payload(count);
		file.read(reinterpret_cast<char*>(payload.data()),
				  static_cast<std::streamsize>(count * sizeof(float)));
		if (!file)
			return false;

		const auto* bytes = reinterpret_cast<const uint8_t*>(payload.data());
		if (crc32(bytes, count * sizeof(float)) != header.crc32)
			return false;

		data.sampleRate = header.sampleRate;
		data.sendDb = header.sendDb;
		data.bandGainDb = header.bandGainDb;
		data.bandNormalized = (header.flags & kFlagBandNormalized) != 0;
		data.channels.resize(header.channelCount);
		for (uint32_t c = 0; c < header.channelCount; ++c)
		{
			data.channels[c].assign(
				payload.begin() + static_cast<std::ptrdiff_t>(c * header.numSamples),
				payload.begin() + static_cast<std::ptrdiff_t>((c + 1) * header.numSamples));
		}
		return true;
	}

} // namespace DevicesForge
