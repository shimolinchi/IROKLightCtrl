#include "AudioCapture.h"
#include "AngryMiaoReceiver.h"
#include "IrokKeyboard.h"
#include "LampArrayController.h"
#include "Logger.h"
#include "TrayApp.h"

#include <Shellapi.h>
#include <appmodel.h>
#include <objbase.h>

#include <fstream>
#include <sstream>

namespace {

using namespace lightctrl;

std::wstring CurrentPackageFullName() noexcept {
    UINT32 length = 0;
    if (GetCurrentPackageFullName(&length, nullptr) != ERROR_INSUFFICIENT_BUFFER || length == 0) {
        return {};
    }
    std::wstring name(length, L'\0');
    if (GetCurrentPackageFullName(&length, name.data()) != ERROR_SUCCESS) {
        return {};
    }
    name.resize(length > 0 ? length - 1 : 0);
    return name;
}

bool HasPackageIdentity() noexcept {
    return !CurrentPackageFullName().empty();
}

std::string ToUtf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int length = WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8,
                        0,
                        text.data(),
                        static_cast<int>(text.size()),
                        result.data(),
                        length,
                        nullptr,
                        nullptr);
    return result;
}

std::string JsonString(const std::wstring& value) {
    const std::string utf8 = ToUtf8(value);
    std::ostringstream output;
    output << '"';
    for (const unsigned char character : utf8) {
        switch (character) {
            case '"':
                output << "\\\"";
                break;
            case '\\':
                output << "\\\\";
                break;
            case '\b':
                output << "\\b";
                break;
            case '\f':
                output << "\\f";
                break;
            case '\n':
                output << "\\n";
                break;
            case '\r':
                output << "\\r";
                break;
            case '\t':
                output << "\\t";
                break;
            default:
                if (character < 0x20) {
                    constexpr char digits[] = "0123456789abcdef";
                    output << "\\u00" << digits[(character >> 4U) & 0x0fU]
                           << digits[character & 0x0fU];
                } else {
                    output << static_cast<char>(character);
                }
                break;
        }
    }
    output << '"';
    return output.str();
}

bool WriteUtf8(const std::filesystem::path& path, const std::string& content) {
    std::error_code error;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
    }
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(content.data(), static_cast<std::streamsize>(content.size()));
    return stream.good();
}

std::vector<std::wstring> Arguments() {
    int count = 0;
    LPWSTR* raw = CommandLineToArgvW(GetCommandLineW(), &count);
    std::vector<std::wstring> arguments;
    if (raw) {
        arguments.assign(raw, raw + count);
        LocalFree(raw);
    }
    return arguments;
}

