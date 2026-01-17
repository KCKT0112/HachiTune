#pragma once

#include <vector>
#include <cstddef>

/**
 * Lightweight draw-curve container (frame-indexed, integer values).
 * Mirrors ds-editor-lite DrawCurve enough for freehand pitch drawing.
 *
 * - localStart : first frame index
 * - step       : sample step between consecutive values (frames)
 * - values     : integer values (e.g., pitch in cents)
 */
class DrawCurve
{
public:
    DrawCurve() = default;

    explicit DrawCurve(int startFrame, int stepFrames = 1)
        : start(startFrame), step(stepFrames) {}

    void setLocalStart(int startFrame) { start = startFrame; }
    int localStart() const { return start; }

    void setStep(int s) { step = s > 0 ? s : 1; }
    int getStep() const { return step; }

    const std::vector<int>& values() const { return data; }
    std::vector<int>& mutableValues() { return data; }
    void setValues(std::vector<int> v) { data = std::move(v); }

    void appendValue(int v) { data.push_back(v); }

    void clearValues() { data.clear(); }

    int localEndTick() const { return start + static_cast<int>(data.size()) * step; }

private:
    int start = 0;
    int step = 1;
    std::vector<int> data;
};

