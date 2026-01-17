#include "RMVPEPitchDetector.h"
#include <cmath>
#include <algorithm>

RMVPEPitchDetector::RMVPEPitchDetector() = default;

RMVPEPitchDetector::~RMVPEPitchDetector() = default;

bool RMVPEPitchDetector::loadModel(const juce::File& modelPath,
                                    GPUProvider provider,
                                    int deviceId)
{
#ifdef HAVE_ONNXRUNTIME
    try
    {
        // Initialize ONNX Runtime
        onnxEnv = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "RMVPEPitchDetector");

        Ort::SessionOptions sessionOptions;
        sessionOptions.SetIntraOpNumThreads(1);
        sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        // Configure execution provider based on GPU settings
#if defined(_WIN32) && defined(USE_DIRECTML)
        if (provider == GPUProvider::DirectML)
        {
            try {
                sessionOptions.AppendExecutionProvider("DML");
                DBG("RMVPE: DirectML execution provider added");
            } catch (const Ort::Exception& e) {
                DBG("RMVPE: Failed to add DirectML provider, using CPU: " << e.what());
            }
        }
        else
#endif
#ifdef USE_CUDA
        if (provider == GPUProvider::CUDA)
        {
            try {
                OrtCUDAProviderOptions cudaOptions;
                cudaOptions.device_id = deviceId;
                sessionOptions.AppendExecutionProvider_CUDA(cudaOptions);
                DBG("RMVPE: CUDA execution provider added, device: " << deviceId);
            } catch (const Ort::Exception& e) {
                DBG("RMVPE: Failed to add CUDA provider, using CPU: " << e.what());
            }
        }
        else
#endif
        if (provider == GPUProvider::CoreML)
        {
            try {
                sessionOptions.AppendExecutionProvider("CoreML");
                DBG("RMVPE: CoreML execution provider added");
            } catch (const Ort::Exception& e) {
                DBG("RMVPE: Failed to add CoreML provider, using CPU: " << e.what());
            }
        }
        else
        {
            if (provider != GPUProvider::CPU)
            {
                DBG("RMVPE: Using CPU execution provider");
            }
        }

#ifdef _WIN32
        std::wstring modelPathW = modelPath.getFullPathName().toWideCharPointer();
        onnxSession = std::make_unique<Ort::Session>(*onnxEnv, modelPathW.c_str(), sessionOptions);
#else
        std::string modelPathStr = modelPath.getFullPathName().toStdString();
        onnxSession = std::make_unique<Ort::Session>(*onnxEnv, modelPathStr.c_str(), sessionOptions);
#endif

        allocator = std::make_unique<Ort::AllocatorWithDefaultOptions>();

        // Get input/output names
        size_t numInputs = onnxSession->GetInputCount();
        size_t numOutputs = onnxSession->GetOutputCount();

        inputNameStrings.clear();
        outputNameStrings.clear();
        inputNames.clear();
        outputNames.clear();

        for (size_t i = 0; i < numInputs; ++i)
        {
            auto namePtr = onnxSession->GetInputNameAllocated(i, *allocator);
            inputNameStrings.push_back(namePtr.get());
        }

        for (size_t i = 0; i < numOutputs; ++i)
        {
            auto namePtr = onnxSession->GetOutputNameAllocated(i, *allocator);
            outputNameStrings.push_back(namePtr.get());
        }

        for (const auto& name : inputNameStrings)
            inputNames.push_back(name.c_str());
        for (const auto& name : outputNameStrings)
            outputNames.push_back(name.c_str());

        loaded = true;
        DBG("RMVPE model loaded successfully");
        return true;
    }
    catch (const Ort::Exception& e)
    {
        DBG("ONNX Runtime error: " << e.what());
        loaded = false;
        return false;
    }
    catch (const std::exception& e)
    {
        DBG("Error loading RMVPE model: " << e.what());
        loaded = false;
        return false;
    }
#else
    DBG("ONNX Runtime not available");
    return false;
#endif
}

std::vector<float> RMVPEPitchDetector::resampleTo16k(const float* audio, int numSamples, int srcRate)
{
    if (srcRate == SAMPLE_RATE)
    {
        return std::vector<float>(audio, audio + numSamples);
    }

    // Linear interpolation resampling
    double ratio = static_cast<double>(SAMPLE_RATE) / srcRate;
    int outSamples = static_cast<int>(numSamples * ratio);

    std::vector<float> resampled(outSamples);

    for (int i = 0; i < outSamples; ++i)
    {
        double srcPos = i / ratio;
        int srcIdx = static_cast<int>(srcPos);
        double frac = srcPos - srcIdx;

        if (srcIdx + 1 < numSamples)
        {
            resampled[i] = static_cast<float>(audio[srcIdx] * (1.0 - frac) + audio[srcIdx + 1] * frac);
        }
        else if (srcIdx < numSamples)
        {
            resampled[i] = audio[srcIdx];
        }
    }

    return resampled;
}

