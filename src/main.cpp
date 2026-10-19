
#include "midi_writer.hpp"
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

double calculateRms(const std::vector<std::int16_t>& samples) {
    double sumSquares = 0.0;

    for (const auto sample : samples) {
        const double value = static_cast<double>(sample);
        sumSquares += value * value;
    }

    return std::sqrt(sumSquares / static_cast<double>(samples.size()));
}

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
            argc > 1 ? argv[1] : "test_audio/test.wav";
        const std::string midiPath = argc > 2 ? argv[2] : "output.mid";

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

        std::vector<double> frameRmsValues;
        for (std::size_t start = 0;
             start + frameSize <= wav.samples.size();
             start += hopSize) {
            const std::vector<std::int16_t> frame(
                wav.samples.begin() + start,
                wav.samples.begin() + start + frameSize
            );
            frameRmsValues.push_back(calculateRms(frame));
        }

        constexpr double minimumNoiseFloor = 1.0;
        constexpr double onsetMultiplier = 3.0;
        constexpr double offsetMultiplier = 1.5;
        constexpr double releaseSeconds = 0.10;
        constexpr double noiseFloorRiseRate = 0.001;
        const std::size_t releaseFrames =
            static_cast<std::size_t>(std::ceil(releaseSeconds / hopSeconds));
        double noiseFloor = std::max(
            minimumNoiseFloor,
            *std::min_element(frameRmsValues.begin(), frameRmsValues.end())
        );
        bool noteActive = false;
        std::size_t belowOffsetFrames = 0;
        std::vector<PitchFrame> frames;
        frames.reserve(frameRmsValues.size());

        std::size_t frameIndex = 0;
        for (std::size_t start = 0;
             start + frameSize <= wav.samples.size();
             start += hopSize, ++frameIndex) {

            std::vector<std::int16_t> frame(
                wav.samples.begin() + start,
                wav.samples.begin() + start + frameSize
            );

            const double rms = frameRmsValues[frameIndex];
            const double onsetThreshold = noiseFloor * onsetMultiplier;
            const double offsetThreshold = noiseFloor * offsetMultiplier;

            if (!noteActive && rms >= onsetThreshold) {
                noteActive = true;
            }

            bool isVoiced = noteActive;
            if (noteActive && rms < offsetThreshold) {
                ++belowOffsetFrames;
                if (belowOffsetFrames >= releaseFrames) {
                    noteActive = false;
                    belowOffsetFrames = 0;
                    isVoiced = false;
                }
            } else if (noteActive) {
                belowOffsetFrames = 0;
            }

            const double autocorrelationFrequency = !isVoiced
                ? 0.0
                : detectPitch(frame, wav.sampleRate);
            const double yinFrequency = !isVoiced
                ? 0.0
                : detectPitchYin(frame, wav.sampleRate);

            frames.push_back({
                static_cast<double>(start) / wav.sampleRate,
                autocorrelationFrequency,
                yinFrequency,
                yinFrequency > 0.0 ? frequencyToMidi(yinFrequency) : -1
            });

            if (rms < noiseFloor) {
                noiseFloor = rms;
            } else {
                noiseFloor +=
                    noiseFloorRiseRate * (rms - noiseFloor);
            }
        }

        std::vector<int> pitchNotes;
        pitchNotes.reserve(frames.size());
        for (const auto& frame : frames) {
            pitchNotes.push_back(frame.midiNote);
        }

        constexpr std::size_t octaveContextFrames = 10;
        for (std::size_t i = 0; i < frames.size(); ++i) {
            if (frames[i].autocorrelationFrequency <= 0.0 ||
                frames[i].yinFrequency <= 0.0) {
                continue;
            }

            const int autocorrelationNote =
                frequencyToMidi(frames[i].autocorrelationFrequency);
            const int yinNote = frequencyToMidi(frames[i].yinFrequency);
            if (std::abs(autocorrelationNote - yinNote) < 11 ||
                std::abs(autocorrelationNote - yinNote) > 13) {
                continue;
            }

            const std::size_t contextBegin =
                i > octaveContextFrames ? i - octaveContextFrames : 0;
            const std::size_t contextEnd =
                std::min(frames.size(), i + octaveContextFrames + 1);
            std::vector<int> agreedNotes;
            for (std::size_t j = contextBegin; j < contextEnd; ++j) {
                if (j == i || frames[j].autocorrelationFrequency <= 0.0 ||
                    frames[j].yinFrequency <= 0.0) {
                    continue;
                }

                const int nearbyAutocorrelationNote =
                    frequencyToMidi(frames[j].autocorrelationFrequency);
                const int nearbyYinNote =
                    frequencyToMidi(frames[j].yinFrequency);
                if (std::abs(nearbyAutocorrelationNote - nearbyYinNote) <= 1) {
                    agreedNotes.push_back(static_cast<int>(std::round(
                        (nearbyAutocorrelationNote + nearbyYinNote) / 2.0
                    )));
                }
            }

            if (agreedNotes.size() < 3) {
                continue;
            }

            std::sort(agreedNotes.begin(), agreedNotes.end());
            const int contextNote = agreedNotes[agreedNotes.size() / 2];
            const int autocorrelationDistance =
                std::abs(autocorrelationNote - contextNote);
            const int yinDistance = std::abs(yinNote - contextNote);
            if (autocorrelationDistance <= 2 && yinDistance >= 8) {
                pitchNotes[i] = autocorrelationNote;
            } else if (yinDistance <= 2 && autocorrelationDistance >= 8) {
                pitchNotes[i] = yinNote;
            }
        }

        // Smooth note labels using the median of nearby voiced frames.
        std::vector<int> smoothed;
        smoothed.reserve(pitchNotes.size());

        for (std::size_t i = 0; i < pitchNotes.size(); ++i) {
            std::vector<int> nearby;

            const std::size_t begin = i > 2 ? i - 2 : 0;
            const std::size_t end =
                std::min(pitchNotes.size(), i + 3);

            for (std::size_t j = begin; j < end; ++j) {
                if (pitchNotes[j] >= 0) {
                    nearby.push_back(pitchNotes[j]);
                }
            }

            if (pitchNotes[i] < 0 || nearby.size() < 3) {
                smoothed.push_back(pitchNotes[i]);
            } else {
                std::sort(nearby.begin(), nearby.end());
                smoothed.push_back(nearby[nearby.size() / 2]);
            }
        }

        // Group consecutive equal note labels into runs.
        constexpr double minNoteDuration = 0.10;
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

            if (note >= 0) {
                events.push_back({note, startTime, endTime});
            }

            begin = end;
        }

        // Merge same notes separated by a short gap.
        std::vector<NoteEvent> merged;

        for (const auto& event : events) {
            if (!merged.empty() &&
                merged.back().midiNote == event.midiNote &&
                event.start - merged.back().end <= 0.20) {
                merged.back().end = event.end;
            } else {
                merged.push_back(event);
            }
        }

        merged.erase(
            std::remove_if(
                merged.begin(),
                merged.end(),
                [minNoteDuration](const NoteEvent& event) {
                    return event.end - event.start < minNoteDuration;
                }
            ),
            merged.end()
        );

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

        std::cout << "\nDetected note events (YIN with octave checks):\n";

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

        writeMidiFile(merged, midiPath);
        std::cout << "MIDI file written to " << midiPath << '\n';

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }

    return 0;
}
