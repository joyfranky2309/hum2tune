#include "yin_pitch_detector.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

double detectPitchYin(
    const std::vector<std::int16_t>& samples,
    std::uint32_t sampleRate
) {
    if (sampleRate == 0 || samples.size() < 2) {
        return 0.0;
    }

    constexpr double minFrequency = 80.0;
    constexpr double maxFrequency = 1000.0;
    constexpr double threshold = 0.20;
    constexpr std::size_t maxWindowSize = 4096;

    const std::size_t windowSize =
        std::min(samples.size(), maxWindowSize);
    const std::size_t minLag = std::max<std::size_t>(
        2,
        static_cast<std::size_t>(
            std::ceil(static_cast<double>(sampleRate) / maxFrequency)
        )
    );
    const std::size_t maxLag = std::min(
        static_cast<std::size_t>(
            static_cast<double>(sampleRate) / minFrequency
        ),
        windowSize / 2
    );

    if (maxLag <= minLag) {
        return 0.0;
    }

    std::vector<double> signal(windowSize);
    double mean = 0.0;
    for (std::size_t i = 0; i < windowSize; ++i) {
        mean += samples[i];
    }
    mean /= static_cast<double>(windowSize);

    double energy = 0.0;
    for (std::size_t i = 0; i < windowSize; ++i) {
        signal[i] = static_cast<double>(samples[i]) - mean;
        energy += signal[i] * signal[i];
    }
    if (energy == 0.0) {
        return 0.0;
    }

    std::vector<double> difference(maxLag + 1, 0.0);
    std::vector<double> normalizedDifference(maxLag + 1, 1.0);
    double cumulativeDifference = 0.0;

    for (std::size_t lag = 1; lag <= maxLag; ++lag) {
        double sum = 0.0;
        for (std::size_t i = 0; i < windowSize - lag; ++i) {
            const double delta = signal[i] - signal[i + lag];
            sum += delta * delta;
        }

        difference[lag] = sum;
        cumulativeDifference += sum;
        normalizedDifference[lag] =
            cumulativeDifference > 0.0
                ? sum * static_cast<double>(lag) / cumulativeDifference
                : 1.0;
    }

    std::size_t bestLag = 0;
    for (std::size_t lag = minLag; lag <= maxLag; ++lag) {
        if (normalizedDifference[lag] < threshold) {
            bestLag = lag;
            while (bestLag < maxLag &&
                   normalizedDifference[bestLag + 1] <
                       normalizedDifference[bestLag]) {
                ++bestLag;
            }
            break;
        }
    }

    if (bestLag == 0) {
        return 0.0;
    }

    double refinedLag = static_cast<double>(bestLag);
    if (bestLag > 1 && bestLag < maxLag) {
        const double left = normalizedDifference[bestLag - 1];
        const double center = normalizedDifference[bestLag];
        const double right = normalizedDifference[bestLag + 1];
        const double denominator = left - 2.0 * center + right;
        if (denominator != 0.0) {
            refinedLag += 0.5 * (left - right) / denominator;
        }
    }

    return static_cast<double>(sampleRate) / refinedLag;
}
