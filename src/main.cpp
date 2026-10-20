
#include "midi_writer.hpp"
#include "pitch_detector.hpp"
#include "wav_reader.hpp"
#include "yin_pitch_detector.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

struct PitchFrame {
    double time;
    double autocorrelationFrequency;
    double autocorrelationConfidence;
    double yinFrequency;
    double yinConfidence;
    int midiNote; // -1 means no detected pitch
};

struct AnalysisConfig {
    std::string inputPath;
    std::string midiPath;
    std::string outputDirectory;
    double frameSeconds = 0.040;
    double hopSeconds = 0.010;
    double minFrequency = 80.0;
    double maxFrequency = 1000.0;
    double minRms = 250.0;
    double minNoteDuration = 0.15;
    double mergeGapSeconds = 0.15;
    double autocorrelationThreshold = 0.55;
    double yinThreshold = 0.20;
    double highPassHz = 75.0;
    double maxZeroCrossingRate = 0.35;
};

struct OutputPaths {
    std::filesystem::path midi;
    std::filesystem::path framesCsv;
    std::filesystem::path notesCsv;
};

std::string noteName(int midiNote);

int frequencyToMidi(double frequency) {
    if (frequency <= 0.0) {
        return -1;
    }

    return static_cast<int>(
        std::round(69.0 + 12.0 * std::log2(frequency / 440.0))
    );
}

void printUsage(const char* program) {
    std::cout
        << "Usage: " << program
        << " [input.wav] [output.mid] [--output-dir DIR] [options]\n"
        << "If paths are omitted, the program asks for the input WAV and output folder.\n"
        << "Options:\n"
        << "  --frame-ms VALUE              Analysis frame length (default 40)\n"
        << "  --hop-ms VALUE                Analysis hop length (default 10)\n"
        << "  --min-frequency HZ            Minimum pitch (default 80)\n"
        << "  --max-frequency HZ            Maximum pitch (default 1000)\n"
        << "  --min-rms VALUE               Minimum adaptive RMS gate (default 250)\n"
        << "  --max-zcr VALUE               Maximum zero-crossing rate (default 0.35)\n"
        << "  --autocorrelation-threshold N Peak confidence (0..1, default 0.55)\n"
        << "  --yin-threshold N             YIN difference threshold (0..1, default 0.20)\n"
        << "  --high-pass HZ                High-pass cutoff (default 75)\n"
        << "  --min-note-duration SEC       Minimum MIDI note duration (default 0.15)\n"
        << "  --merge-gap SEC               Merge same-note gaps (default 0.15)\n"
        << "  --output-dir DIR              Folder for MIDI and analysis reports\n"
        << "  --help                        Show this help\n";
}

double parseNumber(const std::string& value, const std::string& option) {
    std::size_t parsedLength = 0;
    const double result = std::stod(value, &parsedLength);
    if (parsedLength != value.size() || !std::isfinite(result)) {
        throw std::invalid_argument("Invalid value for " + option + ": " + value);
    }
    return result;
}