std::vector<float> RMVPEPitchDetector::decodeF0(const float* hidden, int numFrames, float threshold)
{
    // Decode hidden states to F0 values
    // This matches the Python decode function in export.py
    std::vector<float> f0(numFrames, 0.0f);

    for (int t = 0; t < numFrames; ++t)
    {
        const float* frame = hidden + t * N_CLASS;

        // Find max value and index
        int center = 0;
        float maxVal = frame[0];
        for (int i = 1; i < N_CLASS; ++i)
        {
            if (frame[i] > maxVal)
            {
                maxVal = frame[i];
                center = i;
            }
        }

        // Check threshold (unvoiced detection)
        if (maxVal < threshold)
        {
            f0[t] = 0.0f;
            continue;
        }

        // Local weighted average around center (±4 bins)
        int start = std::max(0, center - 4);
        int end = std::min(N_CLASS, center + 5);

        float weightedSum = 0.0f;
        float weightSum = 0.0f;

        for (int i = start; i < end; ++i)
        {
            // idx_cents = idx * 20 + CONST
            float idxCents = i * 20.0f + CONST;
            weightedSum += frame[i] * idxCents;
            weightSum += frame[i];
        }

        if (weightSum > 0.0f)
        {
            float cents = weightedSum / weightSum;
            // f0 = 10 * 2^(cents/1200)
            f0[t] = 10.0f * std::pow(2.0f, cents / 1200.0f);
        }
        else
        {
            f0[t] = 0.0f;
        }
    }

    return f0;
}

std::vector<float> RMVPEPitchDetector::extractF0(const float* audio, int numSamples,
                                                  int sampleRate, float threshold)
{
#ifdef HAVE_ONNXRUNTIME
    if (!loaded)
    {
        DBG("RMVPE model not loaded");
        return {};
    }

    try
    {
        // Step 1: Resample to 16kHz
        auto audio16k = resampleTo16k(audio, numSamples, sampleRate);

        // Process in chunks to avoid stack overflow for long audio
        // Max chunk: 30 seconds at 16kHz = 480000 samples
        constexpr int MAX_CHUNK_SAMPLES = 16000 * 30;
        constexpr int OVERLAP_SAMPLES = 16000; // 1 second overlap

        if (static_cast<int>(audio16k.size()) <= MAX_CHUNK_SAMPLES)
        {
            // Short audio: process directly
            return extractF0Chunk(audio16k.data(), static_cast<int>(audio16k.size()), threshold);
        }

        // Long audio: process in chunks
        std::vector<float> allF0;
        int pos = 0;
        int totalSamples = static_cast<int>(audio16k.size());

        while (pos < totalSamples)
        {
            int chunkEnd = std::min(pos + MAX_CHUNK_SAMPLES, totalSamples);
            int chunkSize = chunkEnd - pos;

            auto chunkF0 = extractF0Chunk(audio16k.data() + pos, chunkSize, threshold);

            if (pos == 0)
            {
                // First chunk: use all frames
                allF0 = std::move(chunkF0);
            }
            else
            {
                // Subsequent chunks: skip overlap frames
                int overlapFrames = OVERLAP_SAMPLES / HOP_SIZE;
                if (static_cast<int>(chunkF0.size()) > overlapFrames)
                {
                    allF0.insert(allF0.end(), chunkF0.begin() + overlapFrames, chunkF0.end());
                }
            }

            pos += MAX_CHUNK_SAMPLES - OVERLAP_SAMPLES;
        }

        return allF0;
    }
    catch (const Ort::Exception& e)
    {
        DBG("ONNX Runtime error during RMVPE inference: " << e.what());
        return {};
    }
    catch (const std::exception& e)
    {
        DBG("Error during RMVPE F0 extraction: " << e.what());
        return {};
    }
#else
    DBG("ONNX Runtime not available");
    return {};
#endif
}

