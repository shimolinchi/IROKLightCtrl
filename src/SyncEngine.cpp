#include "SyncEngine.h"

#include "Logger.h"

#include <winrt/base.h>

namespace lightctrl {
namespace {

RgbColor Blend(RgbColor from, RgbColor to, float amount) {
    const auto channel = [amount](std::uint8_t first, std::uint8_t second) {
        return static_cast<std::uint8_t>(std::clamp(
            std::lround(first + (static_cast<float>(second) - first) * amount), 0L, 255L));
    };
    return {channel(from.r, to.r), channel(from.g, to.g), channel(from.b, to.b)};
}

RgbColor Scale(RgbColor color, float amount) {
    amount = std::clamp(amount, 0.0F, 1.0F);
    return {
        static_cast<std::uint8_t>(std::lround(color.r * amount)),
        static_cast<std::uint8_t>(std::lround(color.g * amount)),
        static_cast<std::uint8_t>(std::lround(color.b * amount)),
    };
}

RgbColor QuantizeReceiverColor(RgbColor color) {
    const auto channel = [](std::uint8_t value) {
        constexpr int step = 24;
        return static_cast<std::uint8_t>(
            std::min(255, ((static_cast<int>(value) + step / 2) / step) * step));
    };
    return {channel(color.r), channel(color.g), channel(color.b)};
}

float EffectPhase(std::chrono::steady_clock::time_point startedAt, int speed, bool reverse) {
    const float normalizedSpeed = static_cast<float>(std::clamp(speed, 1, 100) - 1) / 99.0F;
    const float secondsPerCycle = 12.0F - normalizedSpeed * 10.5F;
    const float elapsed = std::chrono::duration<float>(
                              std::chrono::steady_clock::now() - startedAt)
                              .count();
    float phase = std::fmod(elapsed / secondsPerCycle, 1.0F);
    if (reverse) {
        phase = std::fmod(phase + 0.5F, 1.0F);
    }
    return phase;
}

}  // namespace

SyncEngine::SyncEngine(Settings settings) : settings_(std::move(settings)) {
    sensitivity_.store(static_cast<int>(settings_.sensitivity));
    lightingMode_.store(static_cast<int>(settings_.lightingMode));
    audioColorMode_.store(static_cast<int>(settings_.audioColorMode));
    primaryColor_.store(settings_.primaryColor.Packed());
    secondaryColor_.store(settings_.secondaryColor.Packed());
    maxBrightness_.store(settings_.maxBrightness);
    effectSpeed_.store(settings_.effectSpeed);
    reverseDirection_.store(settings_.reverseDirection);
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

void SyncEngine::SetLightingMode(LightingMode mode) {
    lightingMode_.store(static_cast<int>(mode));
    settings_.lightingMode = mode;
    settings_.Save();
}

void SyncEngine::SetAudioColorMode(AudioColorMode mode) {
    audioColorMode_.store(static_cast<int>(mode));
    settings_.audioColorMode = mode;
    settings_.Save();
}

void SyncEngine::SetPrimaryColor(RgbColor color) {
    primaryColor_.store(color.Packed());
    settings_.primaryColor = color;
    settings_.Save();
}

void SyncEngine::SetSecondaryColor(RgbColor color) {
    secondaryColor_.store(color.Packed());
    settings_.secondaryColor = color;
    settings_.Save();
}

void SyncEngine::SetMaxBrightness(int brightness) {
    brightness = std::clamp(brightness, 10, 100);
    maxBrightness_.store(brightness);
    settings_.maxBrightness = brightness;
    settings_.Save();
}

void SyncEngine::SetEffectSpeed(int speed) {
    speed = std::clamp(speed, 1, 100);
    effectSpeed_.store(speed);
    settings_.effectSpeed = speed;
    settings_.Save();
}

void SyncEngine::SetReverseDirection(bool reverse) {
    reverseDirection_.store(reverse);
    settings_.reverseDirection = reverse;
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
    AngryMiaoReceiver receiver;
    LampArrayController dynamicLighting;

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
    auto initializeReceiver = [&] {
        if (!settings_.angryMiaoReceiverEnabled) {
            return;
        }
        const bool ready = receiver.Open(true);
        UpdateStatus([&](EngineStatus& status) {
            status.angryMiaoReceiverReady = ready;
            status.angryMiaoReceiverName = receiver.ProductName();
            if (!ready) {
                status.lastError = receiver.LastError();
            }
        });
    };
    auto initializeDynamicLighting = [&] {
        dynamicLighting.Initialize();
        UpdateStatus([&](EngineStatus& status) {
            status.dynamicLightingDevices = static_cast<int>(dynamicLighting.DeviceCount());
            status.dynamicLightingAvailable = static_cast<int>(dynamicLighting.AvailableCount());
        });
    };

    initializeAudio();
    initializeKeyboard();
    initializeReceiver();
    initializeDynamicLighting();

    UpdateStatus([&](EngineStatus& status) {
        status.running = true;
        status.paused = paused_.load();
    });
    Logger::Instance().Info(L"Synchronization engine started");

    const auto effectStartedAt = std::chrono::steady_clock::now();
    auto nextFrame = std::chrono::steady_clock::now();
    auto nextAudioRetry = nextFrame + std::chrono::seconds(3);
    auto nextKeyboardRetry = nextFrame + std::chrono::seconds(5);
    auto nextReceiverRetry = nextFrame + std::chrono::seconds(5);
    auto nextDynamicLightingRetry = nextFrame + std::chrono::seconds(5);
    auto nextReceiverFrame = nextFrame;
    std::size_t lastDynamicLightingAvailable = dynamicLighting.AvailableCount();
    while (!stop_.load()) {
        const auto now = std::chrono::steady_clock::now();
        if (reconnect_.exchange(false)) {
            Logger::Instance().Info(L"Manual device reconnect requested");
            audio.Close();
            keyboard.Close(true);
            receiver.Close(true);
            dynamicLighting.Close();
            initializeAudio();
            initializeKeyboard();
            initializeReceiver();
            initializeDynamicLighting();
            nextAudioRetry = now + std::chrono::seconds(3);
            nextKeyboardRetry = now + std::chrono::seconds(5);
            nextReceiverRetry = now + std::chrono::seconds(5);
            nextDynamicLightingRetry = now + std::chrono::seconds(5);
            lastDynamicLightingAvailable = dynamicLighting.AvailableCount();
        }

        if (!audio.Pump() && now >= nextAudioRetry) {
            initializeAudio();
            nextAudioRetry = now + std::chrono::seconds(3);
        }
        if (settings_.keyboardEnabled && !keyboard.IsOpen() && now >= nextKeyboardRetry) {
            initializeKeyboard();
            nextKeyboardRetry = now + std::chrono::seconds(5);
        }
        if (settings_.angryMiaoReceiverEnabled && !receiver.IsOpen() &&
            now >= nextReceiverRetry) {
            initializeReceiver();
            nextReceiverRetry = now + std::chrono::seconds(5);
        }
        if (dynamicLighting.DeviceCount() == 0 && now >= nextDynamicLightingRetry) {
            initializeDynamicLighting();
            nextDynamicLightingRetry = now + std::chrono::seconds(5);
        }

        if (now >= nextFrame) {
            nextFrame = now + std::chrono::milliseconds(settings_.frameIntervalMs);
            const Sensitivity sensitivity = static_cast<Sensitivity>(sensitivity_.load());
            const int brightness = maxBrightness_.load();
            const RgbColor spectrum = analyzer.Analyze(sensitivity, brightness);
            const float phase = EffectPhase(
                effectStartedAt, effectSpeed_.load(), reverseDirection_.load());
            const float paletteAmount = 0.5F - 0.5F * std::cos(phase * 6.28318530718F);
            const RgbColor palette = Blend(RgbColor::FromPacked(primaryColor_.load()),
                                           RgbColor::FromPacked(secondaryColor_.load()),
                                           paletteAmount);

            RgbColor color = spectrum;
            switch (static_cast<LightingMode>(lightingMode_.load())) {
                case LightingMode::Static:
                    color = Scale(RgbColor::FromPacked(primaryColor_.load()), brightness / 100.0F);
                    break;
                case LightingMode::Breathing: {
                    const float pulse = 0.08F + 0.92F * paletteAmount;
                    color = Scale(RgbColor::FromPacked(primaryColor_.load()),
                                  pulse * brightness / 100.0F);
                    break;
                }
                case LightingMode::ColorCycle:
                    color = Scale(palette, brightness / 100.0F);
                    break;
                case LightingMode::Audio:
                default:
                    constexpr float idleLevel = 0.06F;
                    if (static_cast<AudioColorMode>(audioColorMode_.load()) ==
                        AudioColorMode::GradientCycle) {
                        color = Scale(palette,
                                      std::max(idleLevel, analyzer.LastLevel()) *
                                          brightness / 100.0F);
                    } else if (analyzer.LastLevel() < idleLevel) {
                        color = Scale(RgbColor::FromPacked(primaryColor_.load()),
                                      idleLevel * brightness / 100.0F);
                    }
                    break;
            }
            if (!paused_.load()) {
                if (settings_.keyboardEnabled && keyboard.IsOpen() && !keyboard.SetColor(color)) {
                    UpdateStatus([&](EngineStatus& status) {
                        status.keyboardReady = false;
                        status.lastError = keyboard.LastError();
                    });
                    keyboard.Close(false);
                    nextKeyboardRetry = now + std::chrono::seconds(5);
                }
                dynamicLighting.SetColor(color);
                if (settings_.angryMiaoReceiverEnabled && receiver.IsOpen() &&
                    now >= nextReceiverFrame) {
                    nextReceiverFrame = now + std::chrono::milliseconds(400);
                    if (!receiver.SetColor(QuantizeReceiverColor(color))) {
                        UpdateStatus([&](EngineStatus& status) {
                            status.angryMiaoReceiverReady = false;
                            status.lastError = receiver.LastError();
                        });
                        receiver.Close(false);
                        nextReceiverRetry = now + std::chrono::seconds(5);
                    }
                }
            }
            UpdateStatus([&](EngineStatus& status) {
                status.paused = paused_.load();
                status.audioReady = !audio.DeviceName().empty();
                status.keyboardReady = keyboard.IsOpen();
                status.angryMiaoReceiverReady = receiver.IsOpen();
                status.dynamicLightingDevices = static_cast<int>(dynamicLighting.DeviceCount());
                status.dynamicLightingAvailable = static_cast<int>(dynamicLighting.AvailableCount());
                status.audioLevel = analyzer.LastLevel();
                status.color = color;
            });
            const std::size_t available = dynamicLighting.AvailableCount();
            if (available != lastDynamicLightingAvailable) {
                Logger::Instance().Info(L"Windows Dynamic Lighting available chassis devices: " +
                                        std::to_wstring(available));
                lastDynamicLightingAvailable = available;
            }
        }
        Sleep(5);
    }

    keyboard.Close(true);
    receiver.Close(true);
    dynamicLighting.Close();
    audio.Close();
    UpdateStatus([&](EngineStatus& status) {
        status.running = false;
        status.keyboardReady = false;
        status.angryMiaoReceiverReady = false;
        status.audioReady = false;
    });
    Logger::Instance().Info(L"Synchronization engine stopped");
    if (SUCCEEDED(comResult)) {
        CoUninitialize();
    }
}

}  // namespace lightctrl
