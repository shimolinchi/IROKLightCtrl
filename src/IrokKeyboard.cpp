#include "IrokKeyboard.h"

#include "Logger.h"

#include <cwchar>
#include <memory>
#include <set>

namespace lightctrl {
namespace {

constexpr std::array<std::uint8_t, 81> kMg75ProKeys{
    41, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 76,
    53, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 45, 46, 42, 73,
    43, 20, 26, 8, 21, 23, 28, 24, 12, 18, 19, 47, 48, 49, 75,
    57, 4, 22, 7, 9, 10, 11, 13, 14, 15, 51, 52, 40, 78,
    225, 29, 27, 6, 25, 5, 17, 16, 54, 55, 56, 229, 82,
    224, 227, 226, 44, 230, 1, 80, 81, 79,
};

std::wstring ReadHidString(HANDLE handle,
                           BOOLEAN(__stdcall* getter)(HANDLE, PVOID, ULONG)) {
    std::array<wchar_t, 256> text{};
    if (getter(handle, text.data(), static_cast<ULONG>(text.size() * sizeof(wchar_t)))) {
        return text.data();
    }
    return {};
}

}  // namespace

IrokKeyboard::~IrokKeyboard() {
    Close(true);
}

bool IrokKeyboard::Open(bool enterCustomMode) {
    Close(false);
    lastError_.clear();
    if (!FindAndOpenInterface()) {
        return false;
    }
    QueryVersion();
    modernStreaming_ = firmwareVersion_.empty() || FirmwareAtLeast(1, 0, 8);
    if (enterCustomMode) {
        ReadLightingState();
        if (!EnterCustomMode()) {
            Close(false);
            return false;
        }
    }
    Logger::Instance().Info(L"IROK keyboard connected: " + productName_ +
                            (firmwareVersion_.empty() ? L"" : L", firmware " + firmwareVersion_));
    return true;
}

void IrokKeyboard::Close(bool restoreLighting) {
    if (handle_ != INVALID_HANDLE_VALUE) {
        if (restoreLighting && customModeEntered_) {
            RestoreLightingState();
        }
        CancelIoEx(handle_, nullptr);
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
    customModeEntered_ = false;
    hasLastColor_ = false;
}

bool IrokKeyboard::FindAndOpenInterface() {
    GUID hidGuid{};
    HidD_GetHidGuid(&hidGuid);
    HDEVINFO deviceInfo = SetupDiGetClassDevsW(
        &hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
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
        SetupDiGetDeviceInterfaceDetailW(
            deviceInfo, &interfaceData, nullptr, 0, &required, nullptr);
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

        HANDLE candidate = CreateFileW(detail->DevicePath,
                                       GENERIC_READ | GENERIC_WRITE,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                                       nullptr,
                                       OPEN_EXISTING,
                                       FILE_FLAG_OVERLAPPED,
                                       nullptr);
        if (candidate == INVALID_HANDLE_VALUE) {
            continue;
        }

        HIDD_ATTRIBUTES attributes{};
        attributes.Size = sizeof(attributes);
        if (!HidD_GetAttributes(candidate, &attributes) ||
            attributes.VendorID != kVendorId || attributes.ProductID != kProductId) {
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
        const std::wstring lowerPath = [&] {
            std::wstring value = detail->DevicePath;
            std::transform(value.begin(), value.end(), value.begin(), ::towlower);
            return value;
        }();
        const bool vendorInterface = lowerPath.find(L"mi_02") != std::wstring::npos ||
                                     (haveCaps && caps.UsagePage >= 0xff00);
        const bool usableReports = haveCaps && caps.OutputReportByteLength >= 65;
        if (!vendorInterface || !usableReports) {
            CloseHandle(candidate);
            continue;
        }

        handle_ = candidate;
        devicePath_ = detail->DevicePath;
        usagePage_ = caps.UsagePage;
        usage_ = caps.Usage;
        inputReportLength_ = caps.InputReportByteLength;
        outputReportLength_ = caps.OutputReportByteLength;
        productName_ = ReadHidString(handle_, HidD_GetProductString);
        if (productName_.empty()) {
            productName_ = L"IROK MG75 PRO";
        }
        HidD_SetNumInputBuffers(handle_, 64);
        opened = true;
        break;
    }

    SetupDiDestroyDeviceInfoList(deviceInfo);
    if (!opened) {
        SetError(L"IROK MG75 PRO vendor HID interface was not found or is busy");
    }
    return opened;
}

bool IrokKeyboard::WritePayload(const std::array<std::uint8_t, kPayloadLength>& payload) {
    if (!IsOpen()) {
        return false;
    }
    const std::size_t reportSize = std::max<std::size_t>(65, outputReportLength_);
    std::vector<std::uint8_t> report(reportSize, 0);
    std::copy(payload.begin(), payload.end(), report.begin() + 1);

    OVERLAPPED overlapped{};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!overlapped.hEvent) {
        SetError(L"Could not create HID write event");
        return false;
    }
    DWORD written = 0;
    BOOL result = WriteFile(handle_,
                            report.data(),
                            static_cast<DWORD>(report.size()),
                            &written,
                            &overlapped);
    if (!result && GetLastError() == ERROR_IO_PENDING) {
        const DWORD wait = WaitForSingleObject(overlapped.hEvent, 500);
        if (wait == WAIT_OBJECT_0) {
            result = GetOverlappedResult(handle_, &overlapped, &written, FALSE);
        } else {
            CancelIoEx(handle_, &overlapped);
            result = FALSE;
            SetLastError(ERROR_TIMEOUT);
        }
    }
    const DWORD error = result ? ERROR_SUCCESS : GetLastError();
    CloseHandle(overlapped.hEvent);
    if (!result || written != report.size()) {
        // Some HID stacks expose output reports only through the control endpoint.
        if (!HidD_SetOutputReport(handle_, report.data(), static_cast<ULONG>(report.size()))) {
            SetError(L"Keyboard HID write failed: " + Win32Message(error));
            return false;
        }
    }
    return true;
}

bool IrokKeyboard::ReadPayload(std::array<std::uint8_t, kPayloadLength>& payload, DWORD timeoutMs) {
    const std::size_t reportSize = std::max<std::size_t>(65, inputReportLength_);
    std::vector<std::uint8_t> report(reportSize, 0);
    OVERLAPPED overlapped{};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!overlapped.hEvent) {
        return false;
    }
    DWORD read = 0;
    BOOL result = ReadFile(
        handle_, report.data(), static_cast<DWORD>(report.size()), &read, &overlapped);
    if (!result && GetLastError() == ERROR_IO_PENDING) {
        const DWORD wait = WaitForSingleObject(overlapped.hEvent, timeoutMs);
        if (wait == WAIT_OBJECT_0) {
            result = GetOverlappedResult(handle_, &overlapped, &read, FALSE);
        } else {
            CancelIoEx(handle_, &overlapped);
            result = FALSE;
        }
    }
    CloseHandle(overlapped.hEvent);
    if (!result || read < 64) {
        return false;
    }
    const std::size_t offset = read >= 65 ? 1 : 0;
    if (read < offset + payload.size()) {
        return false;
    }
    std::copy_n(report.begin() + static_cast<std::ptrdiff_t>(offset), payload.size(), payload.begin());
    return true;
}

bool IrokKeyboard::SendCommand(const std::array<std::uint8_t, kPayloadLength>& payload,
                               bool expectResponse,
                               std::array<std::uint8_t, kPayloadLength>* response) {
    if (expectResponse) {
        HidD_FlushQueue(handle_);
    }
    if (!WritePayload(payload)) {
        return false;
    }
    if (!expectResponse) {
        return true;
    }
    std::array<std::uint8_t, kPayloadLength> received{};
    if (!ReadPayload(received, 500)) {
        SetError(L"Keyboard HID response timed out");
        return false;
    }
    if (received[4] != 0) {
        SetError(L"Keyboard rejected HID command with status " + std::to_wstring(received[4]));
        return false;
    }
    if (response) {
        *response = received;
    }
    return true;
}

bool IrokKeyboard::QueryVersion() {
    std::array<std::uint8_t, kPayloadLength> request{};
    request[0] = 92;
    request[1] = 2;
    request[2] = 1;
    request[3] = 147;
    request[4] = 255;
    request[5] = 255;
    std::array<std::uint8_t, kPayloadLength> response{};
    if (!SendCommand(request, true, &response)) {
        Logger::Instance().Error(L"Could not read IROK firmware version: " + lastError_);
        lastError_.clear();
        return false;
    }
    std::string ascii;
    for (std::size_t index = 30; index < 44 && response[index] != 0; ++index) {
        if (response[index] >= 32 && response[index] < 127) {
            ascii.push_back(static_cast<char>(response[index]));
        }
    }
    const std::size_t v = ascii.find('V');
    if (v != std::string::npos) {
        ascii.erase(0, v + 1);
    }
    const int length = MultiByteToWideChar(
        CP_UTF8, 0, ascii.data(), static_cast<int>(ascii.size()), nullptr, 0);
    firmwareVersion_.resize(static_cast<std::size_t>(length));
    MultiByteToWideChar(CP_UTF8,
                        0,
                        ascii.data(),
                        static_cast<int>(ascii.size()),
                        firmwareVersion_.data(),
                        length);
    return !firmwareVersion_.empty();
}

bool IrokKeyboard::ReadLightingState() {
    std::array<std::uint8_t, kPayloadLength> request{};
    request[0] = 92;
    request[1] = 44;
    request[2] = 24;
    request[3] = 213;
    std::array<std::uint8_t, kPayloadLength> response{};
    if (!SendCommand(request, true, &response)) {
        Logger::Instance().Error(L"Could not snapshot IROK lighting state: " + lastError_);
        lastError_.clear();
        return false;
    }
    savedLightingState_ = response;
    return true;
}

bool IrokKeyboard::EnterCustomMode() {
    std::array<std::uint8_t, kPayloadLength> request{};
    request[0] = 92;
    request[1] = 44;
    request[2] = 24;
    request[3] = 212;
    request[4] = 1;
    request[41] = savedLightingState_ ? ((*savedLightingState_)[41] | 1U) : 1U;
    request[42] = 5;
    request[43] = FirmwareAtLeast(1, 0, 4) || firmwareVersion_.empty() ? 21 : 14;
    request[44] = 3;
    request[45] = 0;
    request[46] = 255;
    request[47] = 255;
    if (!SendCommand(request, true)) {
        SetError(L"Could not enable IROK custom lighting mode: " + lastError_);
        return false;
    }
    customModeEntered_ = true;
    return true;
}

bool IrokKeyboard::RestoreLightingState() {
    if (!savedLightingState_) {
        return false;
    }
    auto request = *savedLightingState_;
    request[0] = 92;
    request[1] = 44;
    request[2] = 24;
    request[3] = 212;
    request[4] = 1;
    request[46] = 255;
    request[47] = 255;
    const bool restored = SendCommand(request, true);
    if (restored) {
        Logger::Instance().Info(L"IROK lighting state restored");
    } else {
        Logger::Instance().Error(L"Could not restore IROK lighting state: " + lastError_);
    }
    customModeEntered_ = false;
    return restored;
}

bool IrokKeyboard::SetColor(RgbColor color) {
    if (!IsOpen() || !customModeEntered_) {
        return false;
    }
    if (hasLastColor_ && color == lastColor_) {
        return true;
    }

    constexpr std::size_t keysPerPacket = 14;
    for (std::size_t offset = 0; offset < kMg75ProKeys.size(); offset += keysPerPacket) {
        const std::size_t count = std::min(keysPerPacket, kMg75ProKeys.size() - offset);
        std::vector<std::uint8_t> logical{92, 0, 42, 0, 1};
        logical.reserve(5 + count * 4);
        for (std::size_t index = 0; index < count; ++index) {
            logical.push_back(kMg75ProKeys[offset + index]);
            logical.push_back(color.r);
            logical.push_back(color.g);
            logical.push_back(color.b);
        }
        logical[1] = static_cast<std::uint8_t>(logical.size() - 4);
        logical[3] = static_cast<std::uint8_t>(
            (53 + logical[0] + logical[1] + logical[2] + logical.back()) & 0xff);

        std::array<std::uint8_t, kPayloadLength> packet{};
        std::copy(logical.begin(), logical.end(), packet.begin());
        if (!SendCommand(packet, !modernStreaming_)) {
            return false;
        }
    }
    lastColor_ = color;
    hasLastColor_ = true;
    return true;
}

bool IrokKeyboard::FirmwareAtLeast(int major, int minor, int patch) const {
    int actualMajor = 0;
    int actualMinor = 0;
    int actualPatch = 0;
    if (::swscanf_s(
            firmwareVersion_.c_str(), L"%d.%d.%d", &actualMajor, &actualMinor, &actualPatch) < 2) {
        return false;
    }
    if (actualMajor != major) {
        return actualMajor > major;
    }
    if (actualMinor != minor) {
        return actualMinor > minor;
    }
    return actualPatch >= patch;
}

void IrokKeyboard::SetError(const std::wstring& message) {
    lastError_ = message;
    Logger::Instance().Error(message);
}

}  // namespace lightctrl
