#include "AudioAnalyzer.h"

#include <numbers>

namespace als {

void AudioAnalyzer::SetSampleRate(std::uint32_t sampleRate) {
    std::lock_guard lock(mutex_);
    sampleRate_ = sampleRate == 0 ? 48000 : sampleRate;
}

void AudioAnalyzer::Push(const float* samples, std::size_t count) {
    if (!samples || count == 0) {
        return;
    }
    std::lock_guard lock(mutex_);
    for (std::size_t index = 0; index < count; ++index) {
        ring_[writeIndex_] = std::clamp(samples[index], -1.0F, 1.0F);
        writeIndex_ = (writeIndex_ + 1) % ring_.size();
    }
    sampleCount_ = std::min(ring_.size(), sampleCount_ + count);
}

RgbColor AudioAnalyzer::Analyze(Sensitivity sensitivity, int maxBrightness) {
    std::array<float, kFftSize> samples{};
    std::uint32_t sampleRate = 48000;
    std::size_t count = 0;
    {
        std::lock_guard lock(mutex_);
        sampleRate = sampleRate_;
        count = sampleCount_;
        const std::size_t start = (writeIndex_ + ring_.size() - count) % ring_.size();
        const std::size_t padding = ring_.size() - count;
        for (std::size_t index = 0; index < count; ++index) {
            samples[padding + index] = ring_[(start + index) % ring_.size()];
        }
    }

    float rms = 0.0F;
    for (const float sample : samples) {
        rms += sample * sample;
    }
    rms = std::sqrt(rms / static_cast<float>(samples.size()));

    const float sensitivityGain = sensitivity == Sensitivity::Low
                                      ? 0.72F
                                      : (sensitivity == Sensitivity::High ? 1.65F : 1.0F);
    const float gatedRms = std::max(0.0F, rms * sensitivityGain - 0.0015F);
    peakEnvelope_ = std::max(gatedRms, peakEnvelope_ * 0.985F);
    peakEnvelope_ = std::clamp(peakEnvelope_, 0.035F, 0.40F);
    float level = gatedRms <= 0.0005F
                      ? 0.0F
                      : std::clamp(gatedRms / peakEnvelope_, 0.0F, 1.0F);
    level = std::pow(level, 0.70F);
    const float levelSmoothing = level > smoothLevel_ ? 0.58F : 0.18F;
    smoothLevel_ += (level - smoothLevel_) * levelSmoothing;
    lastLevel_.store(smoothLevel_);

    std::array<std::complex<float>, kFftSize> spectrum{};
    for (std::size_t index = 0; index < samples.size(); ++index) {
        const float window = 0.5F - 0.5F * std::cos(
            2.0F * std::numbers::pi_v<float> * static_cast<float>(index) /
            static_cast<float>(samples.size() - 1));
        spectrum[index] = {samples[index] * window, 0.0F};
    }
    Transform(spectrum);

    auto bandEnergy = [&](float lowHz, float highHz) {
        const std::size_t lowBin = std::max<std::size_t>(
            1, static_cast<std::size_t>(lowHz * static_cast<float>(kFftSize) / sampleRate));
        const std::size_t highBin = std::min<std::size_t>(
            kFftSize / 2 - 1,
            static_cast<std::size_t>(highHz * static_cast<float>(kFftSize) / sampleRate));
        float sum = 0.0F;
        for (std::size_t bin = lowBin; bin <= highBin; ++bin) {
            sum += std::norm(spectrum[bin]);
        }
        return std::sqrt(sum / static_cast<float>(std::max<std::size_t>(1, highBin - lowBin + 1)));
    };

    float low = bandEnergy(45.0F, 240.0F);
    float mid = bandEnergy(240.0F, 2200.0F);
    float high = bandEnergy(2200.0F, 10000.0F);
    const float bandMax = std::max({low, mid, high, 0.000001F});
    low = std::pow(low / bandMax, 0.65F);
    mid = std::pow(mid / bandMax, 0.65F);
    high = std::pow(high / bandMax, 0.65F);

    // Bass is warm, vocals are green/cyan, and treble is blue. The same mixed
    // color is sent to every backend so the devices remain visually coherent.
    float red = 1.00F * low + 0.05F * mid + 0.12F * high;
    float green = 0.12F * low + 0.92F * mid + 0.22F * high;
    float blue = 0.02F * low + 0.30F * mid + 1.00F * high;
    const float colorMax = std::max({red, green, blue, 0.000001F});
    red /= colorMax;
    green /= colorMax;
    blue /= colorMax;

    const float brightness = smoothLevel_ * (std::clamp(maxBrightness, 10, 100) / 100.0F);
    red *= brightness * 255.0F;
    green *= brightness * 255.0F;
    blue *= brightness * 255.0F;

    const float attack = 0.58F;
    const float release = 0.18F;
    const auto smooth = [&](float current, float target) {
        return current + (target - current) * (target > current ? attack : release);
    };
    smoothR_ = smooth(smoothR_, red);
    smoothG_ = smooth(smoothG_, green);
    smoothB_ = smooth(smoothB_, blue);

    return {
        static_cast<std::uint8_t>(std::clamp(std::lround(smoothR_), 0L, 255L)),
        static_cast<std::uint8_t>(std::clamp(std::lround(smoothG_), 0L, 255L)),
        static_cast<std::uint8_t>(std::clamp(std::lround(smoothB_), 0L, 255L)),
    };
}

void AudioAnalyzer::Transform(std::array<std::complex<float>, kFftSize>& values) const {
    for (std::size_t index = 1, reversed = 0; index < kFftSize; ++index) {
        std::size_t bit = kFftSize >> 1U;
        for (; reversed & bit; bit >>= 1U) {
            reversed ^= bit;
        }
        reversed ^= bit;
        if (index < reversed) {
            std::swap(values[index], values[reversed]);
        }
    }

    for (std::size_t length = 2; length <= kFftSize; length <<= 1U) {
        const float angle = -2.0F * std::numbers::pi_v<float> / static_cast<float>(length);
        const std::complex<float> step(std::cos(angle), std::sin(angle));
        for (std::size_t offset = 0; offset < kFftSize; offset += length) {
            std::complex<float> weight(1.0F, 0.0F);
            for (std::size_t index = 0; index < length / 2; ++index) {
                const auto even = values[offset + index];
                const auto odd = values[offset + index + length / 2] * weight;
                values[offset + index] = even + odd;
                values[offset + index + length / 2] = even - odd;
                weight *= step;
            }
        }
    }
}

}  // namespace als
