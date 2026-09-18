#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <memory>
#include "../plugin/DevicesForge.h"

namespace DevicesForge 
{

	class IRManager 
	{
	public:
		IRManager();
		~IRManager();

		// Load IR from file
		bool loadIR(const std::string& filePath, int32_t levelIndex = 0);

		// Save IR to file
		bool saveIR(const std::string& filePath, int32_t levelIndex = 0);

		// Get IR data
		const float* getIR(int32_t levelIndex = 0) const;
		int32_t getIRLength(int32_t levelIndex = 0) const;

		// Multi-level capture support
		bool hasMultipleLevels() const { return levels.size() > 1; }
		int32_t getNumLevels() const { return static_cast<int32_t>(levels.size()); }

		// Interpolate between levels based on input level
		const float* getInterpolatedIR(float inputLevelDB) const;

		// Clear all IRs
		void clear();

		// Generate test IR (for development)
		void generateTestIR(int32_t length = 1024, float freq = 1000.0f);

	private:
		struct IRLevel 
		{
			std::vector<float> data;
			float levelDB = 0.0f;
			IRMetadata metadata;
		};

		std::vector<IRLevel> levels;
		mutable std::vector<float> interpolatedBuffer;

		int32_t findNearestLevel(float levelDB) const;
		void interpolateIRs(int32_t indexA, int32_t indexB, float t);
	};

} // DevicesForge
