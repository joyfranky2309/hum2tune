#include "pitch_detector.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

PitchEstimate detectPitch(
    const std::vector<double>& samples,
    std::uint32_t sampleRate,
    const PitchDetectionOptions& options
) {
    if (sampleRate == 0 || samples.size() < 2 ||
        options.minFrequency <= 0.0 ||
        options.maxFrequency <= options.minFrequency) {
        return {};
    }

    constexpr std::size_t maxWindowSize = 4096;
    const std::size_t windowSize = std::min(samples.size(), maxWindowSize);
    const std::size_t minLag = std::max<std::size_t>(
        2,
        static_cast<std::size_t>(std::ceil(
            static_cast<double>(sampleRate) / options.maxFrequency
        ))
    );
    const std::size_t maxLag = std::min(
        static_cast<std::size_t>(
            static_cast<double>(sampleRate) / options.minFrequency
        ),
        windowSize / 2
    );

    if (maxLag <= minLag) {
        return {};
    }

    double mean = 0.0;
    for (std::size_t i = 0; i < windowSize; ++i) {
        mean += samples[i];
    }
    mean /= static_cast<double>(windowSize);

    std::vector<double> signal(windowSize);
    for (std::size_t i = 0; i < windowSize; ++i) {
        signal[i] = samples[i] - mean;
    }

    std::vector<double> correlation(maxLag + 1, 0.0);
    for (std::size_t lag = minLag; lag <= maxLag; ++lag) {
        double sum = 0.0;
        double energyA = 0.0;
        double energyB = 0.0;

        for (std::size_t i = 0; i < windowSize - lag; ++i) {
            const double a = signal[i];
            const double b = signal[i + lag];
            sum += a * b;
            energyA += a * a;
            energyB += b * b;
        }

        const double denominator = std::sqrt(energyA * energyB);
        if (denominator > 0.0) {
            correlation[lag] = sum / denominator;
        }
    }

    std::size_t bestLag = 0;
    double bestStrength = 0.0;
    for (std::size_t lag = minLag + 1; lag < maxLag; ++lag) {
        const double strength = correlation[lag];
        if (strength >= correlation[lag - 1] &&
            strength > correlation[lag + 1] &&
            strength > bestStrength) {
            bestLag = lag;
            bestStrength = strength;
        }
    }

    if (bestLag == 0) {
        return {0.0, 0.0};
    }
    if (bestStrength < options.autocorrelationThreshold) {
        return {0.0, bestStrength};
    }

    double refinedLag = static_cast<double>(bestLag);
    const double left = correlation[bestLag - 1];
    const double center = correlation[bestLag];
    const double right = correlation[bestLag + 1];
    const double denominator = left - 2.0 * center + right;
    if (denominator != 0.0) {
        refinedLag += 0.5 * (left - right) / denominator;
    }

    if (refinedLag <= 0.0) {
        return {};
    }

    const double frequency = static_cast<double>(sampleRate) / refinedLag;
    if (frequency < options.minFrequency ||
        frequency > options.maxFrequency) {
        return {0.0, bestStrength};
    }

    return {frequency, bestStrength};
}
