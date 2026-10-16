
#include "pitch_detector.hpp"
#include "wav_reader.hpp"
#include "yin_pitch_detector.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

struct PitchFrame {
    double time;
    double autocorrelationFrequency;
    double yinFrequency;
    int midiNote; // -1 means no detected pitch
};

struct NoteEvent {
    int midiNote;
    double start;
    double end;
};

int frequencyToMidi(double frequency) {
    if (frequency <= 0.0) {
        return -1;
    }

    return static_cast<int>(
        std::round(69.0 + 12.0 * std::log2(frequency / 440.0))
    );
}

std::string noteName(int midiNote) {
    if (midiNote < 0) {
        return "Unvoiced";
    }

    const std::vector<std::string> names = {
        "C", "C#", "D", "D#", "E", "F",
        "F#", "G", "G#", "A", "A#", "B"
    };

    return names[midiNote % 12] +
           std::to_string(midiNote / 12 - 1);
}

int main(int argc, char* argv[]) {
    try {
        const std::string path =
            argc > 1 ? argv[1] : "test_audio/audiotest.wav";

        const WavData wav = readWav(path);

        if (wav.sampleRate == 0) {
            std::cerr << "Invalid sample rate.\n";
            return 1;
        }

        constexpr double frameSeconds = 0.040;
        constexpr double hopSeconds = 0.010;

        const std::size_t frameSize =
            static_cast<std::size_t>(wav.sampleRate * frameSeconds);

        const std::size_t hopSize =
            static_cast<std::size_t>(wav.sampleRate * hopSeconds);

        if (wav.samples.size() < frameSize) {
            std::cerr << "Audio is shorter than one frame.\n";
            return 1;
        }

        std::vector<PitchFrame> frames;

        for (std::size_t start = 0;
             start + frameSize <= wav.samples.size();
             start += hopSize) {

            std::vector<std::int16_t> frame(
                wav.samples.begin() + start,
                wav.samples.begin() + start + frameSize
            );

            const double autocorrelationFrequency =
                detectPitch(frame, wav.sampleRate);
            const double yinFrequency =
                detectPitchYin(frame, wav.sampleRate);

            frames.push_back({
                static_cast<double>(start) / wav.sampleRate,
                autocorrelationFrequency,
                yinFrequency,
                yinFrequency > 0.0 ? frequencyToMidi(yinFrequency) : -1
            });
        }

        // Smooth note labels using the median of nearby voiced frames.
        std::vector<int> smoothed;
        smoothed.reserve(frames.size());

        for (std::size_t i = 0; i < frames.size(); ++i) {
            std::vector<int> nearby;

            const std::size_t begin = i > 2 ? i - 2 : 0;
            const std::size_t end =
                std::min(frames.size(), i + 3);

            for (std::size_t j = begin; j < end; ++j) {
                if (frames[j].midiNote >= 0) {
                    nearby.push_back(frames[j].midiNote);
                }
            }

            if (frames[i].midiNote < 0 || nearby.size() < 3) {
                smoothed.push_back(frames[i].midiNote);
            } else {
                std::sort(nearby.begin(), nearby.end());
                smoothed.push_back(nearby[nearby.size() / 2]);
            }
        }

        // Group consecutive equal note labels into runs.
        std::vector<NoteEvent> events;
        std::size_t begin = 0;

        while (begin < smoothed.size()) {
            const int note = smoothed[begin];
            std::size_t end = begin + 1;

            while (end < smoothed.size() &&
                   smoothed[end] == note) {
                ++end;
            }

            const double startTime = frames[begin].time;
            const double endTime =
                frames[end - 1].time + hopSeconds;
            const double duration = endTime - startTime;

            // Initial noise filter; we'll tune this using recordings.
            constexpr double minNoteDuration = 0.15;

            if (note >= 0 && duration >= minNoteDuration) {
                events.push_back({note, startTime, endTime});
            }

            begin = end;
        }

        // Merge same notes separated by a short gap.
        std::vector<NoteEvent> merged;

        for (const auto& event : events) {
            if (!merged.empty() &&
                merged.back().midiNote == event.midiNote &&
                event.start - merged.back().end <= 0.15) {
                merged.back().end = event.end;
            } else {
                merged.push_back(event);
            }
        }

        std::cout << std::fixed << std::setprecision(2);
        std::cout << "Frame-by-frame pitch estimates "
                  << "(frame " << frameSeconds * 1000.0
                  << " ms, hop " << hopSeconds * 1000.0 << " ms):\n";
        std::cout << "Frame,Time (s),Autocorrelation (Hz),YIN (Hz),"
                     "Absolute difference (Hz)\n";

        for (std::size_t i = 0; i < frames.size(); ++i) {
            const auto& frame = frames[i];
            std::cout << i + 1 << ',' << frame.time << ',';

            if (frame.autocorrelationFrequency > 0.0) {
                std::cout << frame.autocorrelationFrequency;
            } else {
                std::cout << "unvoiced";
            }
            std::cout << ',';

            if (frame.yinFrequency > 0.0) {
                std::cout << frame.yinFrequency;
            } else {
                std::cout << "unvoiced";
            }
            std::cout << ',';

            if (frame.autocorrelationFrequency > 0.0 &&
                frame.yinFrequency > 0.0) {
                std::cout << std::abs(
                    frame.autocorrelationFrequency - frame.yinFrequency
                );
            } else {
                std::cout << "N/A";
            }
            std::cout << '\n';
        }

        std::cout << "\nDetected note events (based on YIN):\n";

        for (const auto& event : merged) {
            std::cout
                << noteName(event.midiNote)
                << " | MIDI " << event.midiNote
                << " | start " << event.start << " s"
                << " | end " << event.end << " s"
                << " | duration " << event.end - event.start
                << " s\n";
        }

        if (merged.empty()) {
            std::cout << "No stable note events detected.\n";
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }

    return 0;
}
