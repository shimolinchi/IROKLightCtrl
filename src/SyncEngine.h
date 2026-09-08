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
    void RequestReconnect();

    [[nodiscard]] bool IsPaused() const noexcept { return paused_.load(); }
    [[nodiscard]] Sensitivity GetSensitivity() const noexcept {
        return static_cast<Sensitivity>(sensitivity_.load());
    }
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
    mutable std::mutex statusMutex_;
    EngineStatus status_;
};

}  // namespace als
