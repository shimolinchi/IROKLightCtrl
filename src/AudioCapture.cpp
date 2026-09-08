#include "AudioCapture.h"

#include "Logger.h"

#include <Functiondiscoverykeys_devpkey.h>
#include <Ksmedia.h>
#include <Propvarutil.h>

namespace als {

AudioCapture::~AudioCapture() {
    Close();
}

bool AudioCapture::Initialize(AudioAnalyzer& analyzer) {
    Close();
    analyzer_ = &analyzer;

    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator),
                                      nullptr,
                                      CLSCTX_ALL,
                                      IID_PPV_ARGS(&enumerator));
    if (FAILED(result)) {
        Logger::Instance().Error(L"WASAPI device enumerator failed: " + HResultMessage(result));
        return false;
    }

    Microsoft::WRL::ComPtr<IMMDevice> device;
    result = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(result)) {
        Logger::Instance().Error(L"No default playback device: " + HResultMessage(result));
        return false;
    }

    Microsoft::WRL::ComPtr<IPropertyStore> properties;
    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties))) {
        PROPVARIANT name;
        PropVariantInit(&name);
        if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &name)) &&
            name.vt == VT_LPWSTR && name.pwszVal) {
            deviceName_ = name.pwszVal;
        }
        PropVariantClear(&name);
    }

    result = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &audioClient_);
    if (FAILED(result)) {
        Logger::Instance().Error(L"WASAPI activation failed: " + HResultMessage(result));
        Close();
        return false;
    }

    result = audioClient_->GetMixFormat(&format_);
    if (FAILED(result) || !format_) {
        Logger::Instance().Error(L"WASAPI mix format failed: " + HResultMessage(result));
        Close();
        return false;
    }

    channels_ = format_->nChannels;
    sampleRate_ = format_->nSamplesPerSec;
    isFloat_ = format_->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    if (format_->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
        format_->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format_);
        isFloat_ = extensible->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    }

    const bool supported = (isFloat_ && format_->wBitsPerSample == 32) ||
                           (!isFloat_ && (format_->wBitsPerSample == 16 ||
                                         format_->wBitsPerSample == 24 ||
                                         format_->wBitsPerSample == 32));
    if (!supported || channels_ == 0) {
        Logger::Instance().Error(L"Unsupported WASAPI mix format");
        Close();
        return false;
    }

    result = audioClient_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                      AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_NOPERSIST,
                                      0,
                                      0,
                                      format_,
                                      nullptr);
    if (FAILED(result)) {
        Logger::Instance().Error(L"WASAPI loopback initialization failed: " + HResultMessage(result));
        Close();
        return false;
    }

    result = audioClient_->GetService(IID_PPV_ARGS(&captureClient_));
    if (FAILED(result)) {
        Logger::Instance().Error(L"WASAPI capture service failed: " + HResultMessage(result));
        Close();
        return false;
    }
    result = audioClient_->Start();
    if (FAILED(result)) {
        Logger::Instance().Error(L"WASAPI capture start failed: " + HResultMessage(result));
        Close();
        return false;
    }

    running_ = true;
    analyzer.SetSampleRate(sampleRate_);
    Logger::Instance().Info(L"Audio capture: " + deviceName_ + L" at " +
                            std::to_wstring(sampleRate_) + L" Hz");
    return true;
}

bool AudioCapture::Pump() {
    if (!running_ || !captureClient_ || !format_ || !analyzer_) {
        return false;
    }

    UINT32 packetFrames = 0;
    HRESULT result = captureClient_->GetNextPacketSize(&packetFrames);
    while (SUCCEEDED(result) && packetFrames > 0) {
        BYTE* data = nullptr;
        UINT32 frames = 0;
        DWORD flags = 0;
        result = captureClient_->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
        if (FAILED(result)) {
            break;
        }

        std::vector<float> mono(frames, 0.0F);
        if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) == 0 && data) {
            for (UINT32 frameIndex = 0; frameIndex < frames; ++frameIndex) {
                const BYTE* frame = data + static_cast<std::size_t>(frameIndex) * format_->nBlockAlign;
                float sum = 0.0F;
                for (std::uint16_t channel = 0; channel < channels_; ++channel) {
                    sum += ReadSample(frame, channel);
                }
                mono[frameIndex] = sum / static_cast<float>(channels_);
            }
        }
        analyzer_->Push(mono.data(), mono.size());
        captureClient_->ReleaseBuffer(frames);
        result = captureClient_->GetNextPacketSize(&packetFrames);
    }

    if (FAILED(result)) {
        Logger::Instance().Error(L"WASAPI capture interrupted: " + HResultMessage(result));
        Close();
        return false;
    }
    return true;
}

float AudioCapture::ReadSample(const BYTE* frame, std::uint16_t channel) const {
    const std::size_t bytesPerSample = format_->wBitsPerSample / 8;
    const BYTE* sample = frame + static_cast<std::size_t>(channel) * bytesPerSample;
    if (isFloat_) {
        float value = 0.0F;
        std::memcpy(&value, sample, sizeof(value));
        return std::isfinite(value) ? value : 0.0F;
    }
    if (format_->wBitsPerSample == 16) {
        std::int16_t value = 0;
        std::memcpy(&value, sample, sizeof(value));
        return static_cast<float>(value) / 32768.0F;
    }
    if (format_->wBitsPerSample == 24) {
        std::int32_t value = static_cast<std::int32_t>(sample[0]) |
                             (static_cast<std::int32_t>(sample[1]) << 8) |
                             (static_cast<std::int32_t>(sample[2]) << 16);
        if (value & 0x00800000) {
            value |= static_cast<std::int32_t>(0xff000000);
        }
        return static_cast<float>(value) / 8388608.0F;
    }
    std::int32_t value = 0;
    std::memcpy(&value, sample, sizeof(value));
    return static_cast<float>(value) / 2147483648.0F;
}

void AudioCapture::Close() {
    if (running_ && audioClient_) {
        audioClient_->Stop();
    }
    running_ = false;
    captureClient_.Reset();
    audioClient_.Reset();
    if (format_) {
        CoTaskMemFree(format_);
        format_ = nullptr;
    }
    analyzer_ = nullptr;
}

}  // namespace als
