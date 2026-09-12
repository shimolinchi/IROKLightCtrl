#include "Settings.h"

#include "Logger.h"

namespace lightctrl {
namespace {

int ReadInt(const std::filesystem::path& path, const wchar_t* name, int fallback) {
    return static_cast<int>(GetPrivateProfileIntW(L"LightController", name, fallback, path.c_str()));
}

void WriteInt(const std::filesystem::path& path, const wchar_t* name, int value) {
    WritePrivateProfileStringW(
        L"LightController", name, std::to_wstring(value).c_str(), path.c_str());
}

std::wstring ExecutablePath() {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    path.resize(length);
    return path;
}

ThemeMode ReadThemeMode(const std::filesystem::path& path) {
    DWORD value = static_cast<DWORD>(std::clamp(ReadInt(path, L"ThemeMode", 0), 0, 1));
    DWORD bytes = sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER,
                 L"Software\\LightController",
                 L"ThemeMode",
                 RRF_RT_REG_DWORD,
                 nullptr,
                 &value,
                 &bytes);
    return static_cast<ThemeMode>(std::clamp<DWORD>(value, 0, 1));
}

void WriteThemeMode(ThemeMode theme) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
                        L"Software\\LightController",
                        0,
                        nullptr,
                        0,
                        KEY_SET_VALUE,
                        nullptr,
                        &key,
                        nullptr) != ERROR_SUCCESS) {
        return;
    }
    const DWORD value = static_cast<DWORD>(theme);
    RegSetValueExW(key,
                   L"ThemeMode",
                   0,
                   REG_DWORD,
                   reinterpret_cast<const BYTE*>(&value),
                   sizeof(value));
    RegCloseKey(key);
}

}  // namespace

std::filesystem::path Settings::FilePath() {
    return UserLocalDataPath() / L"LightController" / L"settings.ini";
}

Settings Settings::Load() {
    Settings value;
    const auto path = FilePath();
    std::filesystem::create_directories(path.parent_path());
    value.sensitivity = static_cast<Sensitivity>(
        std::clamp(ReadInt(path, L"Sensitivity", 1), 0, 2));
    value.lightingMode = static_cast<LightingMode>(
        std::clamp(ReadInt(path, L"LightingMode", 0), 0, 3));
    value.audioColorMode = static_cast<AudioColorMode>(
        std::clamp(ReadInt(path, L"AudioColorMode", 1), 0, 1));
    value.primaryColor = RgbColor::FromPacked(
        static_cast<std::uint32_t>(ReadInt(path, L"PrimaryColor", 0xff3070)));
    value.secondaryColor = RgbColor::FromPacked(
        static_cast<std::uint32_t>(ReadInt(path, L"SecondaryColor", 0x18b0ff)));
    value.effectSpeed = std::clamp(ReadInt(path, L"EffectSpeed", 55), 1, 100);
    value.reverseDirection = ReadInt(path, L"ReverseDirection", 0) != 0;
    value.frameIntervalMs = std::clamp(ReadInt(path, L"FrameIntervalMs", 50), 35, 100);
    value.maxBrightness = std::clamp(ReadInt(path, L"MaxBrightness", 100), 10, 100);
    value.keyboardEnabled = ReadInt(path, L"KeyboardEnabled", 1) != 0;
    value.angryMiaoReceiverEnabled = ReadInt(path, L"AngryMiaoReceiverEnabled", 1) != 0;
    value.themeMode = ReadThemeMode(path);
    if (!std::filesystem::exists(path)) {
        value.Save();
    }
    return value;
}

void Settings::Save() const {
    const auto path = FilePath();
    std::filesystem::create_directories(path.parent_path());
    WriteInt(path, L"Sensitivity", static_cast<int>(sensitivity));
    WriteInt(path, L"LightingMode", static_cast<int>(lightingMode));
    WriteInt(path, L"AudioColorMode", static_cast<int>(audioColorMode));
    WriteInt(path, L"PrimaryColor", static_cast<int>(primaryColor.Packed()));
    WriteInt(path, L"SecondaryColor", static_cast<int>(secondaryColor.Packed()));
    WriteInt(path, L"EffectSpeed", effectSpeed);
    WriteInt(path, L"ReverseDirection", reverseDirection ? 1 : 0);
    WriteInt(path, L"FrameIntervalMs", frameIntervalMs);
    WriteInt(path, L"MaxBrightness", maxBrightness);
    WriteInt(path, L"KeyboardEnabled", keyboardEnabled ? 1 : 0);
    WriteInt(path, L"AngryMiaoReceiverEnabled", angryMiaoReceiverEnabled ? 1 : 0);
    WriteInt(path, L"ThemeMode", static_cast<int>(themeMode));
    WriteThemeMode(themeMode);
}

bool IsStartupEnabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0,
                      KEY_QUERY_VALUE,
                      &key) != ERROR_SUCCESS) {
        return false;
    }
    wchar_t value[32768]{};
    DWORD bytes = sizeof(value);
    DWORD type = 0;
    const LSTATUS status = RegQueryValueExW(
        key, L"LightController", nullptr, &type, reinterpret_cast<BYTE*>(value), &bytes);
    RegCloseKey(key);
    return status == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ);
}

bool SetStartupEnabled(bool enabled) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
                        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                        0,
                        nullptr,
                        0,
                        KEY_SET_VALUE,
                        nullptr,
                        &key,
                        nullptr) != ERROR_SUCCESS) {
        return false;
    }
    LSTATUS status = ERROR_SUCCESS;
    if (enabled) {
        const std::wstring command = L"\"" + ExecutablePath() + L"\" --background";
        status = RegSetValueExW(key,
                                L"LightController",
                                0,
                                REG_SZ,
                                reinterpret_cast<const BYTE*>(command.c_str()),
                                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        status = RegDeleteValueW(key, L"LightController");
        if (status == ERROR_FILE_NOT_FOUND) {
            status = ERROR_SUCCESS;
        }
    }
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
        Logger::Instance().Error(L"Could not update startup setting: " + Win32Message(status));
    }
    return status == ERROR_SUCCESS;
}

}  // namespace lightctrl
