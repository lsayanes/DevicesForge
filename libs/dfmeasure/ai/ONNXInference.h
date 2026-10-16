#pragma once


#include <string>
#include <vector>
#include <cstdint>
#include <memory>

namespace DevicesForge 
{

	class ONNXInference 
	{
	public:
		ONNXInference();
		~ONNXInference();

		bool initialize(double sampleRate);
		bool loadModel(const std::string& modelPath);

		bool processIR(const float* inputIR, int32_t inputLength,
					float* outputIR, int32_t outputLength);

		// Denoise IR
		bool denoiseIR(const float* noisyIR, int32_t length,
					float* cleanIR);

		inline bool isModelLoaded() const { return modelLoaded; }
		inline std::string getModelName() const { return modelName; }

	private:
		double sampleRate  { 48000.0 };
		bool modelLoaded  { false };
		std::string modelName;
		
		// ONNX Runtime session (opaque pointer)
		void* session { nullptr };
		void* env  { nullptr };

		// Buffer for processing
		std::vector<float> processingBuffer;
		std::vector<float> outputBuffer;

		bool createSession(const std::string& modelPath);
		void destroySession();
	};

} // DevicesForge

