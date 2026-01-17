#include "F0Smoother.h"
#include <algorithm>
#include <cmath>

std::vector<float> F0Smoother::medianFilter(const std::vector<float>& f0, int windowSize)
{
    if (f0.empty() || windowSize < 1)
        return f0;
    
    // Ensure window size is odd
    if (windowSize % 2 == 0)
        windowSize += 1;
    
    int halfWindow = windowSize / 2;
    std::vector<float> smoothed(f0.size());
    
    for (size_t i = 0; i < f0.size(); ++i)
    {
        int start = static_cast<int>(i) - halfWindow;
        int end = static_cast<int>(i) + halfWindow;
        
        // Collect valid F0 values in window (only voiced)
        std::vector<float> windowValues;
        windowValues.reserve(windowSize);
        
        for (int j = start; j <= end; ++j)
        {
            if (j >= 0 && j < static_cast<int>(f0.size()) && f0[j] > 0.0f)
            {
                windowValues.push_back(f0[j]);
            }
        }
        
        if (!windowValues.empty())
        {
            // Sort and get median
            std::sort(windowValues.begin(), windowValues.end());
            size_t mid = windowValues.size() / 2;
            if (windowValues.size() % 2 == 0)
            {
                smoothed[i] = (windowValues[mid - 1] + windowValues[mid]) / 2.0f;
            }
            else
            {
                smoothed[i] = windowValues[mid];
            }
        }
        else
        {
            // No voiced frames in window, keep original (or 0)
            smoothed[i] = f0[i];
        }
    }
    
    return smoothed;
}

std::vector<float> F0Smoother::smoothTransitions(const std::vector<float>& f0,
                                                  const std::vector<bool>& voicedMask,
                                                  int windowSize)
{
    if (f0.empty() || f0.size() != voicedMask.size())
        return f0;
    
    if (windowSize < 1)
        windowSize = 1;
    
    int halfWindow = windowSize / 2;
    std::vector<float> smoothed(f0.size());
    
    for (size_t i = 0; i < f0.size(); ++i)
    {
        if (!voicedMask[i] || f0[i] <= 0.0f)
        {
            smoothed[i] = f0[i];
            continue;
        }
        
        // Weighted average of nearby voiced frames
        float sum = 0.0f;
        float weightSum = 0.0f;
        
        for (int j = -halfWindow; j <= halfWindow; ++j)
        {
            int idx = static_cast<int>(i) + j;
            if (idx >= 0 && idx < static_cast<int>(f0.size()) && 
                voicedMask[idx] && f0[idx] > 0.0f)
            {
                // Gaussian-like weight (closer frames have more weight)
                float weight = std::exp(-0.5f * (j * j) / (halfWindow * halfWindow + 1.0f));
                sum += f0[idx] * weight;
                weightSum += weight;
            }
        }
        
        if (weightSum > 0.0f)
        {
            smoothed[i] = sum / weightSum;
        }
        else
        {
            smoothed[i] = f0[i];
        }
    }
    
    return smoothed;
}

std::vector<float> F0Smoother::interpolateUnvoiced(const std::vector<float>& f0,
                                                     const std::vector<bool>& voicedMask,
                                                     int maxGapFrames)
{
    if (f0.empty() || f0.size() != voicedMask.size())
        return f0;
    
    std::vector<float> interpolated = f0;
    
    // Find unvoiced gaps and interpolate small ones
    size_t gapStart = 0;
    bool inGap = false;
    
    for (size_t i = 0; i < f0.size(); ++i)
    {
        if (!voicedMask[i] && !inGap)
        {
            // Start of gap
            gapStart = i;
            inGap = true;
        }
        else if (voicedMask[i] && inGap)
        {
            // End of gap
            size_t gapEnd = i;
            size_t gapSize = gapEnd - gapStart;
            
            if (gapSize <= static_cast<size_t>(maxGapFrames) && gapStart > 0)
            {
                // Find previous and next voiced frames
                float f0Prev = 0.0f;
                float f0Next = 0.0f;
                
                // Find previous voiced frame
                for (int j = static_cast<int>(gapStart) - 1; j >= 0; --j)
                {
                    if (voicedMask[j] && f0[j] > 0.0f)
                    {
                        f0Prev = f0[j];
                        break;
                    }
                }
                
                // Find next voiced frame
                for (size_t j = gapEnd; j < f0.size(); ++j)
                {
                    if (voicedMask[j] && f0[j] > 0.0f)
                    {
                        f0Next = f0[j];
                        break;
                    }
                }
                
                // Linear interpolation if both ends are available
                if (f0Prev > 0.0f && f0Next > 0.0f)
                {
                    for (size_t j = gapStart; j < gapEnd; ++j)
                    {
                        float t = static_cast<float>(j - gapStart) / gapSize;
                        interpolated[j] = f0Prev * (1.0f - t) + f0Next * t;
                    }
                }
            }
            
            inGap = false;
        }
    }
    
    return interpolated;
}

std::vector<float> F0Smoother::removeOutliers(const std::vector<float>& f0,
                                               float maxJumpRatio)
{
    if (f0.empty())
        return f0;
    
    std::vector<float> cleaned = f0;
    
    for (size_t i = 1; i < f0.size(); ++i)
    {
        if (f0[i] > 0.0f && f0[i - 1] > 0.0f)
        {
            float ratio = f0[i] / f0[i - 1];
            if (ratio > maxJumpRatio || ratio < 1.0f / maxJumpRatio)
            {
                // Outlier detected - use previous value or interpolate
                if (i + 1 < f0.size() && f0[i + 1] > 0.0f)
                {
                    // Interpolate between previous and next
                    cleaned[i] = (f0[i - 1] + f0[i + 1]) / 2.0f;
                }
                else
                {
                    // Use previous value
                    cleaned[i] = f0[i - 1];
                }
            }
        }
    }
    
    return cleaned;
}

std::vector<float> F0Smoother::smoothF0(const std::vector<float>& f0,
                                         const std::vector<bool>& voicedMask)
{
    if (f0.empty())
        return f0;
    
    // Step 1: Remove outliers
    std::vector<float> step1 = removeOutliers(f0, 1.5f);
    
    // Step 2: Median filter
    std::vector<float> step2 = medianFilter(step1, 5);
    
    // Step 3: Smooth transitions
    std::vector<float> step3 = smoothTransitions(step2, voicedMask, 3);
    
    // Step 4: Interpolate small unvoiced gaps
    std::vector<float> step4 = interpolateUnvoiced(step3, voicedMask, 5);
    
    return step4;
}

float F0Smoother::getMedian(const std::vector<float>& values, int start, int end)
{
    if (start < 0) start = 0;
    if (end >= static_cast<int>(values.size())) end = static_cast<int>(values.size()) - 1;
    if (start > end) return 0.0f;
    
    std::vector<float> window;
    window.reserve(end - start + 1);
    
    for (int i = start; i <= end; ++i)
    {
        if (values[i] > 0.0f)
        {
            window.push_back(values[i]);
        }
    }
    
    if (window.empty())
        return 0.0f;
    
    std::sort(window.begin(), window.end());
    size_t mid = window.size() / 2;
    if (window.size() % 2 == 0)
    {
        return (window[mid - 1] + window[mid]) / 2.0f;
    }
    else
    {
        return window[mid];
    }
}

bool F0Smoother::isReasonableJump(float f0Prev, float f0Curr, float maxRatio)
{
    if (f0Prev <= 0.0f || f0Curr <= 0.0f)
        return true;
    
    float ratio = f0Curr / f0Prev;
    return ratio <= maxRatio && ratio >= 1.0f / maxRatio;
}