int RunDiagnostics(const std::filesystem::path& outputPath) {
    AudioAnalyzer analyzer;
    AudioCapture audio;
    const bool audioReady = audio.Initialize(analyzer);
    const std::wstring audioName = audio.DeviceName();
    const std::uint32_t sampleRate = audio.SampleRate();
    audio.Close();

    IrokKeyboard keyboard;
    const bool keyboardReady = keyboard.Open(false);
    const std::wstring keyboardName = keyboard.ProductName();
    const std::wstring keyboardFirmware = keyboard.FirmwareVersion();
    const std::wstring keyboardError = keyboard.LastError();
    const auto keyboardUsagePage = keyboard.UsagePage();
    const auto keyboardUsage = keyboard.Usage();
    const auto keyboardInputReport = keyboard.InputReportLength();
    const auto keyboardOutputReport = keyboard.OutputReportLength();
    keyboard.Close(false);

    AngryMiaoReceiver receiver;
    const bool receiverReady = receiver.Open(false);
    const std::wstring receiverName = receiver.ProductName();
    const std::wstring receiverError = receiver.LastError();
    const auto receiverUsagePage = receiver.UsagePage();
    const auto receiverUsage = receiver.Usage();
    const auto receiverFeatureReport = receiver.FeatureReportLength();
    receiver.Close(false);

    LampArrayController lighting;
    lighting.Initialize();
    const std::wstring lightingError = lighting.LastError();

    std::ostringstream json;
    json << "{\n"
         << "  \"application\": \"LightController\",\n"
         << "  \"packageIdentity\": " << (HasPackageIdentity() ? "true" : "false") << ",\n"
         << "  \"packageFullName\": " << JsonString(CurrentPackageFullName()) << ",\n"
         << "  \"audio\": {\n"
         << "    \"ready\": " << (audioReady ? "true" : "false") << ",\n"
         << "    \"device\": " << JsonString(audioName) << ",\n"
         << "    \"sampleRate\": " << sampleRate << "\n"
         << "  },\n"
         << "  \"keyboard\": {\n"
         << "    \"ready\": " << (keyboardReady ? "true" : "false") << ",\n"
         << "    \"device\": " << JsonString(keyboardName) << ",\n"
         << "    \"firmware\": " << JsonString(keyboardFirmware) << ",\n"
         << "    \"usagePage\": " << keyboardUsagePage << ",\n"
         << "    \"usage\": " << keyboardUsage << ",\n"
         << "    \"inputReportBytes\": " << keyboardInputReport << ",\n"
         << "    \"outputReportBytes\": " << keyboardOutputReport << ",\n"
         << "    \"error\": " << JsonString(keyboardError) << "\n"
         << "  },\n"
         << "  \"dynamicLighting\": {\n"
         << "    \"chassisDevices\": " << lighting.DeviceCount() << ",\n"
         << "    \"availableDevices\": " << lighting.AvailableCount() << ",\n"
         << "    \"error\": " << JsonString(lightingError) << "\n"
         << "  },\n"
         << "  \"angryMiaoReceiver\": {\n"
         << "    \"ready\": " << (receiverReady ? "true" : "false") << ",\n"
         << "    \"device\": " << JsonString(receiverName) << ",\n"
         << "    \"usagePage\": " << receiverUsagePage << ",\n"
         << "    \"usage\": " << receiverUsage << ",\n"
         << "    \"featureReportBytes\": " << receiverFeatureReport << ",\n"
         << "    \"error\": " << JsonString(receiverError) << "\n"
         << "  },\n"
         << "  \"log\": " << JsonString(Logger::Instance().Path().wstring()) << "\n"
         << "}\n";

    lighting.Close();
    return WriteUtf8(outputPath, json.str()) ? 0 : 2;
}

int RunSelfTest(int seconds) {
    IrokKeyboard keyboard;
    LampArrayController lighting;
    const bool keyboardReady = keyboard.Open(true);
    lighting.Initialize();
    const bool lightingReady = lighting.AvailableCount() > 0;

    constexpr RgbColor colors[] = {
        {255, 0, 0}, {0, 255, 0}, {0, 96, 255}, {255, 0, 160},
    };
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    std::size_t index = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        const RgbColor color = colors[index++ % std::size(colors)];
        if (keyboardReady) {
            keyboard.SetColor(color);
        }
        lighting.SetColor(color);
        Sleep(500);
    }

    keyboard.Close(true);
    lighting.Close();
    return (keyboardReady || lightingReady) ? 0 : 3;
}

int RunReceiverSelfTest(int seconds) {
    AngryMiaoReceiver receiver;
    if (!receiver.Open(true)) {
        return 4;
    }
    constexpr RgbColor colors[] = {
        {0, 0, 0}, {255, 0, 0}, {0, 255, 0}, {0, 96, 255}, {255, 0, 160},
    };
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    std::size_t index = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        if (!receiver.SetColor(colors[index++ % std::size(colors)])) {
            receiver.Close(false);
            return 5;
        }
        Sleep(500);
    }
    receiver.Close(true);
    return 0;
}

