# Hum2Tune

Hum2Tune is a small C++ project that reads a WAV audio file, estimates the dominant pitch in short analysis windows, converts voiced frames into note events, and writes the result as a MIDI file.

The project is effectively a pitch-tracking prototype for monophonic humming or sung tones. It is designed around a simple pipeline:

1. Read and downmix 16-bit PCM WAV audio, then high-pass filter it
2. Slice it into Hann-windowed frames (40 ms with a 10 ms hop by default)
3. Estimate pitch with two independent methods:
   - normalized autocorrelation
   - YIN-style difference estimation
4. Gate unvoiced frames using adaptive energy, zero-crossing rate, and detector confidence
5. Confidence-weight the detector estimates and suppress isolated pitch spikes
6. Bridge short same-note dropouts and merge nearby matching note runs
7. Export the events as MIDI

This README explains everything currently in the repository so a new engineer or an LLM can understand the project without having to reverse-engineer the code.

## Project goal

The current code is trying to answer a very specific problem:

> Given a recorded human hum or sung note, determine which note is being sung and produce a MIDI file representing the note timeline.

It is not a full music transcription engine. It is closer to a proof-of-concept note detector for monophonic audio.

## Current project status

At the moment, the project is a working C++ command-line utility that:

- accepts a WAV input file
- analyzes it frame by frame
- prints pitch estimates to stdout
- emits note events to stdout
- writes a MIDI file to disk

The repository includes a known test file and a comparison document showing the detector correctly identifies the reference melody in a generated test sample.

## Build and execution

### Build system

This project uses CMake with a C++20 target.

Files involved:
- `CMakeLists.txt`
- `run.sh`

### Standard build command

```bash
cmake -S . -B build
cmake --build build
```

### Convenience runner

```bash
./run.sh [input.wav] [output.mid]
```

The script:
- configures CMake in `build/`
- builds the executable
- runs `build/hum2tune` with the arguments you passed

### Default behavior

If no arguments are supplied, the program defaults to:

- input: `test_audio/s4.wav`
- output MIDI: `output.mid`

This is set in `src/main.cpp`.

The input and output paths can be followed or preceded by tuning options.
See `./build/hum2tune --help` for all supported controls.

### Example usage

```bash
./run.sh test_audio/hum2tune_known_notes.wav output.mid
```

## Repository structure

```text
hum2tune/
├── CMakeLists.txt
├── run.sh
├── build/                      # generated build artifacts
├── output.mid                  # default output MIDI file if generated
├── src/
│   ├── main.cpp                # main analysis pipeline and terminal output
│   ├── pitch_detection.hpp    # shared detector estimates and options
│   ├── pitch_detector.hpp      # autocorrelation pitch detector declaration
│   ├── pitch_detector.cpp      # normalized autocorrelation estimator
│   ├── yin_pitch_detector.hpp  # YIN detector declaration
│   ├── yin_pitch_detector.cpp  # YIN-style pitch estimator
│   ├── wav_reader.hpp          # WAV reading declaration
│   ├── wav_reader.cpp          # WAV parser
│   ├── midi_writer.hpp         # MIDI export declaration
│   ├── midi_writer.cpp         # MIDI Type 0 file writer
│   └── ...
├── test_audio/
│   ├── hum2tune_known_notes.wav
│   ├── hum2tune_known_notes_reference.txt
│   ├── hum2tune_known_notes_comparison.md
│   ├── meri.wav
│   ├── s2.wav
│   ├── s3.wav
│   ├── s4.wav
│   ├── s5.wav
│   ├── s6.wav
│   ├── saregamaogg.wav
│   ├── test.wav
│   ├── twinkle.wav
│   └── twinkle2r.wav
└── README.md
```

## Source code architecture

### 1. `src/wav_reader.*`

Purpose: read WAV files and extract audio samples.

Code path:
- `WavData readWav(const std::string& path)`
- reads RIFF/WAVE headers
- looks for `fmt ` and `data` chunks
- enforces support for PCM 16-bit WAV
- rejects non-PCM or invalid files

Important behavioral details:
- only 16-bit PCM WAV files are supported
- `channels` must be non-zero
- `sampleRate` must be non-zero
- samples are stored as mono `std::vector<std::int16_t>`; multichannel PCM
  input is downmixed by averaging its channels

This is a very simple audio loader and does not handle many compressed or exotic WAV variants.

### 2. `src/pitch_detector.*`

Purpose: estimate pitch using normalized autocorrelation.

Function:
- `PitchEstimate detectPitch(const std::vector<double>& samples, std::uint32_t sampleRate, const PitchDetectionOptions& options);`

Algorithm summary:
- reject invalid or too-short input
- search a configurable frequency range (defaults: 80 Hz to 1000 Hz)
- limit analysis window to 4096 samples max
- compute the mean of the frame and remove it
- for each lag between minimum and maximum allowed lag, compute normalized correlation
- choose the strongest local peak above the configured threshold
- refine the lag via parabola interpolation around the strongest peak
- return frequency and peak-strength confidence