AnalysisConfig parseArguments(int argc, char* argv[]) {
    AnalysisConfig config;
    bool inputSet = false;
    bool outputSet = false;

    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--help") {
            printUsage(argv[0]);
            std::exit(0);
        }

        const auto readOptionValue = [&]() -> std::string {
            if (i + 1 >= argc) {
                throw std::invalid_argument("Missing value for " + argument);
            }
            return argv[++i];
        };

        if (argument == "--frame-ms") {
            config.frameSeconds =
                parseNumber(readOptionValue(), argument) / 1000.0;
        } else if (argument == "--hop-ms") {
            config.hopSeconds =
                parseNumber(readOptionValue(), argument) / 1000.0;
        } else if (argument == "--min-frequency") {
            config.minFrequency = parseNumber(readOptionValue(), argument);
        } else if (argument == "--max-frequency") {
            config.maxFrequency = parseNumber(readOptionValue(), argument);
        } else if (argument == "--min-rms") {
            config.minRms = parseNumber(readOptionValue(), argument);
        } else if (argument == "--max-zcr") {
            config.maxZeroCrossingRate = parseNumber(
                readOptionValue(), argument
            );
        } else if (argument == "--autocorrelation-threshold") {
            config.autocorrelationThreshold = parseNumber(
                readOptionValue(), argument
            );
        } else if (argument == "--yin-threshold") {
            config.yinThreshold = parseNumber(readOptionValue(), argument);
        } else if (argument == "--high-pass") {
            config.highPassHz = parseNumber(readOptionValue(), argument);
        } else if (argument == "--min-note-duration") {
            config.minNoteDuration = parseNumber(
                readOptionValue(), argument
            );
        } else if (argument == "--merge-gap") {
            config.mergeGapSeconds = parseNumber(readOptionValue(), argument);
        } else if (argument == "--output-dir") {
            config.outputDirectory = readOptionValue();
        } else if (!argument.empty() && argument[0] == '-') {
            throw std::invalid_argument("Unknown option: " + argument);
        } else if (!inputSet) {
            config.inputPath = argument;
            inputSet = true;
        } else if (!outputSet) {
            config.midiPath = argument;
            outputSet = true;
        } else {
            throw std::invalid_argument("Unexpected argument: " + argument);
        }
    }

    if (config.frameSeconds <= 0.0 || config.hopSeconds <= 0.0 ||
        config.hopSeconds > config.frameSeconds ||
        config.minFrequency < 20.0 ||
        config.maxFrequency <= config.minFrequency ||
        config.minRms < 0.0 ||
        config.maxZeroCrossingRate < 0.0 ||
        config.maxZeroCrossingRate > 1.0 ||
        config.autocorrelationThreshold < 0.0 ||
        config.autocorrelationThreshold > 1.0 ||
        config.yinThreshold <= 0.0 ||
        config.yinThreshold > 1.0 ||
        config.highPassHz < 0.0 ||
        config.minNoteDuration < 0.0 ||
        config.mergeGapSeconds < 0.0) {
        throw std::invalid_argument("One or more options are outside valid ranges");
    }

    return config;
}

std::string promptForPath(const std::string& prompt) {
    std::cout << prompt;
    std::string path;
    if (!std::getline(std::cin, path) || path.empty()) {
        throw std::runtime_error("A path is required.");
    }
    return path;
}

OutputPaths prepareOutputPaths(const AnalysisConfig& config) {
    namespace fs = std::filesystem;

    fs::path directory;
    const bool hasOutputDirectory = !config.outputDirectory.empty();
    if (hasOutputDirectory) {
        directory = config.outputDirectory;
    } else if (!config.midiPath.empty()) {
        const fs::path midiArgument(config.midiPath);
        directory = midiArgument.has_parent_path()
            ? midiArgument.parent_path()
            : fs::current_path();
    } else {
        directory = promptForPath("Output folder for MIDI and reports: ");
    }

    if (directory.empty()) {
        directory = fs::current_path();
    }
    fs::create_directories(directory);
    if (!fs::is_directory(directory)) {
        throw std::runtime_error(
            "Output path is not a folder: " + directory.string()
        );
    }
    directory = fs::absolute(directory);

    const fs::path inputPath(config.inputPath);
    const std::string baseName = inputPath.stem().empty()
        ? "hum2tune"
        : inputPath.stem().string();

    fs::path midiPath;
    if (!config.midiPath.empty()) {
        const fs::path requestedMidi(config.midiPath);
        midiPath = hasOutputDirectory
            ? directory / requestedMidi.filename()
            : requestedMidi;
        if (midiPath.parent_path().empty()) {
            midiPath = directory / midiPath;
        }
    } else {
        midiPath = directory / (baseName + ".mid");
    }

    return {
        midiPath,
        directory / (baseName + "_frames.csv"),
        directory / (baseName + "_notes.csv")
    };
}