int RunMouseSettingsTest(const std::filesystem::path& outputPath, bool withEngine = false) {
    std::optional<SyncEngine> engine;
    bool controlReady = true;
    if (withEngine) {
        engine.emplace(Settings::Load());
        engine->Start();
        Sleep(500);
        controlReady = engine->BeginReceiverControl();
        Sleep(100);
    }
    AngryMiaoReceiver receiver;
    AngryMiaoReceiver::MouseSettings settings;
    const bool opened = controlReady && receiver.Open(false);
    const bool ready = opened && receiver.ReadMouseSettings(settings);

    std::ostringstream json;
    json << "{\n"
         << "  \"ready\": " << (ready ? "true" : "false") << ",\n"
         << "  \"online\": " << (settings.mouseOnline ? "true" : "false") << ",\n"
         << "  \"battery\": " << settings.mouseBattery << ",\n"
         << "  \"currentDpiStage\": " << settings.currentDpi << ",\n"
         << "  \"dpi\": [";
    for (std::size_t index = 0; index < settings.dpiX.size(); ++index) {
        json << (index == 0 ? "" : ", ") << settings.dpiX[index];
    }
    json << "],\n"
         << "  \"reportRate\": " << settings.reportRate << ",\n"
         << "  \"usbDebounce\": " << settings.usbDebounce << ",\n"
         << "  \"liftOffDistance\": " << settings.liftOffDistance << ",\n"
         << "  \"motionSync\": " << (settings.motionSync ? "true" : "false") << ",\n"
         << "  \"angleSnap\": " << (settings.angleSnap ? "true" : "false") << ",\n"
         << "  \"rippleCorrection\": " << (settings.rippleCorrection ? "true" : "false") << ",\n"
         << "  \"fpsMode\": " << (settings.fpsMode ? "true" : "false") << ",\n"
         << "  \"dpiButton\": " << (settings.dpiButton ? "true" : "false") << ",\n"
         << "  \"error\": " << JsonString(receiver.LastError()) << "\n"
         << "}\n";
    receiver.Close(false);
    if (engine) {
        engine->EndReceiverControl();
        engine->Stop();
    }
    if (!WriteUtf8(outputPath, json.str())) {
        return 2;
    }
    return ready ? 0 : 6;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    Logger::Instance().Initialize();
    const std::wstring packageFullName = CurrentPackageFullName();
    Logger::Instance().Info(packageFullName.empty()
                                ? L"Windows ambient lighting identity unavailable"
                                : L"Windows ambient lighting identity active: " + packageFullName);
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool shouldUninitialize = SUCCEEDED(comResult);
    if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) {
        Logger::Instance().Error(L"COM initialization failed: " + HResultMessage(comResult));
        return 1;
    }

    const auto arguments = Arguments();
    int result = 0;
    if (arguments.size() >= 2 && arguments[1] == L"--diagnose") {
        const std::filesystem::path output = arguments.size() >= 3
                                                 ? std::filesystem::path(arguments[2])
                                                 : std::filesystem::current_path() /
                                                       L"LightController-diagnostic.json";
        result = RunDiagnostics(output);
    } else if (arguments.size() >= 2 && arguments[1] == L"--self-test") {
        int seconds = 6;
        if (arguments.size() >= 3) {
            try {
                seconds = std::stoi(arguments[2]);
            } catch (...) {
            }
        }
        result = RunSelfTest(std::clamp(seconds, 1, 120));
    } else if (arguments.size() >= 2 && arguments[1] == L"--receiver-self-test") {
        int seconds = 4;
        if (arguments.size() >= 3) {
            try {
                seconds = std::stoi(arguments[2]);
            } catch (...) {
            }
        }
        result = RunReceiverSelfTest(std::clamp(seconds, 1, 120));
    } else if (arguments.size() >= 2 &&
               (arguments[1] == L"--mouse-settings-test" ||
                arguments[1] == L"--engine-mouse-settings-test")) {
        const std::filesystem::path output = arguments.size() >= 3
                                                 ? std::filesystem::path(arguments[2])
                                                 : std::filesystem::current_path() /
                                                       L"LightController-mouse-settings.json";
        result = RunMouseSettingsTest(output, arguments[1] == L"--engine-mouse-settings-test");
    } else {
        const bool background = std::find(arguments.begin(), arguments.end(), L"--background") !=
                                arguments.end();
        HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\LightController.Singleton");
        if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
            if (!background) {
                if (HWND existing = FindWindowW(L"LightController.MainWindow", nullptr)) {
                    PostMessageW(existing, WM_APP + 2, 0, 0);
                }
            }
            if (mutex) {
                CloseHandle(mutex);
            }
            result = 0;
        } else {
            TrayApp app(instance);
            result = app.Run(!background);
            ReleaseMutex(mutex);
            CloseHandle(mutex);
        }
    }

    if (shouldUninitialize) {
        CoUninitialize();
    }
    return result;
}
