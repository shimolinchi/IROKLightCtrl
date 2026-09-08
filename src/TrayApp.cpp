#include "TrayApp.h"

#include "Logger.h"

#include <CommCtrl.h>

namespace als {
namespace {

constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT_PTR kStatusTimer = 1;
constexpr UINT kCommandPause = 1001;
constexpr UINT kCommandReconnect = 1002;
constexpr UINT kCommandSensitivityLow = 1010;
constexpr UINT kCommandSensitivityNormal = 1011;
constexpr UINT kCommandSensitivityHigh = 1012;
constexpr UINT kCommandStartup = 1020;
constexpr UINT kCommandDynamicLightingSettings = 1030;
constexpr UINT kCommandOpenLog = 1031;
constexpr UINT kCommandExit = 1099;

std::wstring SensitivityName(Sensitivity sensitivity) {
    switch (sensitivity) {
        case Sensitivity::Low:
            return L"Low";
        case Sensitivity::High:
            return L"High";
        default:
            return L"Normal";
    }
}

}  // namespace

TrayApp::TrayApp(HINSTANCE instance)
    : instance_(instance), settings_(Settings::Load()), engine_(settings_) {}

TrayApp::~TrayApp() {
    RemoveTrayIcon();
    engine_.Stop();
}

int TrayApp::Run() {
    if (!CreateHiddenWindow()) {
        return 1;
    }
    AddTrayIcon();
    engine_.Start();
    SetTimer(window_, kStatusTimer, 1000, nullptr);
    UpdateTrayIcon(true);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

bool TrayApp::CreateHiddenWindow() {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = instance_;
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = L"IROKLightCtrl.TrayWindow";
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        Logger::Instance().Error(L"Tray window registration failed: " + Win32Message());
        return false;
    }
    window_ = CreateWindowExW(WS_EX_TOOLWINDOW,
                              windowClass.lpszClassName,
                              L"IROKLightCtrl",
                              WS_OVERLAPPED,
                              0,
                              0,
                              0,
                              0,
                              nullptr,
                              nullptr,
                              instance_,
                              this);
    if (!window_) {
        Logger::Instance().Error(L"Tray window creation failed: " + Win32Message());
    }
    return window_ != nullptr;
}

LRESULT CALLBACK TrayApp::WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    TrayApp* self = reinterpret_cast<TrayApp*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<TrayApp*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->HandleMessage(window, message, wParam, lParam)
                : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT TrayApp::HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == kTrayMessage) {
        if (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_CONTEXTMENU) {
            POINT point{};
            GetCursorPos(&point);
            ShowMenu(point);
        } else if (LOWORD(lParam) == WM_LBUTTONDBLCLK) {
            engine_.TogglePaused();
            UpdateTrayIcon();
        }
        return 0;
    }
    switch (message) {
        case WM_COMMAND:
            HandleCommand(LOWORD(wParam));
            return 0;
        case WM_TIMER:
            if (wParam == kStatusTimer) {
                UpdateTrayIcon();
            }
            return 0;
        case WM_DESTROY:
            KillTimer(window, kStatusTimer);
            RemoveTrayIcon();
            engine_.Stop();
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(window, message, wParam, lParam);
    }
}

void TrayApp::AddTrayIcon() {
    icon_ = {};
    icon_.cbSize = sizeof(icon_);
    icon_.hWnd = window_;
    icon_.uID = 1;
    icon_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    icon_.uCallbackMessage = kTrayMessage;
    icon_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(icon_.szTip, L"IROKLightCtrl starting...");
    Shell_NotifyIconW(NIM_ADD, &icon_);
    icon_.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &icon_);
}

void TrayApp::RemoveTrayIcon() {
    if (icon_.hWnd) {
        Shell_NotifyIconW(NIM_DELETE, &icon_);
        icon_.hWnd = nullptr;
    }
}

void TrayApp::UpdateTrayIcon(bool showBalloon) {
    if (!icon_.hWnd) {
        return;
    }
    const auto tooltip = Tooltip();
    icon_.uFlags = NIF_TIP;
    wcsncpy_s(icon_.szTip, tooltip.c_str(), _TRUNCATE);
    if (showBalloon) {
        icon_.uFlags |= NIF_INFO;
        wcscpy_s(icon_.szInfoTitle, L"IROKLightCtrl");
        wcscpy_s(icon_.szInfo, L"Audio lighting synchronization is running in the tray.");
        icon_.dwInfoFlags = NIIF_INFO;
    }
    Shell_NotifyIconW(NIM_MODIFY, &icon_);
}

