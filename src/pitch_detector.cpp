
#include "pitch_detector.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

double detectPitch(
    const std::vector<std::int16_t>& samples,
    std::uint32_t sampleRate
) {
    if (sampleRate == 0 || samples.size() < 2) {
        return 0.0;
    }

    constexpr double minFrequency = 80.0;
    constexpr double maxFrequency = 1000.0;
    constexpr double peakThreshold = 0.55;
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

    std::vector<double> correlation(maxLag + 1, 0.0);

    for (std::size_t i = 0; i < windowSize; ++i) {
        signal[i] = static_cast<double>(samples[i]) - mean;
    }

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
    for (std::size_t lag = minLag + 1; lag < maxLag; ++lag) {
        if (correlation[lag] >= peakThreshold &&
            correlation[lag] >= correlation[lag - 1] &&
            correlation[lag] > correlation[lag + 1]) {
            bestLag = lag;
            break;
        }
    }

    if (bestLag == 0) {
        return 0.0;
    }

    double refinedLag = static_cast<double>(bestLag);
    const double left = correlation[bestLag - 1];
    const double center = correlation[bestLag];
    const double right = correlation[bestLag + 1];
    const double denominator = left - 2.0 * center + right;
    if (denominator != 0.0) {
        refinedLag += 0.5 * (left - right) / denominator;
    }

    return static_cast<double>(sampleRate) / refinedLag;
}
