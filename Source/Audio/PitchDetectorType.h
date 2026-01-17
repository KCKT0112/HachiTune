#pragma once

#include "../JuceHeader.h"

/**
 * Pitch detector types available for F0 extraction.
 */
enum class PitchDetectorType
{
    RMVPE = 0,  // Default - Robust Model for Vocal Pitch Estimation
    FCPE,       // Fast Context-aware Pitch Estimation
    YIN         // Traditional YIN algorithm (fallback)
};

/**
 * Convert PitchDetectorType to string for display/storage.
 */
inline const char* pitchDetectorTypeToString(PitchDetectorType type)
{
    switch (type)
    {
        case PitchDetectorType::RMVPE: return "RMVPE";
        case PitchDetectorType::FCPE:  return "FCPE";
        case PitchDetectorType::YIN:   return "YIN";
        default: return "RMVPE";
    }
}

/**
 * Convert string to PitchDetectorType.
 */
inline PitchDetectorType stringToPitchDetectorType(const juce::String& str)
{
    if (str == "FCPE")  return PitchDetectorType::FCPE;
    if (str == "YIN")   return PitchDetectorType::YIN;
    return PitchDetectorType::RMVPE;  // Default
}
