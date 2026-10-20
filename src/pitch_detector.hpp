#pragma once

#include "pitch_detection.hpp"

#include <cstdint>
#include <vector>

PitchEstimate detectPitch(
    const std::vector<double>& samples,
    std::uint32_t sampleRate,
    const PitchDetectionOptions& options
);