std::vector<float> RMVPEPitchDetector::extractF0Chunk(const float* audio16k, int numSamples, float threshold)
{
#ifdef HAVE_ONNXRUNTIME
    // Prepare input tensor [1, n_samples]
    std::array<int64_t, 2> waveformShape = {1, static_cast<int64_t>(numSamples)};

    Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(
        OrtAllocatorType::OrtArenaAllocator, OrtMemType::OrtMemTypeDefault);

    Ort::Value waveformTensor = Ort::Value::CreateTensor<float>(
        memoryInfo, const_cast<float*>(audio16k), numSamples,
        waveformShape.data(), waveformShape.size());

    // Threshold tensor
    std::array<int64_t, 1> thresholdShape = {1};
    std::vector<float> thresholdData = {threshold};
    Ort::Value thresholdTensor = Ort::Value::CreateTensor<float>(
        memoryInfo, thresholdData.data(), 1,
        thresholdShape.data(), thresholdShape.size());

    // Run inference
    std::vector<Ort::Value> inputTensors;
    inputTensors.push_back(std::move(waveformTensor));
    inputTensors.push_back(std::move(thresholdTensor));

    auto outputTensors = onnxSession->Run(
        Ort::RunOptions{nullptr},
        inputNames.data(), inputTensors.data(), inputTensors.size(),
        outputNames.data(), outputNames.size());

    // Get output - f0 [1, n_frames]
    float* f0Data = outputTensors[0].GetTensorMutableData<float>();
    auto f0Shape = outputTensors[0].GetTensorTypeAndShapeInfo().GetShape();
    int numFrames = static_cast<int>(f0Shape[1]);

    return std::vector<float>(f0Data, f0Data + numFrames);
#else
    return {};
#endif
}

std::vector<float> RMVPEPitchDetector::extractF0WithProgress(const float* audio, int numSamples,
                                                              int sampleRate, float threshold,
                                                              std::function<void(double)> progressCallback)
{
#ifdef HAVE_ONNXRUNTIME
    if (!loaded)
    {
        DBG("RMVPE model not loaded");
        return {};
    }

    try
    {
        if (progressCallback) progressCallback(0.1);

        // Step 1: Resample to 16kHz
        auto audio16k = resampleTo16k(audio, numSamples, sampleRate);

        if (progressCallback) progressCallback(0.3);

        // Step 2: Prepare input tensor [1, n_samples]
        std::array<int64_t, 2> waveformShape = {1, static_cast<int64_t>(audio16k.size())};

        Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(
            OrtAllocatorType::OrtArenaAllocator, OrtMemType::OrtMemTypeDefault);

        Ort::Value waveformTensor = Ort::Value::CreateTensor<float>(
            memoryInfo, audio16k.data(), audio16k.size(),
            waveformShape.data(), waveformShape.size());

        // Threshold tensor
        std::array<int64_t, 1> thresholdShape = {1};
        std::vector<float> thresholdData = {threshold};
        Ort::Value thresholdTensor = Ort::Value::CreateTensor<float>(
            memoryInfo, thresholdData.data(), 1,
            thresholdShape.data(), thresholdShape.size());

        if (progressCallback) progressCallback(0.5);

        // Step 3: Run inference
        std::vector<Ort::Value> inputTensors;
        inputTensors.push_back(std::move(waveformTensor));
        inputTensors.push_back(std::move(thresholdTensor));

        auto outputTensors = onnxSession->Run(
            Ort::RunOptions{nullptr},
            inputNames.data(), inputTensors.data(), inputTensors.size(),
            outputNames.data(), outputNames.size());

        if (progressCallback) progressCallback(0.9);

        // Step 4: Get output - f0 [1, n_frames]
        float* f0Data = outputTensors[0].GetTensorMutableData<float>();
        auto f0Shape = outputTensors[0].GetTensorTypeAndShapeInfo().GetShape();
        int numFrames = static_cast<int>(f0Shape[1]);

        // Copy f0 values
        std::vector<float> f0(f0Data, f0Data + numFrames);

        if (progressCallback) progressCallback(1.0);

        return f0;
    }
    catch (const Ort::Exception& e)
    {
        DBG("ONNX Runtime error during RMVPE inference: " << e.what());
        return {};
    }
    catch (const std::exception& e)
    {
        DBG("Error during RMVPE F0 extraction: " << e.what());
        return {};
    }
#else
    DBG("ONNX Runtime not available");
    return {};
#endif
}

int RMVPEPitchDetector::getNumFrames(int numSamples, int sampleRate) const
{
    // Convert to 16kHz sample count
    int samples16k = static_cast<int>(numSamples * static_cast<double>(SAMPLE_RATE) / sampleRate);
    // n_frames = T // hop_length + 1
    return samples16k / HOP_SIZE + 1;
}

float RMVPEPitchDetector::getTimeForFrame(int frameIndex) const
{
    return static_cast<float>(frameIndex * HOP_SIZE) / SAMPLE_RATE;
}

int RMVPEPitchDetector::getHopSizeForSampleRate(int sampleRate) const
{
    return static_cast<int>(HOP_SIZE * static_cast<double>(sampleRate) / SAMPLE_RATE);
}