std::wstring TrayApp::Tooltip() const {
    const EngineStatus status = engine_.Status();
    if (status.paused) {
        return L"IROKLightCtrl | Paused";
    }
    return L"IROKLightCtrl | Audio " + std::wstring(status.audioReady ? L"OK" : L"wait") +
           L" | Keyboard " + (status.keyboardReady ? L"OK" : L"wait") + L" | Chassis " +
           ((status.dynamicLightingAvailable > 0 || status.auraReady) ? L"OK" : L"wait");
}

std::wstring TrayApp::StatusText() const {
    const EngineStatus status = engine_.Status();
    if (!status.running) {
        return L"Starting devices...";
    }
    std::wstring value = status.paused ? L"Paused" : L"Running";
    value += L" | Audio:" + std::wstring(status.audioReady ? L"OK" : L"-");
    value += L" Keyboard:" + std::wstring(status.keyboardReady ? L"OK" : L"-");
    value += L" Chassis:" +
             std::wstring((status.dynamicLightingAvailable > 0 || status.auraReady) ? L"OK" : L"-");
    return value;
}

void TrayApp::ShowMenu(POINT position) {
    HMENU menu = CreatePopupMenu();
    HMENU sensitivityMenu = CreatePopupMenu();
    const Sensitivity sensitivity = engine_.GetSensitivity();
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, StatusText().c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCommandPause, engine_.IsPaused() ? L"Resume" : L"Pause");
    AppendMenuW(menu, MF_STRING, kCommandReconnect, L"Reconnect devices");
    AppendMenuW(sensitivityMenu,
                MF_STRING | (sensitivity == Sensitivity::Low ? MF_CHECKED : 0),
                kCommandSensitivityLow,
                L"Low");
    AppendMenuW(sensitivityMenu,
                MF_STRING | (sensitivity == Sensitivity::Normal ? MF_CHECKED : 0),
                kCommandSensitivityNormal,
                L"Normal");
    AppendMenuW(sensitivityMenu,
                MF_STRING | (sensitivity == Sensitivity::High ? MF_CHECKED : 0),
                kCommandSensitivityHigh,
                L"High");
    AppendMenuW(menu,
                MF_POPUP | MF_STRING,
                reinterpret_cast<UINT_PTR>(sensitivityMenu),
                (L"Sensitivity: " + SensitivityName(sensitivity)).c_str());
    AppendMenuW(menu,
                MF_STRING | (IsStartupEnabled() ? MF_CHECKED : 0),
                kCommandStartup,
                L"Start with Windows");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCommandDynamicLightingSettings, L"Dynamic Lighting settings");
    AppendMenuW(menu, MF_STRING, kCommandOpenLog, L"Open log");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCommandExit, L"Exit");

    SetForegroundWindow(window_);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, position.x, position.y, 0, window_, nullptr);
    PostMessageW(window_, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

void TrayApp::HandleCommand(UINT command) {
    switch (command) {
        case kCommandPause:
            engine_.TogglePaused();
            break;
        case kCommandReconnect:
            engine_.RequestReconnect();
            break;
        case kCommandSensitivityLow:
            engine_.SetSensitivity(Sensitivity::Low);
            break;
        case kCommandSensitivityNormal:
            engine_.SetSensitivity(Sensitivity::Normal);
            break;
        case kCommandSensitivityHigh:
            engine_.SetSensitivity(Sensitivity::High);
            break;
        case kCommandStartup:
            SetStartupEnabled(!IsStartupEnabled());
            break;
        case kCommandDynamicLightingSettings:
            ShellExecuteW(window_, L"open", L"ms-settings:personalization-lighting", nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case kCommandOpenLog:
            ShellExecuteW(window_, L"open", Logger::Instance().Path().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case kCommandExit:
            DestroyWindow(window_);
            break;
        default:
            break;
    }
    UpdateTrayIcon();
}

}  // namespace als
