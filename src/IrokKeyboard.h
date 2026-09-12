#pragma once

#include "Common.h"

#include <Hidclass.h>
#include <SetupAPI.h>
#include <hidsdi.h>

#include <array>

namespace lightctrl {

class IrokKeyboard final {
public:
    IrokKeyboard() = default;
    ~IrokKeyboard();

    IrokKeyboard(const IrokKeyboard&) = delete;
    IrokKeyboard& operator=(const IrokKeyboard&) = delete;

    bool Open(bool enterCustomMode = true);
    void Close(bool restoreLighting = true);
    bool SetColor(RgbColor color);

    [[nodiscard]] bool IsOpen() const noexcept { return handle_ != INVALID_HANDLE_VALUE; }
    [[nodiscard]] const std::wstring& ProductName() const noexcept { return productName_; }
    [[nodiscard]] const std::wstring& FirmwareVersion() const noexcept { return firmwareVersion_; }
    [[nodiscard]] const std::wstring& LastError() const noexcept { return lastError_; }
    [[nodiscard]] std::uint16_t UsagePage() const noexcept { return usagePage_; }
    [[nodiscard]] std::uint16_t Usage() const noexcept { return usage_; }
    [[nodiscard]] std::uint16_t InputReportLength() const noexcept { return inputReportLength_; }
    [[nodiscard]] std::uint16_t OutputReportLength() const noexcept { return outputReportLength_; }

private:
    static constexpr std::uint16_t kVendorId = 0x1ca5;
    static constexpr std::uint16_t kProductId = 0x0807;
    static constexpr std::size_t kPayloadLength = 64;

    bool FindAndOpenInterface();
    bool WritePayload(const std::array<std::uint8_t, kPayloadLength>& payload);
    bool ReadPayload(std::array<std::uint8_t, kPayloadLength>& payload, DWORD timeoutMs);
    bool SendCommand(const std::array<std::uint8_t, kPayloadLength>& payload,
                     bool expectResponse,
                     std::array<std::uint8_t, kPayloadLength>* response = nullptr);
    bool QueryVersion();
    bool ReadLightingState();
    bool EnterCustomMode();
    bool RestoreLightingState();
    bool FirmwareAtLeast(int major, int minor, int patch) const;
    void SetError(const std::wstring& message);

    HANDLE handle_{INVALID_HANDLE_VALUE};
    std::wstring devicePath_;
    std::wstring productName_;
    std::wstring firmwareVersion_;
    std::wstring lastError_;
    std::uint16_t usagePage_{};
    std::uint16_t usage_{};
    std::uint16_t inputReportLength_{};
    std::uint16_t outputReportLength_{};
    std::optional<std::array<std::uint8_t, kPayloadLength>> savedLightingState_;
    bool customModeEntered_{};
    bool modernStreaming_{};
    RgbColor lastColor_{};
    bool hasLastColor_{};
};

}  // namespace lightctrl
