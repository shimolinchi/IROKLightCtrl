#pragma once

#include "Common.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.Lights.h>
#include <winrt/Windows.UI.h>
#include <winrt/base.h>

namespace als {

struct LampArrayDeviceInfo {
    std::wstring name;
    std::wstring id;
    int kind{};
    int lampCount{};
    bool available{};
};

class LampArrayController final {
public:
    bool Initialize();
    bool SetColor(RgbColor color);
    void Close();

    [[nodiscard]] std::size_t DeviceCount() const noexcept { return arrays_.size(); }
    [[nodiscard]] std::size_t AvailableCount() const;
    [[nodiscard]] const std::vector<LampArrayDeviceInfo>& Devices() const noexcept { return devices_; }
    [[nodiscard]] const std::wstring& LastError() const noexcept { return lastError_; }

private:
    std::vector<winrt::Windows::Devices::Lights::LampArray> arrays_;
    std::vector<LampArrayDeviceInfo> devices_;
    std::wstring lastError_;
    RgbColor lastColor_{};
    bool hasLastColor_{};
};

}  // namespace als
