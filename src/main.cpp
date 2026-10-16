#include "wav_reader.hpp"
#include "pitch_detector.hpp"

#include <exception>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    try {
        const std::string path =
            argc > 1 ? argv[1] : "test_audio/tone.wav";

        const WavData wav = readWav(path);

        const double pitch = detectPitch(wav.samples, wav.sampleRate);

        std::cout << "WAV loaded successfully!\n";
        std::cout << "Sample rate: " << wav.sampleRate << " Hz\n";
        std::cout << "Samples: " << wav.samples.size() << '\n';
        std::cout << "Detected pitch: " << pitch << " Hz\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }

    return 0;
}