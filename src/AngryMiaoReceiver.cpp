#include "AngryMiaoReceiver.h"

#include "Logger.h"

namespace lightctrl {
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

bool AngryMiaoReceiver::WaitRemoteStatus(
    bool waitForRead, std::array<std::uint8_t, kPayloadLength>* response) {
    for (int attempt = 0; attempt < 12; ++attempt) {
        std::array<std::uint8_t, kPayloadLength> request{};
        request[0] = 0xf7;
        std::array<std::uint8_t, kPayloadLength> status{};
        if (!QueryFeature(request, status)) {
            return false;
        }
        const bool ready = waitForRead ? status[0] == 1 : status[5] == 1;
        if (ready) {
            if (response) {
                *response = status;
            }
            return true;
        }
        Sleep(40);
    }
    SetError(waitForRead ? L"Mouse response timed out" : L"Mouse receiver stayed busy");
    return false;
}

bool AngryMiaoReceiver::RemoteRead(
    const std::array<std::uint8_t, kPayloadLength>& command,
    std::array<std::uint8_t, kPayloadLength>& response) {
    std::array<std::uint8_t, kPayloadLength> request{};
    request[0] = 0xf6;
    request[1] = 5;
    if (!SendFeature(request) || !WaitRemoteStatus(false)) {
        return false;
    }

    request = {};
    request[0] = 0xfe;
    request[1] = static_cast<std::uint8_t>(kPayloadLength);
    std::array<std::uint8_t, kPayloadLength> ignored{};
    if (!QueryFeature(request, ignored) || !QueryFeature(command, ignored) ||
        !WaitRemoteStatus(true)) {
        return false;
    }

    request = {};
    request[0] = 0xfc;
    return QueryFeature(request, response);
}

bool AngryMiaoReceiver::RemoteWrite(
    const std::array<std::uint8_t, kPayloadLength>& command) {
    std::array<std::uint8_t, kPayloadLength> request{};
    request[0] = 0xf6;
    request[1] = 5;
    if (!SendFeature(request) || !WaitRemoteStatus(false)) {
        return false;
    }

    request = {};
    request[0] = 0xfe;
    request[1] = static_cast<std::uint8_t>(kPayloadLength);
    std::array<std::uint8_t, kPayloadLength> ignored{};
    if (!QueryFeature(request, ignored) || !SendFeature(command)) {
        return false;
    }
    return WaitRemoteStatus(false);
}

bool AngryMiaoReceiver::ReadMouseInfo(
    std::array<std::uint8_t, kPayloadLength>& response) {
    std::array<std::uint8_t, kPayloadLength> request{};
    request[0] = 0xd3;
    AddChecksum(request, 7);
    return RemoteRead(request, response) && response[0] == 0xd3;
}

bool AngryMiaoReceiver::WriteMouseInfo(
    const std::array<std::uint8_t, kPayloadLength>& response) {
    auto request = response;
    std::fill_n(request.begin(), 8, static_cast<std::uint8_t>(0));
    request[0] = 0x53;
    AddChecksum(request, 7);
    return RemoteWrite(request);
}

bool AngryMiaoReceiver::ReadMouseSettings(MouseSettings& settings) {
    if (!IsOpen()) {
        return false;
    }

    std::array<std::uint8_t, kPayloadLength> dpiRequest{};
    dpiRequest[0] = 0xd4;
    AddChecksum(dpiRequest, 7);
    std::array<std::uint8_t, kPayloadLength> dpi{};
    if (!RemoteRead(dpiRequest, dpi) || dpi[0] != 0xd4) {
        SetError(L"Could not read AM mouse DPI settings");
        return false;
    }

    std::array<std::uint8_t, kPayloadLength> info{};
    if (!ReadMouseInfo(info)) {
        SetError(L"Could not read AM mouse sensor settings");
        return false;
    }

    settings.currentDpi = std::clamp(static_cast<int>(dpi[2]), 0, 7);
    settings.dpiStages = std::clamp(static_cast<int>(dpi[3]), 1, 8);
    for (int index = 0; index < 8; ++index) {
        const int xOffset = 8 + index * 2;
        const int yOffset = 24 + index * 2;
        settings.dpiX[index] = dpi[xOffset] | (dpi[xOffset + 1] << 8);
        settings.dpiY[index] = dpi[yOffset] | (dpi[yOffset + 1] << 8);
        const int colorOffset = 40 + index * 3;
        settings.dpiColors[index] = {dpi[colorOffset], dpi[colorOffset + 1],
                                     dpi[colorOffset + 2]};
    }

    switch (info[9]) {
        case 8:
            settings.reportRate = 125;
            break;
        case 4:
            settings.reportRate = 250;
            break;
        case 2:
            settings.reportRate = 500;
            break;
        default:
            settings.reportRate = 1000;
            break;
    }
    settings.usbDebounce = info[10];
    settings.rippleCorrection = info[12] != 0;
    settings.liftOffDistance = info[52];
    settings.angleSnap = info[53] != 0;
    settings.wirelessDebounce = info[61];
    settings.bluetoothDebounce = info[62];
    settings.motionSync = info[63] == 0;

    std::array<std::uint8_t, kPayloadLength> request{};
    request[0] = 0xe2;
    AddChecksum(request, 7);
    std::array<std::uint8_t, kPayloadLength> mode{};
    if (RemoteRead(request, mode)) {
        settings.fpsMode = mode[1] != 0;
    }
    request = {};
    request[0] = 0xe3;
    AddChecksum(request, 7);
    if (RemoteRead(request, mode)) {
        settings.dpiButton = mode[1] != 0;
    }

    request = {};
    request[0] = 0xf7;
    if (QueryFeature(request, mode)) {
        settings.mouseOnline = mode[4] == 0;
        settings.mouseBattery = settings.mouseOnline ? std::clamp(static_cast<int>(mode[2]), 0, 100)
                                                     : -1;
    }
    Logger::Instance().Info(L"AM mouse settings read through the 2.4 GHz receiver");
    return true;
}

bool AngryMiaoReceiver::SetMouseDpi(MouseSettings& settings, int stage, int dpiValue) {
    stage = std::clamp(stage, 0, 7);
    dpiValue = std::clamp(dpiValue, 100, 26000);
    MouseSettings updated = settings;
    updated.currentDpi = stage;
    updated.dpiX[stage] = dpiValue;
    updated.dpiY[stage] = dpiValue;

    std::array<std::uint8_t, kPayloadLength> request{};
    request[0] = 0x54;
    request[2] = static_cast<std::uint8_t>(stage);
    request[3] = static_cast<std::uint8_t>(updated.dpiStages);
    AddChecksum(request, 7);
    for (int index = 0; index < 8; ++index) {
        const auto x = static_cast<unsigned int>(updated.dpiX[index]);
        const auto y = static_cast<unsigned int>(updated.dpiY[index]);
        request[8 + index * 2] = static_cast<std::uint8_t>(x & 0xffU);
        request[9 + index * 2] = static_cast<std::uint8_t>((x >> 8U) & 0xffU);
        request[24 + index * 2] = static_cast<std::uint8_t>(y & 0xffU);
        request[25 + index * 2] = static_cast<std::uint8_t>((y >> 8U) & 0xffU);
        request[40 + index * 3] = updated.dpiColors[index].r;
        request[41 + index * 3] = updated.dpiColors[index].g;
        request[42 + index * 3] = updated.dpiColors[index].b;
    }
    if (!RemoteWrite(request)) {
        return false;
    }
    settings = updated;
    return true;
}

bool AngryMiaoReceiver::SetMouseReportRate(MouseSettings& settings, int reportRate) {
    std::array<std::uint8_t, kPayloadLength> info{};
    if (!ReadMouseInfo(info)) {
        return false;
    }
    const int normalized = reportRate <= 125 ? 125 : reportRate <= 250 ? 250
                                               : reportRate <= 500   ? 500
                                                                   : 1000;
    info[9] = normalized == 125 ? 8 : normalized == 250 ? 4 : normalized == 500 ? 2 : 1;
    if (!WriteMouseInfo(info)) {
        return false;
    }
    settings.reportRate = normalized;
    return true;
}

bool AngryMiaoReceiver::SetMouseUsbDebounce(MouseSettings& settings, int milliseconds) {
    std::array<std::uint8_t, kPayloadLength> info{};
    if (!ReadMouseInfo(info)) {
        return false;
    }
    info[10] = static_cast<std::uint8_t>(std::clamp(milliseconds, 0, 20));
    if (!WriteMouseInfo(info)) {
        return false;
    }
    settings.usbDebounce = info[10];
    return true;
}

bool AngryMiaoReceiver::SetMouseLiftOffDistance(MouseSettings& settings, int distance) {
    std::array<std::uint8_t, kPayloadLength> info{};
    if (!ReadMouseInfo(info)) {
        return false;
    }
    info[52] = static_cast<std::uint8_t>(std::clamp(distance, 1, 2));
    if (!WriteMouseInfo(info)) {
        return false;
    }
    settings.liftOffDistance = info[52];
    return true;
}

bool AngryMiaoReceiver::SetMouseMotionSync(MouseSettings& settings, bool enabled) {
    std::array<std::uint8_t, kPayloadLength> info{};
    if (!ReadMouseInfo(info)) {
        return false;
    }
    info[63] = enabled ? 0 : 1;
    if (!WriteMouseInfo(info)) {
        return false;
    }
    settings.motionSync = enabled;
    return true;
}

bool AngryMiaoReceiver::SetMouseAngleSnap(MouseSettings& settings, bool enabled) {
    std::array<std::uint8_t, kPayloadLength> info{};
    if (!ReadMouseInfo(info)) {
        return false;
    }
    info[53] = enabled ? 1 : 0;
    if (!WriteMouseInfo(info)) {
        return false;
    }
    settings.angleSnap = enabled;
    return true;
}

bool AngryMiaoReceiver::SetMouseRippleCorrection(MouseSettings& settings, bool enabled) {
    std::array<std::uint8_t, kPayloadLength> info{};
    if (!ReadMouseInfo(info)) {
        return false;
    }
    info[12] = enabled ? 1 : 0;
    if (!WriteMouseInfo(info)) {
        return false;
    }
    settings.rippleCorrection = enabled;
    return true;
}

bool AngryMiaoReceiver::SetMouseFpsMode(MouseSettings& settings, bool enabled) {
    std::array<std::uint8_t, kPayloadLength> request{};
    request[0] = 0x62;
    request[1] = enabled ? 1 : 0;
    AddChecksum(request, 7);
    if (!RemoteWrite(request)) {
        return false;
    }
    settings.fpsMode = enabled;
    return true;
}

bool AngryMiaoReceiver::SetMouseDpiButton(MouseSettings& settings, bool enabled) {
    std::array<std::uint8_t, kPayloadLength> request{};
    request[0] = 0x63;
    request[1] = enabled ? 1 : 0;
    AddChecksum(request, 7);
    if (!RemoteWrite(request)) {
        return false;
    }
    settings.dpiButton = enabled;
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
    state.brightness = color == RgbColor{} ? 0 : 4;
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

}  // namespace lightctrl
