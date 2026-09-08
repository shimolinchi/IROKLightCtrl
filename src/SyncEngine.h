#pragma once

#include "AudioCapture.h"
#include "AuraController.h"
#include "IrokKeyboard.h"
#include "LampArrayController.h"
#include "Settings.h"

namespace als {

struct EngineStatus {
    bool running{};
    bool paused{};
    bool audioReady{};
    bool keyboardReady{};
    int dynamicLightingDevices{};
    int dynamicLightingAvailable{};
    bool auraReady{};
    int auraDevices{};
    float audioLevel{};
    RgbColor color{};
    std::wstring audioName;
    std::wstring keyboardName;
    std::wstring keyboardFirmware;
    std::wstring auraStatus;
    std::wstring lastError;
};

class SyncEngine final {
public:
    explicit SyncEngine(Settings settings);
    ~SyncEngine();

    SyncEngine(const SyncEngine&) = delete;
    SyncEngine& operator=(const SyncEngine&) = delete;

    void Start();
    void Stop();
    void SetPaused(bool paused);
    void TogglePaused();
    void SetSensitivity(Sensitivity sensitivity);
    void SetLightingMode(LightingMode mode);
    void SetAudioColorMode(AudioColorMode mode);
    void SetPrimaryColor(RgbColor color);
    void SetSecondaryColor(RgbColor color);
    void SetMaxBrightness(int brightness);
    void SetEffectSpeed(int speed);
    void SetReverseDirection(bool reverse);
    void RequestReconnect();

    [[nodiscard]] bool IsPaused() const noexcept { return paused_.load(); }
    [[nodiscard]] Sensitivity GetSensitivity() const noexcept {
        return static_cast<Sensitivity>(sensitivity_.load());
    }
    [[nodiscard]] LightingMode GetLightingMode() const noexcept {
        return static_cast<LightingMode>(lightingMode_.load());
    }
    [[nodiscard]] AudioColorMode GetAudioColorMode() const noexcept {
        return static_cast<AudioColorMode>(audioColorMode_.load());
    }
    [[nodiscard]] RgbColor GetPrimaryColor() const noexcept {
        return RgbColor::FromPacked(primaryColor_.load());
    }
    [[nodiscard]] RgbColor GetSecondaryColor() const noexcept {
        return RgbColor::FromPacked(secondaryColor_.load());
    }
    [[nodiscard]] int GetMaxBrightness() const noexcept { return maxBrightness_.load(); }
    [[nodiscard]] int GetEffectSpeed() const noexcept { return effectSpeed_.load(); }
    [[nodiscard]] bool GetReverseDirection() const noexcept { return reverseDirection_.load(); }
    [[nodiscard]] EngineStatus Status() const;

private:
    void ThreadMain();
    void UpdateStatus(const std::function<void(EngineStatus&)>& update);

    Settings settings_;
    std::thread thread_;
    std::atomic<bool> stop_{};
    std::atomic<bool> paused_{};
    std::atomic<bool> reconnect_{};
    std::atomic<int> sensitivity_{static_cast<int>(Sensitivity::Normal)};
    std::atomic<int> lightingMode_{static_cast<int>(LightingMode::Audio)};
    std::atomic<int> audioColorMode_{static_cast<int>(AudioColorMode::GradientCycle)};
    std::atomic<std::uint32_t> primaryColor_{RgbColor{255, 48, 112}.Packed()};
    std::atomic<std::uint32_t> secondaryColor_{RgbColor{24, 176, 255}.Packed()};
    std::atomic<int> maxBrightness_{100};
    std::atomic<int> effectSpeed_{55};
    std::atomic<bool> reverseDirection_{};
    mutable std::mutex statusMutex_;
    EngineStatus status_;
};

}  // namespace als
