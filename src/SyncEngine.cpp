#include "SyncEngine.h"

#include "Logger.h"

#include <winrt/base.h>

namespace als {

SyncEngine::SyncEngine(Settings settings) : settings_(std::move(settings)) {
    sensitivity_.store(static_cast<int>(settings_.sensitivity));
}

SyncEngine::~SyncEngine() {
    Stop();
}

void SyncEngine::Start() {
    if (thread_.joinable()) {
        return;
    }
    stop_.store(false);
    thread_ = std::thread(&SyncEngine::ThreadMain, this);
}

void SyncEngine::Stop() {
    stop_.store(true);
    if (thread_.joinable()) {
        thread_.join();
    }
}

void SyncEngine::SetPaused(bool paused) {
    paused_.store(paused);
    UpdateStatus([&](EngineStatus& status) { status.paused = paused; });
}

void SyncEngine::TogglePaused() {
    SetPaused(!paused_.load());
}

void SyncEngine::SetSensitivity(Sensitivity sensitivity) {
    sensitivity_.store(static_cast<int>(sensitivity));
    settings_.sensitivity = sensitivity;
    settings_.Save();
}

void SyncEngine::RequestReconnect() {
    reconnect_.store(true);
}

EngineStatus SyncEngine::Status() const {
    std::lock_guard lock(statusMutex_);
    return status_;
}

void SyncEngine::UpdateStatus(const std::function<void(EngineStatus&)>& update) {
    std::lock_guard lock(statusMutex_);
    update(status_);
}

void SyncEngine::ThreadMain() {
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) {
        UpdateStatus([&](EngineStatus& status) {
            status.lastError = L"COM initialization failed: " + HResultMessage(comResult);
        });
        return;
    }

    AudioAnalyzer analyzer;
    AudioCapture audio;
    IrokKeyboard keyboard;
    LampArrayController dynamicLighting;
    AuraController aura;

    auto initializeAudio = [&] {
        const bool ready = audio.Initialize(analyzer);
        UpdateStatus([&](EngineStatus& status) {
            status.audioReady = ready;
            status.audioName = audio.DeviceName();
            if (!ready) {
                status.lastError = L"System audio capture is unavailable";
            }
        });
    };
    auto initializeKeyboard = [&] {
        if (!settings_.keyboardEnabled) {
            return;
        }
        const bool ready = keyboard.Open(true);
        UpdateStatus([&](EngineStatus& status) {
            status.keyboardReady = ready;
            status.keyboardName = keyboard.ProductName();
            status.keyboardFirmware = keyboard.FirmwareVersion();
            if (!ready) {
                status.lastError = keyboard.LastError();
            }
        });
    };
    auto initializeDynamicLighting = [&] {
        if (!settings_.dynamicLightingEnabled) {
            return;
        }
        dynamicLighting.Initialize();
        UpdateStatus([&](EngineStatus& status) {
            status.dynamicLightingDevices = static_cast<int>(dynamicLighting.DeviceCount());
            status.dynamicLightingAvailable = static_cast<int>(dynamicLighting.AvailableCount());
        });
    };

    initializeAudio();
    initializeKeyboard();
    initializeDynamicLighting();
    if (settings_.auraFallbackEnabled) {
        aura.Start();
    }

    UpdateStatus([&](EngineStatus& status) {
        status.running = true;
        status.paused = paused_.load();
    });
    Logger::Instance().Info(L"Synchronization engine started");

    auto nextFrame = std::chrono::steady_clock::now();
    auto nextAudioRetry = nextFrame + std::chrono::seconds(3);
    auto nextKeyboardRetry = nextFrame + std::chrono::seconds(5);
    while (!stop_.load()) {
        const auto now = std::chrono::steady_clock::now();
        if (reconnect_.exchange(false)) {
            Logger::Instance().Info(L"Manual device reconnect requested");
            audio.Close();
            keyboard.Close(true);
            dynamicLighting.Close();
            initializeAudio();
            initializeKeyboard();
            initializeDynamicLighting();
            nextAudioRetry = now + std::chrono::seconds(3);
            nextKeyboardRetry = now + std::chrono::seconds(5);
        }

        if (!audio.Pump() && now >= nextAudioRetry) {
            initializeAudio();
            nextAudioRetry = now + std::chrono::seconds(3);
        }
        if (settings_.keyboardEnabled && !keyboard.IsOpen() && now >= nextKeyboardRetry) {
            initializeKeyboard();
            nextKeyboardRetry = now + std::chrono::seconds(5);
        }

        if (now >= nextFrame) {
            nextFrame = now + std::chrono::milliseconds(settings_.frameIntervalMs);
            const Sensitivity sensitivity = static_cast<Sensitivity>(sensitivity_.load());
            const RgbColor color = analyzer.Analyze(sensitivity, settings_.maxBrightness);
            if (!paused_.load()) {
                if (settings_.keyboardEnabled && keyboard.IsOpen() && !keyboard.SetColor(color)) {
                    UpdateStatus([&](EngineStatus& status) {
                        status.keyboardReady = false;
                        status.lastError = keyboard.LastError();
                    });
                    keyboard.Close(false);
                    nextKeyboardRetry = now + std::chrono::seconds(5);
                }
                if (settings_.dynamicLightingEnabled) {
                    dynamicLighting.SetColor(color);
                }
                if (settings_.auraFallbackEnabled) {
                    aura.SubmitColor(color);
                }
            }
            UpdateStatus([&](EngineStatus& status) {
                status.paused = paused_.load();
                status.audioReady = !audio.DeviceName().empty();
                status.keyboardReady = keyboard.IsOpen();
                status.dynamicLightingDevices = static_cast<int>(dynamicLighting.DeviceCount());
                status.dynamicLightingAvailable = static_cast<int>(dynamicLighting.AvailableCount());
                status.auraReady = aura.IsReady();
                status.auraDevices = aura.DeviceCount();
                status.auraStatus = aura.Status();
                status.audioLevel = analyzer.LastLevel();
                status.color = color;
            });
        }
        Sleep(5);
    }

    keyboard.Close(true);
    dynamicLighting.Close();
    audio.Close();
    aura.Stop();
    UpdateStatus([&](EngineStatus& status) {
        status.running = false;
        status.keyboardReady = false;
        status.audioReady = false;
        status.auraReady = false;
    });
    Logger::Instance().Info(L"Synchronization engine stopped");
    if (SUCCEEDED(comResult)) {
        CoUninitialize();
    }
}

}  // namespace als