void writeAnalysisReports(
    const OutputPaths& paths,
    const std::vector<PitchFrame>& frames,
    const std::vector<int>& processedNotes,
    const std::vector<NoteEvent>& notes
) {
    std::ofstream frameOutput(paths.framesCsv);
    if (!frameOutput) {
        throw std::runtime_error(
            "Could not open frame report: " + paths.framesCsv.string()
        );
    }
    frameOutput << "frame,time_seconds,autocorrelation_hz,yin_hz,"
                   "autocorrelation_confidence,yin_confidence,"
                   "absolute_difference_hz,processed_midi_note\n"
                << std::fixed << std::setprecision(6);
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const auto& frame = frames[i];
        frameOutput << i + 1 << ',' << frame.time << ',';
        if (frame.autocorrelationFrequency > 0.0) {
            frameOutput << frame.autocorrelationFrequency;
        }
        frameOutput << ',';
        if (frame.yinFrequency > 0.0) {
            frameOutput << frame.yinFrequency;
        }
        frameOutput << ',' << frame.autocorrelationConfidence << ','
                    << frame.yinConfidence << ',';
        if (frame.autocorrelationFrequency > 0.0 &&
            frame.yinFrequency > 0.0) {
            frameOutput << std::abs(
                frame.autocorrelationFrequency - frame.yinFrequency
            );
        }
        frameOutput << ',' << processedNotes[i] << '\n';
    }
    if (!frameOutput) {
        throw std::runtime_error(
            "Could not write frame report: " + paths.framesCsv.string()
        );
    }

    std::ofstream notesOutput(paths.notesCsv);
    if (!notesOutput) {
        throw std::runtime_error(
            "Could not open note report: " + paths.notesCsv.string()
        );
    }
    notesOutput << "note,midi_note,start_seconds,end_seconds,duration_seconds\n"
                << std::fixed << std::setprecision(6);
    for (const auto& note : notes) {
        notesOutput << noteName(note.midiNote) << ',' << note.midiNote << ','
                    << note.start << ',' << note.end << ','
                    << note.end - note.start << '\n';
    }
    if (!notesOutput) {
        throw std::runtime_error(
            "Could not write note report: " + paths.notesCsv.string()
        );
    }
}

std::vector<double> applyHighPass(
    const std::vector<std::int16_t>& samples,
    std::uint32_t sampleRate,
    double cutoffHz
) {
    std::vector<double> filtered(samples.size());
    if (cutoffHz <= 0.0) {
        for (std::size_t i = 0; i < samples.size(); ++i) {
            filtered[i] = samples[i];
        }
        return filtered;
    }

    const double dt = 1.0 / sampleRate;
    const double rc = 1.0 / (2.0 * std::acos(-1.0) * cutoffHz);
    const double alpha = rc / (rc + dt);
    double previousInput = 0.0;
    double previousOutput = 0.0;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const double input = samples[i];
        const double output = alpha * (previousOutput + input - previousInput);
        filtered[i] = output;
        previousInput = input;
        previousOutput = output;
    }
    return filtered;
}

