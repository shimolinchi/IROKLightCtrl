#include "AngryMiaoReceiver.h"

#include "Logger.h"

namespace als {
namespace {

std::wstring ReadProductName(HANDLE handle) {
    wchar_t buffer[256]{};
    return HidD_GetProductString(handle, buffer, sizeof(buffer)) ? buffer : L"";
}

}  // namespace

AngryMiaoReceiver::~AngryMiaoReceiver() { Close(true); }

bool AngryMiaoReceiver::Open(bool snapshotLighting) {
    Close(false);
    lastError_.clear();
    if (!FindAndOpenInterface()) {
        return false;
    }
    if (snapshotLighting && !ReadLightingState()) {
        Close(false);
        return false;
    }
    Logger::Instance().Info(L"Angry Miao receiver connected: " + productName_);
    return true;
}

void AngryMiaoReceiver::Close(bool restoreLighting) {
    if (handle_ != INVALID_HANDLE_VALUE) {
        if (restoreLighting && savedLightingState_) {
            RestoreLightingState();
        }
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
    savedLightingState_.reset();
    hasLastColor_ = false;
}

bool AngryMiaoReceiver::FindAndOpenInterface() {
    GUID hidGuid{};
    HidD_GetHidGuid(&hidGuid);
    HDEVINFO deviceInfo = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr,
                                               DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (deviceInfo == INVALID_HANDLE_VALUE) {
        SetError(L"HID enumeration failed: " + Win32Message());
        return false;
    }

    bool opened = false;
    for (DWORD index = 0;; ++index) {
        SP_DEVICE_INTERFACE_DATA interfaceData{};
        interfaceData.cbSize = sizeof(interfaceData);
        if (!SetupDiEnumDeviceInterfaces(deviceInfo, nullptr, &hidGuid, index, &interfaceData)) {
            break;
        }
        DWORD required = 0;
        SetupDiGetDeviceInterfaceDetailW(deviceInfo, &interfaceData, nullptr, 0, &required, nullptr);
        if (required < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) {
            continue;
        }
        std::vector<BYTE> storage(required);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(
                deviceInfo, &interfaceData, detail, required, nullptr, nullptr)) {
            continue;
        }

        HANDLE candidate = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                       0, nullptr);
        if (candidate == INVALID_HANDLE_VALUE) {
            continue;
        }
        HIDD_ATTRIBUTES attributes{};
        attributes.Size = sizeof(attributes);
        if (!HidD_GetAttributes(candidate, &attributes) || attributes.VendorID != kVendorId ||
            attributes.ProductID != kProductId) {
            CloseHandle(candidate);
            continue;
        }

        PHIDP_PREPARSED_DATA preparsed = nullptr;
        HIDP_CAPS caps{};
        const bool haveCaps = HidD_GetPreparsedData(candidate, &preparsed) &&
                              HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS;
        if (preparsed) {
            HidD_FreePreparsedData(preparsed);
        }
        if (!haveCaps || caps.UsagePage != 0xffff || caps.Usage != 2 ||
            caps.FeatureReportByteLength < 65) {
            CloseHandle(candidate);
            continue;
        }

        handle_ = candidate;
        devicePath_ = detail->DevicePath;
        usagePage_ = caps.UsagePage;
        usage_ = caps.Usage;
        featureReportLength_ = caps.FeatureReportByteLength;
        productName_ = ReadProductName(handle_);
        if (productName_.empty()) {
            productName_ = L"AM INFINITY 8K MOUSE";
        }
        opened = true;
        break;
    }
    SetupDiDestroyDeviceInfoList(deviceInfo);
    if (!opened) {
        SetError(L"AM INFINITY 8K receiver lighting interface was not found or is busy");
    }
    return opened;
}

void AngryMiaoReceiver::AddChecksum(
    std::array<std::uint8_t, kPayloadLength>& payload, std::size_t checksumIndex) {
    unsigned int sum = 0;
    for (std::size_t index = 0; index < checksumIndex; ++index) {
        sum += payload[index];
    }
    payload[checksumIndex] = static_cast<std::uint8_t>(255U - (sum & 0xffU));
}

