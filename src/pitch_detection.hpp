#pragma once

struct PitchEstimate {
    double frequency = 0.0;
    double confidence = 0.0;
};

struct PitchDetectionOptions {
    double minFrequency = 80.0;
    double maxFrequency = 1000.0;
    double autocorrelationThreshold = 0.55;
    double yinThreshold = 0.20;
};
