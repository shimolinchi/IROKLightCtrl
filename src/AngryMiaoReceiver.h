#pragma once

#include "Common.h"

#include <Hidclass.h>
#include <SetupAPI.h>
#include <hidsdi.h>

#include <array>

namespace lightctrl {

class AngryMiaoReceiver final {
public:
    struct MouseSettings {
        std::array<int, 8> dpiX{};
        std::array<int, 8> dpiY{};
        std::array<RgbColor, 8> dpiColors{};
        int currentDpi{};
        int dpiStages{8};
        int reportRate{1000};
        int usbDebounce{3};
        int wirelessDebounce{1};
        int bluetoothDebounce{1};
        int liftOffDistance{1};
        bool motionSync{true};
        bool angleSnap{};
        bool rippleCorrection{};
        bool fpsMode{};
        bool dpiButton{true};
        int mouseBattery{-1};
        bool mouseOnline{};
    };

    AngryMiaoReceiver() = default;
    ~AngryMiaoReceiver();

    AngryMiaoReceiver(const AngryMiaoReceiver&) = delete;
    AngryMiaoReceiver& operator=(const AngryMiaoReceiver&) = delete;

    bool Open(bool snapshotLighting = true);
    void Close(bool restoreLighting = true);
    bool SetColor(RgbColor color);
    bool ReadMouseSettings(MouseSettings& settings);
    bool SetMouseDpi(MouseSettings& settings, int stage, int dpi);
    bool SetMouseReportRate(MouseSettings& settings, int reportRate);
    bool SetMouseUsbDebounce(MouseSettings& settings, int milliseconds);
    bool SetMouseLiftOffDistance(MouseSettings& settings, int distance);
    bool SetMouseMotionSync(MouseSettings& settings, bool enabled);
    bool SetMouseAngleSnap(MouseSettings& settings, bool enabled);
    bool SetMouseRippleCorrection(MouseSettings& settings, bool enabled);
    bool SetMouseFpsMode(MouseSettings& settings, bool enabled);
    bool SetMouseDpiButton(MouseSettings& settings, bool enabled);

    [[nodiscard]] bool IsOpen() const noexcept { return handle_ != INVALID_HANDLE_VALUE; }
    [[nodiscard]] const std::wstring& ProductName() const noexcept { return productName_; }
    [[nodiscard]] const std::wstring& LastError() const noexcept { return lastError_; }
    [[nodiscard]] std::uint16_t UsagePage() const noexcept { return usagePage_; }
    [[nodiscard]] std::uint16_t Usage() const noexcept { return usage_; }
    [[nodiscard]] std::uint16_t FeatureReportLength() const noexcept { return featureReportLength_; }

private:
    struct LightingState {
        std::uint8_t effect{};
        std::uint8_t speed{};
        std::uint8_t brightness{};
        std::uint8_t option{};
        RgbColor color{};
        std::uint8_t chargingSwitch{1};
    };

    static constexpr std::uint16_t kVendorId = 0x3151;
    static constexpr std::uint16_t kProductId = 0x5007;
    static constexpr std::size_t kPayloadLength = 64;

    bool FindAndOpenInterface();
    bool SendFeature(const std::array<std::uint8_t, kPayloadLength>& payload);
    bool QueryFeature(std::array<std::uint8_t, kPayloadLength> request,
                      std::array<std::uint8_t, kPayloadLength>& response);
    bool RemoteRead(const std::array<std::uint8_t, kPayloadLength>& command,
                    std::array<std::uint8_t, kPayloadLength>& response);
    bool RemoteWrite(const std::array<std::uint8_t, kPayloadLength>& command);
    bool WaitRemoteStatus(bool waitForRead,
                          std::array<std::uint8_t, kPayloadLength>* response = nullptr);
    bool ReadMouseInfo(std::array<std::uint8_t, kPayloadLength>& response);
    bool WriteMouseInfo(const std::array<std::uint8_t, kPayloadLength>& response);
    bool ReadLightingState();
    bool ApplyLightingState(const LightingState& state, bool updateChargingSwitch);
    bool RestoreLightingState();
    static void AddChecksum(std::array<std::uint8_t, kPayloadLength>& payload,
                            std::size_t checksumIndex);
    void SetError(const std::wstring& message);

    HANDLE handle_{INVALID_HANDLE_VALUE};
    std::wstring devicePath_;
    std::wstring productName_;
    std::wstring lastError_;
    std::uint16_t usagePage_{};
    std::uint16_t usage_{};
    std::uint16_t featureReportLength_{};
    std::optional<LightingState> savedLightingState_;
    RgbColor lastColor_{};
    bool hasLastColor_{};
};

}  // namespace lightctrl
