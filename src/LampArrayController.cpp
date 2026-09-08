#include "LampArrayController.h"

#include "Logger.h"

namespace als {

bool LampArrayController::Initialize() {
    Close();
    try {
        using winrt::Windows::Devices::Enumeration::DeviceInformation;
        using winrt::Windows::Devices::Lights::LampArray;
        using winrt::Windows::Devices::Lights::LampArrayKind;

        const auto selector = LampArray::GetDeviceSelector();
        const auto found = DeviceInformation::FindAllAsync(selector).get();
        for (const auto& device : found) {
            auto array = LampArray::FromIdAsync(device.Id()).get();
            if (!array || array.LampArrayKind() != LampArrayKind::Chassis) {
                continue;
            }
            LampArrayDeviceInfo info;
            info.name = device.Name().c_str();
            info.id = device.Id().c_str();
            info.kind = static_cast<int>(array.LampArrayKind());
            info.lampCount = array.LampCount();
            info.available = array.IsAvailable();
            devices_.push_back(std::move(info));
            arrays_.push_back(std::move(array));
        }
        Logger::Instance().Info(L"Windows Dynamic Lighting chassis devices: " +
                                std::to_wstring(arrays_.size()));
        return !arrays_.empty();
    } catch (const winrt::hresult_error& error) {
        lastError_ = error.message().c_str();
        Logger::Instance().Error(L"Dynamic Lighting initialization failed: " + lastError_);
        Close();
        return false;
    }
}

bool LampArrayController::SetColor(RgbColor color) {
    if (hasLastColor_ && color == lastColor_) {
        return AvailableCount() > 0;
    }
    bool applied = false;
    try {
        winrt::Windows::UI::Color value{};
        value.A = 255;
        value.R = color.r;
        value.G = color.g;
        value.B = color.b;
        for (std::size_t index = 0; index < arrays_.size(); ++index) {
            const bool available = arrays_[index].IsAvailable();
            devices_[index].available = available;
            if (!available) {
                continue;
            }
            arrays_[index].BrightnessLevel(1.0);
            arrays_[index].SetColor(value);
            applied = true;
        }
        if (applied) {
            lastColor_ = color;
            hasLastColor_ = true;
        }
        return applied;
    } catch (const winrt::hresult_error& error) {
        lastError_ = error.message().c_str();
        Logger::Instance().Error(L"Dynamic Lighting update failed: " + lastError_);
        return false;
    }
}

std::size_t LampArrayController::AvailableCount() const {
    std::size_t count = 0;
    for (const auto& array : arrays_) {
        try {
            if (array.IsAvailable()) {
                ++count;
            }
        } catch (...) {
        }
    }
    return count;
}

void LampArrayController::Close() {
    arrays_.clear();
    devices_.clear();
    hasLastColor_ = false;
}

}  // namespace als
