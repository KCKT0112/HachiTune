#pragma once

#include <vector>

/**
 * Sinusoidal smoothing convolution 1D filter.
 * Based on ds-editor-lite's CurveUtil::SinusoidalSmoothingConv1d.
 * 
 * This creates a smooth pitch curve by applying a sinusoidal kernel
 * convolution, which helps reduce artifacts in pitch inference results.
 */
class SinusoidalSmoothing
{
public:
    /**
     * @param kernel_size Size of the smoothing kernel (must be >= 1)
     *                    Larger values = more smoothing
     */
    explicit SinusoidalSmoothing(int kernel_size);
    
    /**
     * Apply smoothing to input values
     * @param x Input values to smooth
     * @return Smoothed output values
     */
    std::vector<double> forward(const std::vector<double>& x) const;
    
    /**
     * Apply smoothing to float input values (convenience method)
     * @param x Input values to smooth
     * @return Smoothed output values
     */
    std::vector<float> smooth(const std::vector<float>& x) const;

private:
    int kernel_size;
    std::vector<double> kernel;
};

