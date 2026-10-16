#include "ONNXInference.h"

#include <onnxruntime_cxx_api.h>
#include <cstring>
#include <cmath>
#include <iostream>

namespace DevicesForge 
{

    ONNXInference::ONNXInference() {}

    ONNXInference::~ONNXInference() 
    {
        destroySession();
    }

    bool ONNXInference::initialize(double sampleRate) 
    {
        this->sampleRate = sampleRate;
        
        try 
        {
            env = new Ort::Env(ORT_LOGGING_LEVEL_WARNING, "DevicesForge");
            return true;
        } 
        catch (const std::exception& e) 
        {
            return false;
        }
    }

    bool ONNXInference::loadModel(const std::string& modelPath) 
    {
        if (env)
        { 
            try 
            {
                destroySession();
                
                Ort::SessionOptions sessionOptions;
                sessionOptions.SetIntraOpNumThreads(1);
                sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_BASIC);

                session = new Ort::Session(
                    *static_cast<Ort::Env*>(env),
                    modelPath.c_str(),
                    sessionOptions
                );

                modelLoaded = true;
                modelName = modelPath;                
            } 
            catch (const Ort::Exception& e)
            {
                modelLoaded = false;
                std::cerr << "[DevicesForge] loadModel failed ("
                          << e.GetOrtErrorCode() << "): " << e.what() << '\n';
            }
            catch (const std::exception& e)
            {
                modelLoaded = false;
                std::cerr << "[DevicesForge] loadModel failed: "
                          << e.what() << '\n';
            }
        }

        return modelLoaded;

    }

    bool ONNXInference::processIR(const float* inputIR, int32_t inputLength,
                                float* outputIR, int32_t outputLength) 
    {

        // Fallback: just copy input to output
        auto fallback = [&]() -> bool
        {
            int32_t copyLength = std::min(inputLength, outputLength);
            std::memcpy(outputIR, inputIR, copyLength * sizeof(float));    
            return false;    
        };
        
        if (!modelLoaded || !session) 
            return fallback();

        try 
        {
            auto sessionPtr = static_cast<Ort::Session*>(session);
            
            // Prepare input tensor
            std::vector<int64_t> inputShape = {1, inputLength};
            Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(
                OrtArenaAllocator, OrtMemTypeDefault);

            Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
                memoryInfo,
                const_cast<float*>(inputIR),
                inputLength,
                inputShape.data(),
                inputShape.size()
            );

            // Run inference
            const char* inputNames[] = {"input"};
            const char* outputNames[] = {"output"};

            auto outputTensors = sessionPtr->Run(
                Ort::RunOptions{nullptr},
                inputNames,
                &inputTensor,
                1,
                outputNames,
                1
            );

            // Copy output
            float* outputData = outputTensors[0].GetTensorMutableData<float>();
            int32_t outputSize = static_cast<int32_t>(outputTensors[0].GetTensorTypeAndShapeInfo().GetElementCount());
            
            int32_t copyLength = std::min(outputSize, outputLength);
            std::memcpy(outputIR, outputData, copyLength * sizeof(float));

            return true;
        } catch (const std::exception& e) 
        {
            return fallback();
        }
    }

    bool ONNXInference::denoiseIR(const float* noisyIR, int32_t length,
                                float* cleanIR) 
	{
        return processIR(noisyIR, length, cleanIR, length);
    }

    bool ONNXInference::createSession(const std::string& modelPath) 
	{
        return loadModel(modelPath);
    }

    void ONNXInference::destroySession() 
	{
        if (session) 
		{
            delete static_cast<Ort::Session*>(session);
            session = nullptr;
        }
        
		if (env) 
		{
            delete static_cast<Ort::Env*>(env);
            env = nullptr;
        }
        
		modelLoaded = false;
		modelName.clear();
    }

} //  DevicesForge

