
#include "wav_reader.hpp"

#include <fstream>
#include <stdexcept>
#include <string>

namespace {

std::uint32_t readU32(std::istream& in) {
    unsigned char b[4];

    if (!in.read(reinterpret_cast<char*>(b), 4)) {
        throw std::runtime_error("Unexpected end of WAV file");
    }

    return static_cast<std::uint32_t>(b[0])
         | (static_cast<std::uint32_t>(b[1]) << 8)
         | (static_cast<std::uint32_t>(b[2]) << 16)
         | (static_cast<std::uint32_t>(b[3]) << 24);
}

std::uint16_t readU16(std::istream& in) {
    unsigned char b[2];

    if (!in.read(reinterpret_cast<char*>(b), 2)) {
        throw std::runtime_error("Unexpected end of WAV file");
    }

    return static_cast<std::uint16_t>(b[0])
         | (static_cast<std::uint16_t>(b[1]) << 8);
}

void expectTag(std::istream& in, const char* expected) {
    char tag[4];

    if (!in.read(tag, 4) ||
        tag[0] != expected[0] ||
        tag[1] != expected[1] ||
        tag[2] != expected[2] ||
        tag[3] != expected[3]) {
        throw std::runtime_error(
            std::string("Invalid WAV structure; expected ") + expected
        );
    }
}

} // namespace

WavData readWav(const std::string& path) {
    std::ifstream file(path, std::ios::binary);

    if (!file) {
        throw std::runtime_error("Cannot open WAV file: " + path);
    }

    expectTag(file, "RIFF");
    readU32(file); // RIFF chunk size
    expectTag(file, "WAVE");

    WavData wav;
    bool foundFormat = false;
    bool foundData = false;
    std::uint16_t audioFormat = 0;
    std::uint32_t dataSize = 0;

    while (file && !(foundFormat && foundData)) {
        char chunkId[4];

        if (!file.read(chunkId, 4)) {
            break;
        }

        const std::uint32_t chunkSize = readU32(file);
        const auto chunkStart = file.tellg();

        if (chunkId[0] == 'f' && chunkId[1] == 'm' &&
            chunkId[2] == 't' && chunkId[3] == ' ') {
            audioFormat = readU16(file);
            wav.channels = readU16(file);
            wav.sampleRate = readU32(file);
            readU32(file); // Byte rate
            readU16(file); // Block alignment
            wav.bitsPerSample = readU16(file);
            foundFormat = true;
        } else if (chunkId[0] == 'd' && chunkId[1] == 'a' &&
                   chunkId[2] == 't' && chunkId[3] == 'a') {
            if (foundData) {
                throw std::runtime_error("Multiple data chunks unsupported");
            }

            if (chunkSize % 2 != 0) {
                throw std::runtime_error("Invalid 16-bit audio data size");
            }

            dataSize = chunkSize;
            wav.samples.resize(dataSize / 2);

            for (auto& sample : wav.samples) {
                sample = static_cast<std::int16_t>(readU16(file));
            }

            foundData = true;
        }

        // Chunks may contain extra fields or padding.
        // Move to the next chunk using the declared chunk size.
        file.clear();
        file.seekg(chunkStart + static_cast<std::streamoff>(chunkSize)
                   + static_cast<std::streamoff>(chunkSize % 2));
    }

    if (!foundFormat || !foundData) {
        throw std::runtime_error("WAV is missing fmt or data chunk");
    }

    if (audioFormat != 1 || wav.bitsPerSample != 16 ||
        wav.channels == 0 || wav.sampleRate == 0) {
        throw std::runtime_error(
            "Only PCM 16-bit WAV files are supported"
        );
    }

    return wav;
}
