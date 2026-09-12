#pragma once

#include "AudioAnalyzer.h"

#include <Audioclient.h>
#include <Mmdeviceapi.h>
#include <wrl/client.h>

namespace lightctrl {

class AudioCapture final {
public:
    AudioCapture() = default;
    ~AudioCapture();

    AudioCapture(const AudioCapture&) = delete;
    AudioCapture& operator=(const AudioCapture&) = delete;

    bool Initialize(AudioAnalyzer& analyzer);
    bool Pump();
    void Close();

    [[nodiscard]] const std::wstring& DeviceName() const noexcept { return deviceName_; }
    [[nodiscard]] std::uint32_t SampleRate() const noexcept { return sampleRate_; }

private:
    float ReadSample(const BYTE* frame, std::uint16_t channel) const;

    Microsoft::WRL::ComPtr<IAudioClient> audioClient_;
    Microsoft::WRL::ComPtr<IAudioCaptureClient> captureClient_;
    WAVEFORMATEX* format_{};
    AudioAnalyzer* analyzer_{};
    std::wstring deviceName_;
    std::uint32_t sampleRate_{};
    std::uint16_t channels_{};
    bool isFloat_{};
    bool running_{};
};

}  // namespace lightctrl
