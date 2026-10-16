#pragma once

#include <cstdint>
#include <vector>

double detectPitch(
    const std::vector<std::int16_t>& samples,
    std::uint32_t sampleRate
);