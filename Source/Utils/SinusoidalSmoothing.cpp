#include "SinusoidalSmoothing.h"
#include <algorithm>
#include <cmath>

SinusoidalSmoothing::SinusoidalSmoothing(int kernel_size)
    : kernel_size(std::max(kernel_size, 1))
{
    if (this->kernel_size > 1)
    {
        kernel.resize(this->kernel_size);
        double kernel_sum = 0.0;

        // Precompute step value to avoid repeated division
        const double step = 1.0 / (this->kernel_size - 1);

        for (int i = 0; i < this->kernel_size; ++i)
        {
            constexpr double PI = 3.14159265358979323846;
            const double t_val = i * step;
            kernel[i] = std::sin(PI * t_val);
            kernel_sum += kernel[i];
        }

        // Normalize kernel in a single pass
        const double inv_kernel_sum = 1.0 / kernel_sum;
        std::transform(kernel.begin(), kernel.end(), kernel.begin(),
                      [inv_kernel_sum](const double val)
                      {
                          return val * inv_kernel_sum;
                      });
    }
    else
    {
        kernel = {1.0};
    }
}

std::vector<double> SinusoidalSmoothing::forward(const std::vector<double>& x) const
{
    if (kernel_size == 1)
        return x;
    if (x.empty())
        return {};

    const int K = kernel_size;
    const int L = static_cast<int>(x.size());
    const int total_pad = K - 1;
    const int left_pad = total_pad / 2;
    const int right_pad = total_pad - left_pad;

    std::vector<double> padded_x;
    padded_x.reserve(L + total_pad);

    // Left padding: repeat first value
    for (int i = 0; i < left_pad; ++i)
    {
        padded_x.push_back(x[0]);
    }

    // Original data
    padded_x.insert(padded_x.end(), x.begin(), x.end());

    // Right padding: repeat last value
    for (int i = 0; i < right_pad; ++i)
    {
        padded_x.push_back(x.back());
    }

    std::vector<double> output;
    output.reserve(L);

    // Apply convolution
    for (int i = 0; i < L; ++i)
    {
        double conv_sum = 0.0;
        for (int j = 0; j < K; ++j)
        {
            conv_sum += padded_x[i + j] * kernel[j];
        }
        output.push_back(conv_sum);
    }

    return output;
}

std::vector<float> SinusoidalSmoothing::smooth(const std::vector<float>& x) const
{
    std::vector<double> x_double(x.begin(), x.end());
    std::vector<double> smoothed = forward(x_double);
    std::vector<float> result(smoothed.begin(), smoothed.end());
    return result;
}