bool AngryMiaoReceiver::SendFeature(
    const std::array<std::uint8_t, kPayloadLength>& payload) {
    if (!IsOpen()) {
        return false;
    }
    std::vector<std::uint8_t> report(featureReportLength_, 0);
    std::copy(payload.begin(), payload.end(), report.begin() + 1);
    if (!HidD_SetFeature(handle_, report.data(), static_cast<ULONG>(report.size()))) {
        SetError(L"Angry Miao receiver HID write failed: " + Win32Message());
        return false;
    }
    return true;
}

bool AngryMiaoReceiver::QueryFeature(
    std::array<std::uint8_t, kPayloadLength> request,
    std::array<std::uint8_t, kPayloadLength>& response) {
    if (!SendFeature(request)) {
        return false;
    }
    Sleep(10);
    std::vector<std::uint8_t> report(featureReportLength_, 0);
    if (!HidD_GetFeature(handle_, report.data(), static_cast<ULONG>(report.size()))) {
        SetError(L"Angry Miao receiver HID read failed: " + Win32Message());
        return false;
    }
    std::copy_n(report.begin() + 1, response.size(), response.begin());
    return true;
}

bool AngryMiaoReceiver::ReadLightingState() {
    std::array<std::uint8_t, kPayloadLength> request{};
    request[0] = 0x88;
    AddChecksum(request, 7);
    std::array<std::uint8_t, kPayloadLength> lighting{};
    if (!QueryFeature(request, lighting)) {
        return false;
    }

    request = {};
    request[0] = 0x87;
    AddChecksum(request, 7);
    std::array<std::uint8_t, kPayloadLength> charging{};
    if (!QueryFeature(request, charging)) {
        return false;
    }
    savedLightingState_ = LightingState{lighting[1], lighting[2], lighting[3], lighting[4],
                                        {lighting[5], lighting[6], lighting[7]}, charging[1]};
    return true;
}

bool AngryMiaoReceiver::ApplyLightingState(const LightingState& state,
                                           bool updateChargingSwitch) {
    std::array<std::uint8_t, kPayloadLength> request{};
    request[0] = 0x08;
    request[1] = state.effect;
    request[2] = state.speed;
    request[3] = state.brightness;
    request[4] = state.option;
    request[5] = state.color.r;
    request[6] = state.color.g;
    request[7] = state.color.b;
    AddChecksum(request, 8);
    if (!SendFeature(request)) {
        return false;
    }
    if (updateChargingSwitch) {
        Sleep(10);
        request = {};
        request[0] = 0x07;
        request[1] = state.chargingSwitch;
        AddChecksum(request, 7);
        if (!SendFeature(request)) {
            return false;
        }
    }
    return true;
}

bool AngryMiaoReceiver::SetColor(RgbColor color) {
    if (!IsOpen()) {
        return false;
    }
    if (hasLastColor_ && color == lastColor_) {
        return true;
    }
    LightingState state{};
    state.effect = 1;
    state.brightness = 4;
    state.option = 7;
    state.color = color;
    state.chargingSwitch = savedLightingState_ ? savedLightingState_->chargingSwitch : 1;
    if (!ApplyLightingState(state, !hasLastColor_)) {
        return false;
    }
    lastColor_ = color;
    hasLastColor_ = true;
    return true;
}

bool AngryMiaoReceiver::RestoreLightingState() {
    if (!savedLightingState_) {
        return false;
    }
    const bool restored = ApplyLightingState(*savedLightingState_, true);
    if (restored) {
        Logger::Instance().Info(L"Angry Miao receiver lighting state restored");
    }
    return restored;
}

void AngryMiaoReceiver::SetError(const std::wstring& message) {
    lastError_ = message;
    Logger::Instance().Error(message);
}

}  // namespace als
