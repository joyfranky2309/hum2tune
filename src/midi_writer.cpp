#include "midi_writer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

constexpr std::uint16_t ticksPerQuarterNote = 480;
constexpr std::uint32_t microsecondsPerQuarterNote = 500000;
constexpr std::uint64_t ticksPerSecond =
    ticksPerQuarterNote * 1000000ULL / microsecondsPerQuarterNote;
constexpr std::uint32_t maxVariableLengthQuantity = 0x0FFFFFFF;

struct TimedMidiEvent {
    std::uint64_t tick;
    int midiNote;
    bool noteOn;
};

void appendBigEndian(std::vector<std::uint8_t>& bytes,
                     std::uint32_t value,
                     int byteCount) {
    for (int shift = (byteCount - 1) * 8; shift >= 0; shift -= 8) {
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

void appendVariableLengthQuantity(std::vector<std::uint8_t>& bytes,
                                  std::uint32_t value) {
    if (value > maxVariableLengthQuantity) {
        throw std::runtime_error("MIDI event gap exceeds the maximum delta time.");
    }

    std::uint8_t encoded[4];
    int length = 1;
    encoded[3] = static_cast<std::uint8_t>(value & 0x7F);

    while ((value >>= 7) != 0) {
        encoded[3 - length] =
            static_cast<std::uint8_t>((value & 0x7F) | 0x80);
        ++length;
    }

    bytes.insert(bytes.end(), encoded + 4 - length, encoded + 4);
}

std::uint64_t secondsToTicks(double seconds) {
    const long double ticks =
        static_cast<long double>(seconds) * ticksPerSecond;
    const long double roundedTicks = std::round(ticks);
    if (!std::isfinite(roundedTicks) ||
        roundedTicks > static_cast<long double>(
                           std::numeric_limits<std::uint64_t>::max())) {
        throw std::invalid_argument("Note time is too large to encode in MIDI.");
    }
    return static_cast<std::uint64_t>(roundedTicks);
}

}  // namespace

void writeMidiFile(const std::vector<NoteEvent>& events,
                   const std::string& path) {
    std::vector<TimedMidiEvent> timedEvents;
    timedEvents.reserve(events.size() * 2);

    for (const auto& event : events) {
        if (event.midiNote < 0 || event.midiNote > 127) {
            throw std::invalid_argument("MIDI note number must be between 0 and 127.");
        }
        if (!std::isfinite(event.start) || !std::isfinite(event.end) ||
            event.start < 0.0 || event.end <= event.start) {
            throw std::invalid_argument("MIDI note times must be finite, nonnegative, and end after start.");
        }

        const auto startTick = secondsToTicks(event.start);
        const auto endTick = secondsToTicks(event.end);
        if (endTick <= startTick) {
            throw std::invalid_argument("MIDI note duration is shorter than one tick.");
        }
        timedEvents.push_back({startTick, event.midiNote, true});
        timedEvents.push_back({endTick, event.midiNote, false});
    }

    std::stable_sort(timedEvents.begin(), timedEvents.end(),
        [](const TimedMidiEvent& left, const TimedMidiEvent& right) {
            if (left.tick != right.tick) {
                return left.tick < right.tick;
            }
            return left.noteOn < right.noteOn;
        });

    std::vector<std::uint8_t> track;
    track.reserve(10 + timedEvents.size() * 4);
    appendVariableLengthQuantity(track, 0);
    track.insert(track.end(), {0xFF, 0x51, 0x03});
    appendBigEndian(track, microsecondsPerQuarterNote, 3);

    std::uint64_t previousTick = 0;
    for (const auto& event : timedEvents) {
        const std::uint64_t delta = event.tick - previousTick;
        if (delta > maxVariableLengthQuantity) {
            throw std::runtime_error("MIDI event gap exceeds the maximum delta time.");
        }
        appendVariableLengthQuantity(track, static_cast<std::uint32_t>(delta));
        track.push_back(event.noteOn ? 0x90 : 0x80);
        track.push_back(static_cast<std::uint8_t>(event.midiNote));
        track.push_back(event.noteOn ? 96 : 0);
        previousTick = event.tick;
    }

    appendVariableLengthQuantity(track, 0);
    track.insert(track.end(), {0xFF, 0x2F, 0x00});

    if (track.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("MIDI track is too large to write.");
    }

    std::vector<std::uint8_t> file;
    file.reserve(14 + 8 + track.size());
    file.insert(file.end(), {'M', 'T', 'h', 'd'});
    appendBigEndian(file, 6, 4);
    appendBigEndian(file, 0, 2);
    appendBigEndian(file, 1, 2);
    appendBigEndian(file, ticksPerQuarterNote, 2);
    file.insert(file.end(), {'M', 'T', 'r', 'k'});
    appendBigEndian(file, static_cast<std::uint32_t>(track.size()), 4);
    file.insert(file.end(), track.begin(), track.end());

    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("Could not open MIDI output file: " + path);
    }
    output.write(reinterpret_cast<const char*>(file.data()),
                 static_cast<std::streamsize>(file.size()));
    if (!output) {
        throw std::runtime_error("Could not write MIDI output file: " + path);
    }
}
