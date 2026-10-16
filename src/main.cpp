
#include "wav_reader.hpp"
#include "yin_pitch_detector.hpp"

#include <cstddef>
#include <exception>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char* argv[]) {
    try {
        const std::string path =
            argc > 1 ? argv[1] : "test_audio/hum2.wav";

        const WavData wav = readWav(path);

        if (wav.sampleRate == 0) {
            std::cerr << "Invalid sample rate.\n";
            return 1;
        }

        // Analyze 40 ms of audio at a time.
        const std::size_t frameSize =
            static_cast<std::size_t>(wav.sampleRate * 0.040);

        // Start a new frame every 10 ms.
        const std::size_t hopSize =
            static_cast<std::size_t>(wav.sampleRate * 0.010);

        if (wav.samples.size() < frameSize) {
            std::cerr << "Audio is shorter than one frame.\n";
            return 1;
        }

        std::cout << "WAV loaded successfully!\n";
        std::cout << "Sample rate: " << wav.sampleRate << " Hz\n";
        std::cout << "Samples: " << wav.samples.size() << '\n';
        std::cout << "Frame size: " << frameSize << " samples\n";
        std::cout << "Hop size: " << hopSize << " samples\n\n";

        std::cout << "Time (s), Frequency (Hz)\n";
        std::cout << std::fixed << std::setprecision(2);

        for (std::size_t start = 0;
             start + frameSize <= wav.samples.size();
             start += hopSize) {

            std::vector<std::int16_t> frame(
                wav.samples.begin() + start,
                wav.samples.begin() + start + frameSize
            );

            const double frequency =
                detectPitchYin(frame, wav.sampleRate);

            const double time =
                static_cast<double>(start) / wav.sampleRate;

            std::cout << time << ", ";

            if (frequency > 0.0) {
                std::cout << frequency;
            } else {
                std::cout << "unvoiced";
            }

            std::cout << '\n';
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }

    return 0;
}
