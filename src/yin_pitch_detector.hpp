#pragma once

#include "pitch_detection.hpp"

#include <cstdint>
#include <vector>

PitchEstimate detectPitchYin(
    const std::vector<double>& samples,
    std::uint32_t sampleRate,
    const PitchDetectionOptions& options
);
