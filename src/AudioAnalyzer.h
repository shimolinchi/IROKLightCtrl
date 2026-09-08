#pragma once

#include "Common.h"

#include <array>
#include <complex>

namespace als {

class AudioAnalyzer final {
public:
    static constexpr std::size_t kFftSize = 2048;

    void SetSampleRate(std::uint32_t sampleRate);
    void Push(const float* samples, std::size_t count);
    RgbColor Analyze(Sensitivity sensitivity, int maxBrightness);
    [[nodiscard]] float LastLevel() const noexcept { return lastLevel_.load(); }

private:
    void Transform(std::array<std::complex<float>, kFftSize>& values) const;

    std::mutex mutex_;
    std::array<float, kFftSize> ring_{};
    std::size_t writeIndex_{};
    std::size_t sampleCount_{};
    std::uint32_t sampleRate_{48000};

    float peakEnvelope_{0.08F};
    float smoothR_{};
    float smoothG_{};
    float smoothB_{};
    std::atomic<float> lastLevel_{};
};

}  // namespace als