double median(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
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
        AnalysisConfig config = parseArguments(argc, argv);
        if (config.inputPath.empty()) {
            config.inputPath = promptForPath("Input WAV file: ");
        }
        const WavData wav = readWav(config.inputPath);

        if (wav.sampleRate == 0) {
            std::cerr << "Invalid sample rate.\n";
            return 1;
        }
        if (config.maxFrequency >= wav.sampleRate / 2.0 ||
            config.highPassHz >= wav.sampleRate / 2.0) {
            throw std::invalid_argument(
                "Frequency limits and high-pass cutoff must be below Nyquist"
            );
        }
        const OutputPaths outputPaths = prepareOutputPaths(config);

        const double frameSamples =
            wav.sampleRate * config.frameSeconds;
        const double hopSamples =
            wav.sampleRate * config.hopSeconds;
        if (!std::isfinite(frameSamples) || !std::isfinite(hopSamples) ||
            frameSamples >=
                static_cast<double>(std::numeric_limits<std::size_t>::max()) ||
            hopSamples >=
                static_cast<double>(std::numeric_limits<std::size_t>::max())) {
            throw std::invalid_argument("Frame and hop sizes are too large");
        }
        const std::size_t frameSize =
            static_cast<std::size_t>(frameSamples);
        const std::size_t hopSize = static_cast<std::size_t>(hopSamples);

        if (frameSize < 2 || hopSize == 0 ||
            wav.samples.size() < frameSize) {
            std::cerr << "Audio is shorter than one frame.\n";
            return 1;
        }

        const std::vector<double> filteredSamples =
            applyHighPass(wav.samples, wav.sampleRate, config.highPassHz);

        struct FrameMetrics {
            double rms;
            double zeroCrossingRate;
        };
        std::vector<FrameMetrics> metrics;
        std::vector<double> rmsValues;
        for (std::size_t start = 0;
             start + frameSize <= filteredSamples.size();
             start += hopSize) {
            double energy = 0.0;
            std::size_t crossings = 0;
            for (std::size_t i = 0; i < frameSize; ++i) {
                const double sample = filteredSamples[start + i];
                energy += sample * sample;
                if (i > 0 &&
                    std::signbit(sample) !=
                        std::signbit(filteredSamples[start + i - 1])) {
                    ++crossings;
                }
            }
            const double rms = std::sqrt(energy / frameSize);
            const double zcr =
                static_cast<double>(crossings) / (frameSize - 1);
            metrics.push_back({rms, zcr});
            rmsValues.push_back(rms);
        }

        std::vector<double> sortedRms = rmsValues;
        std::sort(sortedRms.begin(), sortedRms.end());
        const double noiseFloor =
            sortedRms[static_cast<std::size_t>((sortedRms.size() - 1) * 0.10)];
        const double medianRms = median(rmsValues);
        // Keep the adaptive gate below typical signal level when no silence exists.
        const double adaptiveRmsThreshold = std::max(
            config.minRms,
            std::min(noiseFloor * 1.5, medianRms * 0.8)
        );

        const PitchDetectionOptions detectorOptions{
            config.minFrequency,
            config.maxFrequency,
            config.autocorrelationThreshold,
            config.yinThreshold
        };
        std::vector<PitchFrame> frames;
        frames.reserve(metrics.size());
        for (std::size_t frameIndex = 0, start = 0;
             frameIndex < metrics.size();
             ++frameIndex, start += hopSize) {
            PitchEstimate autocorrelation;
            PitchEstimate yin;
            if (metrics[frameIndex].rms >= adaptiveRmsThreshold &&
                metrics[frameIndex].zeroCrossingRate <=
                    config.maxZeroCrossingRate) {
                std::vector<double> windowedFrame(frameSize);
                for (std::size_t i = 0; i < frameSize; ++i) {
                    const double hann = 0.5 * (
                        1.0 - std::cos(
                            2.0 * std::acos(-1.0) * i / (frameSize - 1)
                        )
                    );
                    windowedFrame[i] = filteredSamples[start + i] * hann;
                }
                autocorrelation =
                    detectPitch(windowedFrame, wav.sampleRate, detectorOptions);
                yin = detectPitchYin(
                    windowedFrame, wav.sampleRate, detectorOptions
                );
            }

            const bool voiced =
                (autocorrelation.frequency > 0.0 &&
                 autocorrelation.confidence >=
                     config.autocorrelationThreshold) ||
                (yin.frequency > 0.0 &&
                 yin.confidence >= 1.0 - config.yinThreshold);
            if (!voiced) {
                autocorrelation.frequency = 0.0;
                yin.frequency = 0.0;
            }
            frames.push_back({
                static_cast<double>(start) / wav.sampleRate,
                autocorrelation.frequency,
                autocorrelation.confidence,
                yin.frequency,
                yin.confidence,
                yin.frequency > 0.0
                    ? frequencyToMidi(yin.frequency)
                    : autocorrelation.frequency > 0.0
                        ? frequencyToMidi(autocorrelation.frequency)
                        : -1
            });
        }

        std::vector<int> pitchNotes;
        pitchNotes.reserve(frames.size());
        for (const auto& frame : frames) {
            pitchNotes.push_back(frame.midiNote);
        }

        constexpr std::size_t octaveContextFrames = 10;
        for (std::size_t i = 0; i < frames.size(); ++i) {
            const bool hasAutocorrelation =
                frames[i].autocorrelationFrequency > 0.0;
            const bool hasYin = frames[i].yinFrequency > 0.0;
            if (!hasAutocorrelation && !hasYin) {
                continue;
            }

            const int autocorrelationNote = hasAutocorrelation
                ? frequencyToMidi(frames[i].autocorrelationFrequency)
                : -1;
            const int yinNote = hasYin
                ? frequencyToMidi(frames[i].yinFrequency)
                : -1;

            int currentNote = -1;
            if (hasYin) {
                currentNote = yinNote;
            } else if (hasAutocorrelation) {
                currentNote = autocorrelationNote;
            }

            if (hasAutocorrelation && hasYin) {
                if (std::abs(autocorrelationNote - yinNote) <= 1) {
                    const double confidenceSum =
                        frames[i].autocorrelationConfidence +
                        frames[i].yinConfidence;
                    currentNote = confidenceSum > 0.0
                        ? static_cast<int>(std::round(
                            (autocorrelationNote *
                                 frames[i].autocorrelationConfidence +
                             yinNote * frames[i].yinConfidence) /
                            confidenceSum
                        ))
                        : yinNote;
                } else {
                    currentNote =
                        frames[i].autocorrelationConfidence >
                                frames[i].yinConfidence
                            ? autocorrelationNote
                            : yinNote;
                }
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

            if (agreedNotes.size() >= 3) {
                std::sort(agreedNotes.begin(), agreedNotes.end());
                const int contextNote = agreedNotes[agreedNotes.size() / 2];
                const int autocorrelationDistance =
                    std::abs(autocorrelationNote - contextNote);
                const int yinDistance = std::abs(yinNote - contextNote);
                if (autocorrelationDistance <= 2 && yinDistance >= 8) {
                    currentNote = autocorrelationNote;
                } else if (yinDistance <= 2 && autocorrelationDistance >= 8) {
                    currentNote = yinNote;
                }
            }

            pitchNotes[i] = currentNote;
        }

        const std::vector<int> unsuppressedPitchNotes = pitchNotes;
        for (std::size_t i = 0; i < unsuppressedPitchNotes.size(); ++i) {
            const int currentNote = unsuppressedPitchNotes[i];
            if (currentNote < 0) {
                continue;
            }

            int previousValid = -1;
            for (std::ptrdiff_t j = static_cast<std::ptrdiff_t>(i) - 1; j >= 0; --j) {
                const int note =
                    unsuppressedPitchNotes[static_cast<std::size_t>(j)];
                if (note >= 0) {
                    previousValid = note;
                    break;
                }
            }

            int nextValid = -1;
            for (std::size_t j = i + 1; j < unsuppressedPitchNotes.size(); ++j) {
                if (unsuppressedPitchNotes[j] >= 0) {
                    nextValid = unsuppressedPitchNotes[j];
                    break;
                }
            }

            if (previousValid >= 0 && nextValid >= 0 &&
                std::abs(currentNote - previousValid) > 2 &&
                std::abs(currentNote - nextValid) > 2) {
                pitchNotes[i] = -1;
            }
        }

        // Fill one-frame dropouts only when both surrounding notes agree.
        std::vector<int> smoothed;
        smoothed.reserve(pitchNotes.size());

        for (std::size_t i = 0; i < pitchNotes.size(); ++i) {
            if (pitchNotes[i] < 0 && i > 0 &&
                i + 1 < pitchNotes.size() &&
                pitchNotes[i - 1] >= 0 &&
                pitchNotes[i - 1] == pitchNotes[i + 1]) {
                smoothed.push_back(pitchNotes[i - 1]);
            } else {
                smoothed.push_back(pitchNotes[i]);
            }
        }

        // Group consecutive equal note labels and merge brief same-note dropouts.
        std::vector<NoteEvent> events;
        for (std::size_t begin = 0; begin < smoothed.size();) {
            const int note = smoothed[begin];
            std::size_t end = begin + 1;
            while (end < smoothed.size() && smoothed[end] == note) {
                ++end;
            }
            if (note >= 0) {
                events.push_back({
                    note,
                    frames[begin].time,
                    frames[end - 1].time + config.hopSeconds
                });
            }
            begin = end;
        }

        events.erase(
            std::remove_if(
                events.begin(),
                events.end(),
                [&config](const NoteEvent& event) {
                    return event.end - event.start < config.minNoteDuration;
                }
            ),
            events.end()
        );

        std::vector<NoteEvent> merged;
        for (const auto& event : events) {
            if (!merged.empty() &&
                merged.back().midiNote == event.midiNote &&
                event.start - merged.back().end <=
                    config.mergeGapSeconds + 1e-9) {
                merged.back().end = event.end;
            } else {
                merged.push_back(event);
            }
        }

        std::cout << std::fixed << std::setprecision(2);
        std::cout << "Frame-by-frame pitch estimates "
                  << "(frame " << config.frameSeconds * 1000.0
                  << " ms, hop " << config.hopSeconds * 1000.0 << " ms):\n";
        std::cout << "Frame,Time (s),Autocorrelation (Hz),YIN (Hz),"
                     "Autocorrelation confidence,YIN confidence,"
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

            std::cout << frame.autocorrelationConfidence << ','
                      << frame.yinConfidence << ',';

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

        std::cout << "\nDetected note events (confidence-weighted):\n";

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

        writeMidiFile(merged, outputPaths.midi.string());
        writeAnalysisReports(outputPaths, frames, smoothed, merged);
        std::cout << "MIDI file written to " << outputPaths.midi << '\n'
                  << "Frame report written to " << outputPaths.framesCsv << '\n'
                  << "Note report written to " << outputPaths.notesCsv << '\n';

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }

    return 0;
}