This is a classic autocorrelation-based pitch detector. It is intentionally tuned for speech/hum-like monophonic signals.

### 3. `src/yin_pitch_detector.*`

Purpose: estimate pitch using a YIN-style difference approach.

Function:
- `PitchEstimate detectPitchYin(const std::vector<double>& samples, std::uint32_t sampleRate, const PitchDetectionOptions& options);`

Algorithm summary:
- uses the same configurable frequency range
- subtract mean from samples
- compute squared differences between signal and delayed copies
- cumulative/normalized difference values are tracked
- detect the first lag where normalized difference drops below threshold
- refine candidate lag with a local quadratic interpolation
- return frequency and confidence derived from the selected minimum difference

The YIN estimator is used as a second opinion and is confidence-weighted against the autocorrelation result in the main pipeline.

### 4. `src/midi_writer.*`

Purpose: write note events to a valid standard MIDI file.

Function:
- `void writeMidiFile(const std::vector<NoteEvent>& events, const std::string& path);`

`NoteEvent` is defined in `midi_writer.hpp` as:

```cpp
struct NoteEvent {
    int midiNote;
    double start;
    double end;
};
```

Behavior:
- validates MIDI note range 0..127
- validates time ranges
- converts seconds to MIDI ticks using a fixed tempo
- writes note-on and note-off events with delta times
- creates a MIDI Type 0 track file

This project writes a standard MIDI file suitable for playback or further editing in DAWs and MIDI tools.

### 5. `src/main.cpp`

This is the orchestration file and the heart of the system.

Main responsibilities:
- open and parse WAV input
- define the analysis frame size and hop size
- loop over audio frames
- call both pitch estimators
- compare and reconcile their outputs
- smooth note labels
- convert pitches to MIDI note numbers
- group consecutive same-note frames into note events
- merge close note runs
- filter out short note events
- print stats to stdout
- write final MIDI output

#### Frame and hop configuration

```cpp
constexpr double frameSeconds = 0.040; // 40 ms
constexpr double hopSeconds = 0.010;  // 10 ms
```

The code slices audio into 40 ms windows, advancing 10 ms each time.

#### Pitch-to-MIDI conversion

```cpp
int frequencyToMidi(double frequency) {
    if (frequency <= 0.0) {
        return -1;
    }

    return static_cast<int>(
        std::round(69.0 + 12.0 * std::log2(frequency / 440.0))
    );
}
```

This converts a frequency to a MIDI note number centered around A4 = 69.

#### Note name conversion

```cpp
std::string noteName(int midiNote)
```

This maps MIDI numbers back to labels such as `C4`, `D#3`, etc. It returns `Unvoiced` for invalid or silent frames.

## Data flow in detail

### Audio ingestion

The program loads the wav file into `WavData`:

```cpp
const WavData wav = readWav(path);
```

Then it ensures:
- `wav.sampleRate != 0`
- the file is long enough to produce at least one frame

### Frame generation

The program downmixes multichannel WAV data to mono, applies a 75 Hz
high-pass filter, then analyzes 40 ms frames with a 10 ms hop. A Hann window
is applied to each frame before both detectors run. The RMS gate adapts to
the quiet end of the recording while respecting the configurable minimum RMS;
zero-crossing rate and detector confidence also participate in voicing.

The `PitchFrame` struct is used to store:

```cpp
struct PitchFrame {
    double time;
    double autocorrelationFrequency;
    double autocorrelationConfidence;
    double yinFrequency;
    double yinConfidence;
    int midiNote;
};
```

### Detector agreement and smoothing

The strongest eligible autocorrelation peak and the first eligible YIN
minimum provide frequency and confidence estimates. Agreement is confidence
weighted; nearby frames where both estimators agree can help resolve octave
disagreements. A single unvoiced frame between matching notes is filled.

### Event grouping

Same-valued note labels are grouped into runs. Same-note events separated by
gaps up to 0.15 seconds are merged by default after sub-minimum blips are
removed, and events shorter than 0.15 seconds are dropped. These values are
configurable to balance fragmentation against preserving repeated notes.

```bash
./run.sh test_audio/meri.wav output.mid \
  --min-frequency 80 --max-frequency 1000 \
  --min-rms 250 --merge-gap 0.15 --min-note-duration 0.15
```

Run `./build/hum2tune --help` for the complete option list.

Then the code:
- drops note events shorter than the configured minimum duration
- outputs note events as a vector of `NoteEvent`

## What the program prints

The program prints a CSV-like summary to stdout, including:

```text
Frame-by-frame pitch estimates (frame 40.00 ms, hop 10.00 ms):
Frame,Time (s),Autocorrelation (Hz),YIN (Hz),
Autocorrelation confidence,YIN confidence,Absolute difference (Hz)
```

Then it prints one line per detected note event:

```text
Detected note events (YIN with octave checks):
C4 | MIDI 60 | start 0.00 s | end 0.39 s | duration 0.39 s
```

After that, it writes output to MIDI:

```text
MIDI file written to output.mid
```

## Expected audio input characteristics

The current detector is tuned for:

- monophonic singing or humming
- reasonably steady pitch
- sample rate above zero
- 16-bit PCM WAV
- single-channel or at least a single active channel in the data block
- target frequencies roughly in the range 80 Hz to 1000 Hz

It is not built for:
- polyphonic music
- highly noisy recordings
- compressed audio formats
- multi-channel complex mixes
- arbitrary file formats without a PCM header

## Known test data and validation

The repository contains a generated reference melody and supporting test documentation.

### Reference melody

From `test_audio/hum2tune_known_notes_reference.txt`:

```text
C4 | 60 | 261.63 | 0.40
D4 | 62 | 293.66 | 0.40
E4 | 64 | 329.63 | 0.60
G4 | 67 | 392.00 | 0.60
E4 | 64 | 329.63 | 0.40
D4 | 62 | 293.66 | 0.40
C4 | 60 | 261.63 | 0.80
```

This is a clean generated melody with silence between notes.

### Comparison notes

The file `test_audio/hum2tune_known_notes_comparison.md` explains that:

- both autocorrelation and YIN detected the melody correctly
- fully-contained note frames were correct 100% of the time on the clean reference
- computed pitch error was extremely low in cents
- the system can reject very low-level noise with an adaptive RMS gate
- boundary timing is approximate because the program timestamps frame starts, not exact waveform boundaries

This acts as the strongest local validation artifact in the repository.

## Important implementation details and caveats

### 1. It is a frame-based estimator

Each note estimate is based on a 40 ms analysis window. The code does not do full waveform analysis on the entire file at once. This means boundaries are approximate and tied to the frame grid.

### 2. It is tuned for monophonic pitch detection

The heuristics are designed for clean hum-like input. They are not a general-purpose music transcription system.

### 3. It uses two detectors and a consensus strategy

Rather than trusting one algorithm blindly, the implementation cross-checks:
- autocorrelation result
- YIN result
- local context agreement
- median smoothing

This improves robustness, but it also introduces algorithmic tuning complexity.

### 4. It is intentionally approximate in timing

The code reports note boundaries based on the start times of the frames that produced the note labels. Since each frame is 40 ms and a hop is 10 ms, note boundaries can be early by several tens of milliseconds.

### 5. Silence handling is heuristic

The program treats a frame as unvoiced when the pitch estimator returns zero or when local checks fail. There is no advanced denoising or voice activity detection beyond the frame-level heuristics in `main.cpp`.

### 6. WAV support is limited

Current restrictions:
- `RIFF`/`WAVE` format only
- PCM (`audioFormat == 1`)
- 16-bit samples
- non-zero channels and sample rate

Anything outside that is rejected.

## Build artifacts and generated files

The `build/` directory contains generated CMake output. It is not source code, but it is important for understanding the project state exactly as built on this machine.

The binary is:

```bash
build/hum2tune
```

This is the executable produced from the C++ sources.

## How to interpret this project as a whole

The repository is best understood as:

> A compact research/prototype pipeline for monophonic pitch estimation and note event extraction from WAV files, implemented in C++ and exported as MIDI.

It is not a modern library project or a large application. It is a focused single-binary tool built for a specific purpose:

- detect note pitch from audio
- validate against generated reference melody
- export note timeline as MIDI

## Typical workflow

A normal user workflow looks like this:

```bash
./run.sh test_audio/hum2tune_known_notes.wav output.mid
```

Then they can:
- inspect the console output for frame estimates and note events
- load `output.mid` into a MIDI editor/player
- compare detected notes to the reference text or expected melody

## Summary for future maintainers or AI readers

If someone reads this repository in the future, they should understand these essentials:

- This is a C++20 CLI pitch detector for monophonic audio.
- It reads WAV audio, not MP3/OGG or streaming input.
- It uses a 40 ms analysis frame and 10 ms hop.
- It estimates pitch using both autocorrelation and YIN heuristics.
- It cross-checks the estimates and smooths labels before producing note events.
- It converts those note events into MIDI.
- It has been validated on a generated reference melody in `test_audio/`.
- The code is a practical prototype, not a polished general-purpose transcription engine.

## Next logical improvements

If this project were extended, the likely next improvements would be:

- support stereo and multichannel WAV handling
- add more robust silence/voicing detection
- support configurable frame size, hop size, and min/max frequency
- add unit tests for pitch detection and MIDI writing
- improve note timing accuracy with better onset/offset estimation
- support additional file formats and streaming input
- add calibration for different vocal ranges and room noise

## Final note

This repository currently represents a focused audio signal-processing prototype that successfully demonstrates note detection and MIDI export from simple recorded hums or generated test tones. It is compact, explainable, and built around a straightforward frame-by-frame pitch-tracking architecture.
