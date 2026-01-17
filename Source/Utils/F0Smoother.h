#pragma once

#include "../JuceHeader.h"
#include <vector>

/**
 * F0 smoothing utilities for better pitch correction quality.
 * Provides median filtering and interpolation for natural-sounding results.
 */
class F0Smoother
{
public:
    /**
     * Apply median filter to F0 values to reduce jitter.
     * @param f0 Input F0 values
     * @param windowSize Median filter window size (should be odd, e.g., 5, 7, 9)
     * @return Smoothed F0 values
     */
    static std::vector<float> medianFilter(const std::vector<float>& f0, int windowSize = 5);
    
    /**
     * Smooth F0 transitions using moving average with voiced/unvoiced awareness.
     * @param f0 Input F0 values
     * @param voicedMask Voiced/unvoiced mask
     * @param windowSize Smoothing window size
     * @return Smoothed F0 values
     */
    static std::vector<float> smoothTransitions(const std::vector<float>& f0,
                                                 const std::vector<bool>& voicedMask,
                                                 int windowSize = 3);
    
    /**
     * Interpolate F0 values across unvoiced regions for smoother synthesis.
     * Uses linear interpolation between voiced segments.
     * @param f0 Input F0 values
     * @param voicedMask Voiced/unvoiced mask
     * @param maxGapFrames Maximum gap to interpolate (frames)
     * @return Interpolated F0 values
     */
    static std::vector<float> interpolateUnvoiced(const std::vector<float>& f0,
                                                    const std::vector<bool>& voicedMask,
                                                    int maxGapFrames = 5);
    
    /**
     * Remove outliers from F0 sequence (sudden jumps that are likely errors).
     * @param f0 Input F0 values
     * @param maxJumpRatio Maximum allowed jump ratio (e.g., 1.5 = 50% change)
     * @return Cleaned F0 values
     */
    static std::vector<float> removeOutliers(const std::vector<float>& f0,
                                              float maxJumpRatio = 1.5f);
    
    /**
     * Comprehensive smoothing pipeline combining all techniques.
     * @param f0 Input F0 values
     * @param voicedMask Voiced/unvoiced mask
     * @return Fully smoothed F0 values
     */
    static std::vector<float> smoothF0(const std::vector<float>& f0,
                                        const std::vector<bool>& voicedMask);
    
private:
    /**
     * Helper: Get median value from a window of F0 values.
     */
    static float getMedian(const std::vector<float>& values, int start, int end);
    
    /**
     * Helper: Check if F0 jump is reasonable.
     */
    static bool isReasonableJump(float f0Prev, float f0Curr, float maxRatio);
};

