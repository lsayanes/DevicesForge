#include "IRManager.h"
#include <cmath>
#include <algorithm>
#include <fstream>

namespace DevicesForge 
{
	IRManager::IRManager() 
	{
		interpolatedBuffer.resize(MAX_IR_LENGTH, 0.0f);
	}

	IRManager::~IRManager() {}

	bool IRManager::loadIR(const std::string& filePath, int32_t levelIndex) 
	{
		// Simple binary loader for development
		// In production, use a proper WAV reader
		
		std::ifstream file(filePath, std::ios::binary);
		if (!file.is_open()) {
			return false;
		}

		// Read header (simplified)
		char header[44];
		file.read(header, 44);

		// Get data size
		int32_t dataSize = 0;
		file.seekg(40, std::ios::beg);
		file.read(reinterpret_cast<char*>(&dataSize), 4);

		int32_t numSamples = dataSize / sizeof(float);
		if (numSamples <= 0 || numSamples > MAX_IR_LENGTH) 
			return false;

		// Resize levels array if needed
		while (static_cast<int32_t>(levels.size()) <= levelIndex) 
			levels.push_back(IRLevel());

		// Read IR data
		levels[levelIndex].data.resize(numSamples);
		file.read(reinterpret_cast<char*>(levels[levelIndex].data.data()), 
				numSamples * sizeof(float));

		// Set metadata
		levels[levelIndex].metadata.length = numSamples;
		levels[levelIndex].metadata.name = filePath;

		++revision;
		return true;
	}

	bool IRManager::saveIR(const std::string& filePath, int32_t levelIndex) 
	{
		if (levelIndex < 0 || levelIndex >= static_cast<int32_t>(levels.size())) 
			return false;

		const auto& ir = levels[levelIndex];
		if (ir.data.empty()) 
			return false;

		std::ofstream file(filePath, std::ios::binary);
		if (!file.is_open()) 
			return false;

		int32_t numSamples = static_cast<int32_t>(ir.data.size());
		int32_t dataSize = numSamples * sizeof(float);
		int32_t fileSize = 36 + dataSize;

		// Write WAV header
		char header[44] = {0};
		
		// RIFF header
		header[0] = 'R'; header[1] = 'I'; header[2] = 'F'; header[3] = 'F';
		std::memcpy(header + 4, &fileSize, 4);
		header[8] = 'W'; header[9] = 'A'; header[10] = 'V'; header[11] = 'E';

		// fmt chunk
		header[12] = 'f'; header[13] = 'm'; header[14] = 't'; header[15] = ' ';
		int32_t chunkSize = 16;
		std::memcpy(header + 16, &chunkSize, 4);
		int16_t audioFormat = 3; // IEEE float
		int16_t numChannels = 1;
		int32_t sampleRate = 48000;
		int32_t byteRate = sampleRate * numChannels * sizeof(float);
		int16_t blockAlign = numChannels * sizeof(float);
		int16_t bitsPerSample = 32;
		
		std::memcpy(header + 20, &audioFormat, 2);
		std::memcpy(header + 22, &numChannels, 2);
		std::memcpy(header + 24, &sampleRate, 4);
		std::memcpy(header + 28, &byteRate, 4);
		std::memcpy(header + 32, &blockAlign, 2);
		std::memcpy(header + 34, &bitsPerSample, 2);

		// data chunk
		header[36] = 'd'; header[37] = 'a'; header[38] = 't'; header[39] = 'a';
		std::memcpy(header + 40, &dataSize, 4);

		file.write(header, 44);
		file.write(reinterpret_cast<const char*>(ir.data.data()), dataSize);

		return true;
	}

	const float* IRManager::getIR(int32_t levelIndex) const 
	{
		if (levelIndex < 0 || levelIndex >= static_cast<int32_t>(levels.size())) 
			return nullptr;

		return levels[levelIndex].data.data();
	}

	int32_t IRManager::getIRLength(int32_t levelIndex) const 
	{
		if (levelIndex < 0 || levelIndex >= static_cast<int32_t>(levels.size())) 
			return 0;

		return static_cast<int32_t>(levels[levelIndex].data.size());
	}

	const float* IRManager::getInterpolatedIR(float inputLevelDB) const 
	{
		if (levels.empty()) 
			return nullptr;

		if (levels.size() == 1) 
			return levels[0].data.data();

		// Find bracketing levels
		int32_t indexA = 0;
		int32_t indexB = 0;

		for (int32_t i = 0; i < static_cast<int32_t>(levels.size()); i++) 
		{
			if (levels[i].levelDB <= inputLevelDB) 
			{
				indexA = i;
				indexB = std::min(i + 1, static_cast<int32_t>(levels.size()) - 1);
			}
		}

		if (indexA == indexB) 
			return levels[indexA].data.data();

		// Calculate interpolation factor
		float rangeA = levels[indexA].levelDB;
		float rangeB = levels[indexB].levelDB;
		float t = (inputLevelDB - rangeA) / (rangeB - rangeA);
		t = std::max(0.0f, std::min(1.0f, t));

		// Interpolate
		int32_t length = std::min(getIRLength(indexA), getIRLength(indexB));
		for (int32_t i = 0; i < length; ++i) 
		{
			interpolatedBuffer[i] = levels[indexA].data[i] * (1.0f - t) +
								levels[indexB].data[i] * t;
		}

		return interpolatedBuffer.data();
	}

	void IRManager::clear() 
	{
		levels.clear();
		++revision;
	}

	void IRManager::setLevelIR(int32_t levelIndex, const std::vector<float>& data, float levelDB)
	{
		if (data.empty())
			return;

		while (static_cast<int32_t>(levels.size()) <= levelIndex)
			levels.push_back(IRLevel());

		levels[levelIndex].data = data;
		levels[levelIndex].levelDB = levelDB;
		levels[levelIndex].metadata.length = static_cast<int32_t>(data.size());
		levels[levelIndex].metadata.levelDB = levelDB;
		levels[levelIndex].metadata.name = "Captured IR";
		++revision;
	}

	void IRManager::generateTestIR(int32_t length, float freq) 
	{
		IRLevel level;
		level.levelDB = 0.0f;
		level.data.resize(length);

		float sampleRate = 48000.0f;
		for (int32_t i = 0; i < length; ++i) {
			float t = static_cast<float>(i) / sampleRate;
			level.data[i] = std::sin(kTwoPiF * freq * t) *
						std::exp(-3.0f * t);
		}

		level.metadata.length = length;
		level.metadata.name = "Test IR";
		levels.push_back(level);
		++revision;
	}

	int32_t IRManager::findNearestLevel(float levelDB) const 
	{
		if (levels.empty()) 
			return 0;

		int32_t nearest = 0;
		float minDist = std::abs(levels[0].levelDB - levelDB);

		for (int32_t i = 1; i < static_cast<int32_t>(levels.size()); i++) 
		{
			float dist = std::abs(levels[i].levelDB - levelDB);
			if (dist < minDist) 
			{
				minDist = dist;
				nearest = i;
			}
		}

		return nearest;
	}

} // namespace DevicesForge
