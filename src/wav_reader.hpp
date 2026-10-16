
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct WavData {
    std::uint16_t channels = 0;
    std::uint32_t sampleRate = 0;
    std::uint16_t bitsPerSample = 0;
    std::vector<std::int16_t> samples;
};

WavData readWav(const std::string& path);
