#include "TrayApp.h"

#include "Logger.h"

#include <Dwmapi.h>
#include <commdlg.h>
#include <windowsx.h>

#include <iomanip>
#include <sstream>
#include <utility>

namespace lightctrl {
namespace {

constexpr wchar_t kWindowClass[] = L"LightController.MainWindow";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kShowMessage = WM_APP + 2;
constexpr UINT_PTR kAnimationTimer = 1;

constexpr UINT kCommandOpen = 1000;
constexpr UINT kCommandPause = 1001;
constexpr UINT kCommandReconnect = 1002;
constexpr UINT kCommandStartup = 1003;
constexpr UINT kCommandDynamicLightingSettings = 1004;
constexpr UINT kCommandOpenLog = 1005;
constexpr UINT kCommandExit = 1099;

enum TargetId : int {
    kNavLighting = 1,
    kNavAudio,
    kNavDevices,

    kModeAudio = 10,
    kModeStatic,
    kModeBreathing,
    kModeCycle,
    kPrimaryColor,
    kSecondaryColor,
    kBrightnessSlider,
    kSpeedSlider,
    kDirectionForward,
    kDirectionReverse,

    kAudioSpectrum = 30,
    kAudioGradient,
    kSensitivityLow,
    kSensitivityNormal,
    kSensitivityHigh,
    kEnableAudio,

    kReconnect = 40,
    kOpenDynamicLighting,
    kOpenLog,
    kThemeDark,
    kThemeLight,
    kStartupToggle,
    kPause,
    kOpenKeyboardDriver,
    kOpenMouseDriver,
    kExit,
};

enum UiIcon : int {
    kIconLighting,
    kIconAudio,
    kIconDevices,
    kIconKeyboard,
    kIconMouse,
    kIconChassis,
};

constexpr COLORREF kBrand = RGB(249, 115, 22);
constexpr COLORREF kBrandDark = RGB(234, 88, 12);
constexpr COLORREF kReady = RGB(34, 178, 112);
constexpr COLORREF kWaiting = RGB(242, 151, 54);

bool Contains(const RECT& bounds, POINT point) {
    return point.x >= bounds.left && point.x < bounds.right && point.y >= bounds.top &&
           point.y < bounds.bottom;
}

RgbColor BlendColor(RgbColor from, RgbColor to, float amount) {
    amount = std::clamp(amount, 0.0F, 1.0F);
    const auto channel = [amount](std::uint8_t first, std::uint8_t second) {
        return static_cast<std::uint8_t>(std::clamp(
            std::lround(first + (static_cast<float>(second) - first) * amount), 0L, 255L));
    };
    return {channel(from.r, to.r), channel(from.g, to.g), channel(from.b, to.b)};
}

COLORREF ToColorRef(RgbColor color) {
    return RGB(color.r, color.g, color.b);
}

RgbColor FromColorRef(COLORREF color) {
    return {GetRValue(color), GetGValue(color), GetBValue(color)};
}

std::wstring HexColor(RgbColor color) {
    std::wostringstream value;
    value << L'#' << std::uppercase << std::hex << std::setfill(L'0') << std::setw(2)
          << static_cast<int>(color.r) << std::setw(2) << static_cast<int>(color.g)
          << std::setw(2) << static_cast<int>(color.b);
    return value.str();
}

std::wstring LightingModeName(LightingMode mode) {
    switch (mode) {
        case LightingMode::Static:
            return L"常亮";
        case LightingMode::Breathing:
            return L"呼吸";
        case LightingMode::ColorCycle:
            return L"色域循环";
        case LightingMode::Audio:
        default:
            return L"音乐律动";
    }
}

void FillGradient(HDC context, const RECT& bounds, COLORREF first, COLORREF second) {
    TRIVERTEX vertices[2]{};
    vertices[0].x = bounds.left;
    vertices[0].y = bounds.top;
    vertices[0].Red = static_cast<COLOR16>(GetRValue(first) << 8U);
    vertices[0].Green = static_cast<COLOR16>(GetGValue(first) << 8U);
    vertices[0].Blue = static_cast<COLOR16>(GetBValue(first) << 8U);
    vertices[0].Alpha = 0xff00;
    vertices[1].x = bounds.right;
    vertices[1].y = bounds.bottom;
    vertices[1].Red = static_cast<COLOR16>(GetRValue(second) << 8U);
    vertices[1].Green = static_cast<COLOR16>(GetGValue(second) << 8U);
    vertices[1].Blue = static_cast<COLOR16>(GetBValue(second) << 8U);
    vertices[1].Alpha = 0xff00;
    GRADIENT_RECT gradient{0, 1};
    GradientFill(context, vertices, 2, &gradient, 1, GRADIENT_FILL_RECT_H);
}

}  // namespace

TrayApp::TrayApp(HINSTANCE instance)
    : instance_(instance), settings_(Settings::Load()), engine_(settings_) {
    RefreshPalette();
}

TrayApp::~TrayApp() {
    RemoveTrayIcon();
    engine_.Stop();
    DestroyBackBuffer();
    DestroyFonts();
}

int TrayApp::Run(bool showWindow) {
    if (!CreateMainWindow()) {
        return 1;
    }

    AddTrayIcon();
    engine_.Start();
    SetTimer(window_, kAnimationTimer, 33, nullptr);
    UpdateTrayIcon(!showWindow);
    if (showWindow) {
        ShowMainWindow();
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

bool TrayApp::CreateMainWindow() {
    dpi_ = GetDpiForSystem();

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = instance_;
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hIconSm = windowClass.hIcon;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        Logger::Instance().Error(L"Window registration failed: " + Win32Message());
        return false;
    }

    RECT frame{0, 0, Scale(1180), Scale(700)};
    AdjustWindowRectExForDpi(&frame, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi_);
    const int width = frame.right - frame.left;
    const int height = frame.bottom - frame.top;
    const int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    const int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
    window_ = CreateWindowExW(0,
                              kWindowClass,
                              L"LightController - IROK MG75 PRO",
                              WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                              x,
                              y,
                              width,
                              height,
                              nullptr,
                              nullptr,
                              instance_,
                              this);
    if (!window_) {
        Logger::Instance().Error(L"Main window creation failed: " + Win32Message());
        return false;
    }

    dpi_ = GetDpiForWindow(window_);
    CreateFonts();

    ApplyWindowTheme();
    return true;
}

void TrayApp::RefreshPalette() {
    if (settings_.themeMode == ThemeMode::Light) {
        palette_ = {
            RGB(244, 244, 245), RGB(228, 228, 231), RGB(250, 250, 250),
            RGB(250, 250, 250), RGB(255, 255, 255), RGB(255, 255, 255),
            RGB(244, 244, 245), RGB(255, 247, 237), RGB(228, 228, 231),
            RGB(24, 24, 27), RGB(113, 113, 122), RGB(161, 161, 170),
            RGB(228, 228, 231), RGB(255, 247, 237), RGB(194, 65, 12),
            RGB(255, 247, 237), RGB(154, 52, 18), RGB(255, 255, 255),
            RGB(63, 63, 70), RGB(82, 82, 91), RGB(212, 212, 216),
            RGB(228, 228, 231), RGB(212, 212, 216), RGB(161, 161, 170),
            RGB(255, 255, 255), RGB(249, 115, 22), RGB(255, 255, 255),
            RGB(234, 88, 12)};
        return;
    }

    palette_ = {
        RGB(39, 39, 42), RGB(24, 24, 27), RGB(32, 32, 35),
        RGB(24, 24, 27), RGB(32, 32, 35), RGB(39, 39, 42),
        RGB(48, 48, 52), RGB(67, 45, 35), RGB(63, 63, 70),
        RGB(250, 250, 250), RGB(161, 161, 170), RGB(113, 113, 122),
        RGB(63, 63, 70), RGB(67, 45, 35), RGB(253, 186, 116),
        RGB(60, 41, 32), RGB(253, 186, 116), RGB(48, 48, 52),
        RGB(212, 212, 216), RGB(161, 161, 170), RGB(82, 82, 91),
        RGB(63, 63, 70), RGB(82, 82, 91), RGB(113, 113, 122),
        RGB(250, 250, 250), RGB(24, 24, 27), RGB(250, 250, 250),
        RGB(63, 63, 70)};
}

void TrayApp::ApplyWindowTheme() {
    if (!window_) {
        return;
    }
    const BOOL dark = settings_.themeMode == ThemeMode::Dark;
    DwmSetWindowAttribute(window_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    DwmSetWindowAttribute(
        window_, DWMWA_CAPTION_COLOR, &palette_.caption, sizeof(palette_.caption));
    DwmSetWindowAttribute(
        window_, DWMWA_TEXT_COLOR, &palette_.captionText, sizeof(palette_.captionText));
    DwmSetWindowAttribute(
        window_, DWMWA_BORDER_COLOR, &palette_.captionBorder, sizeof(palette_.captionBorder));
}

void TrayApp::CreateFonts() {
    DestroyFonts();
    const auto makeFont = [&](int pointSize, int weight, const wchar_t* family) {
        return CreateFontW(-MulDiv(pointSize, static_cast<int>(dpi_), 72),
                           0,
                           0,
                           0,
                           weight,
                           FALSE,
                           FALSE,
                           FALSE,
                           DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE,
                           family);
    };
    brandFont_ = makeFont(23, FW_BOLD, L"Segoe UI");
    titleFont_ = makeFont(18, FW_SEMIBOLD, L"Microsoft YaHei UI");
    headingFont_ = makeFont(11, FW_SEMIBOLD, L"Microsoft YaHei UI");
    bodyFont_ = makeFont(9, FW_NORMAL, L"Microsoft YaHei UI");
    smallFont_ = makeFont(8, FW_NORMAL, L"Microsoft YaHei UI");
    keyFont_ = makeFont(7, FW_NORMAL, L"Segoe UI");
}

void TrayApp::DestroyFonts() {
    for (HFONT* font : {&brandFont_, &titleFont_, &headingFont_, &bodyFont_, &smallFont_, &keyFont_}) {
        if (*font) {
            DeleteObject(*font);
            *font = nullptr;
        }
    }
}

void TrayApp::ShowMainWindow() {
    ShowWindow(window_, SW_RESTORE);
    SetForegroundWindow(window_);
    InvalidateRect(window_, nullptr, FALSE);
}

LRESULT CALLBACK TrayApp::WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    TrayApp* self = reinterpret_cast<TrayApp*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        self = static_cast<TrayApp*>(create->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->HandleMessage(window, message, wParam, lParam)
                : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT TrayApp::HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_NCCREATE:
            return TRUE;
        case WM_PAINT:
            PaintWindow();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED) {
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        case WM_GETMINMAXINFO: {
            auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
            limits->ptMinTrackSize.x = Scale(1040);
            limits->ptMinTrackSize.y = Scale(665);
            return 0;
        }
        case WM_DPICHANGED: {
            dpi_ = HIWORD(wParam);
            const auto* suggested = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(window,
                         nullptr,
                         suggested->left,
                         suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            CreateFonts();
            DestroyBackBuffer();
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        case WM_TIMER:
            if (wParam == kAnimationTimer) {
                ++timerTicks_;
                const bool repaint = selectedPage_ == 1 ||
                                     (selectedPage_ == 0 && timerTicks_ % 2 == 0) ||
                                     (selectedPage_ == 2 && timerTicks_ % 15 == 0);
                if (repaint) {
                    InvalidateRect(window, nullptr, FALSE);
                }
                if (timerTicks_ % 30 == 0) {
                    UpdateTrayIcon();
                }
            }
            return 0;
        case WM_MOUSEMOVE: {
            const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (activeSlider_ != 0) {
                UpdateSlider(activeSlider_, point.x);
            }
            const int hovered = HitTest(point);
            if (hoveredTarget_ != hovered) {
                hoveredTarget_ = hovered;
                InvalidateRect(window, nullptr, FALSE);
            }
            StartMouseTracking();
            return 0;
        }
        case WM_MOUSELEAVE:
            trackingMouse_ = false;
            hoveredTarget_ = 0;
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        case WM_LBUTTONDOWN: {
            SetFocus(window);
            const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            const int target = HitTest(point);
            if (target == kBrightnessSlider || target == kSpeedSlider) {
                activeSlider_ = target;
                SetCapture(window);
                UpdateSlider(target, point.x);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (activeSlider_ != 0) {
                UpdateSlider(activeSlider_, point.x);
                activeSlider_ = 0;
                ReleaseCapture();
            } else {
                HandleClick(HitTest(point));
            }
            return 0;
        }
        case WM_CAPTURECHANGED:
            activeSlider_ = 0;
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT && hoveredTarget_ != 0) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            break;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                ShowWindow(window, SW_HIDE);
                return 0;
            }
            break;
        case WM_COMMAND:
            HandleTrayCommand(LOWORD(wParam));
            return 0;
        case kShowMessage:
            ShowMainWindow();
            return 0;
        case kTrayMessage: {
            const UINT event = LOWORD(lParam);
            if (event == WM_CONTEXTMENU || event == WM_RBUTTONUP) {
                POINT point{GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam)};
                if (point.x == -1 && point.y == -1) {
                    GetCursorPos(&point);
                }
                ShowMenu(point);
            } else if (event == NIN_SELECT || event == NIN_KEYSELECT ||
                       event == WM_LBUTTONDBLCLK) {
                ShowMainWindow();
            }
            return 0;
        }
        case WM_CLOSE:
            if (exiting_) {
                DestroyWindow(window);
            } else {
                ShowWindow(window, SW_HIDE);
            }
            return 0;
        case WM_DESTROY:
            KillTimer(window, kAnimationTimer);
            RemoveTrayIcon();
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void TrayApp::PaintWindow() {
    PAINTSTRUCT paint{};
    HDC target = BeginPaint(window_, &paint);
    RECT client{};
    GetClientRect(window_, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width > 0 && height > 0) {
        EnsureBackBuffer(target, width, height);
        DrawInterface(backBufferDc_, client);
        BitBlt(target, 0, 0, width, height, backBufferDc_, 0, 0, SRCCOPY);
    }
    EndPaint(window_, &paint);
}

void TrayApp::EnsureBackBuffer(HDC target, int width, int height) {
    if (backBufferDc_ && backBufferSize_.cx == width && backBufferSize_.cy == height) {
        return;
    }
    DestroyBackBuffer();
    backBufferDc_ = CreateCompatibleDC(target);
    backBufferBitmap_ = CreateCompatibleBitmap(target, width, height);
    backBufferOldBitmap_ = SelectObject(backBufferDc_, backBufferBitmap_);
    backBufferSize_ = {width, height};
}

void TrayApp::DestroyBackBuffer() {
    if (backBufferDc_) {
        if (backBufferOldBitmap_) {
            SelectObject(backBufferDc_, backBufferOldBitmap_);
        }
        if (backBufferBitmap_) {
            DeleteObject(backBufferBitmap_);
        }
        DeleteDC(backBufferDc_);
    }
    backBufferDc_ = nullptr;
    backBufferBitmap_ = nullptr;
    backBufferOldBitmap_ = nullptr;
    backBufferSize_ = {};
}

void TrayApp::DrawInterface(HDC context, const RECT& client) {
    hitTargets_.clear();
    SetBkMode(context, TRANSPARENT);
    FillGradient(context, client, palette_.backgroundTop, palette_.backgroundBottom);

    DrawHeader(context, client);

    const int margin = Scale(18);
    const int contentTop = Scale(78);
    const int contentBottom = client.bottom - margin;
    const int navigationWidth = Scale(108);
    const int panelWidth =
        std::clamp(static_cast<int>(client.right) * 29 / 100, Scale(310), Scale(350));
    const int gutter = Scale(12);

    const RECT navigation{margin,
                          contentTop,
                          margin + navigationWidth,
                          contentBottom};
    const RECT panel{client.right - margin - panelWidth,
                     contentTop,
                     client.right - margin,
                     contentBottom};
    const RECT workspace{navigation.right + gutter,
                         contentTop,
                         panel.left - gutter,
                         contentBottom};

    DrawNavigation(context, navigation);
    if (selectedPage_ == 0) {
        DrawLightingPage(context, workspace, panel);
    } else if (selectedPage_ == 1) {
        DrawAudioPage(context, workspace, panel);
    } else {
        DrawDevicesPage(context, workspace, panel);
    }
}

void TrayApp::DrawHeader(HDC context, const RECT& client) {
    DrawLabel(context,
              L"LightController",
              {Scale(25), Scale(10), Scale(245), Scale(54)},
              brandFont_,
              settings_.themeMode == ThemeMode::Dark ? palette_.captionText : kBrandDark);
    DrawLabel(context,
              L"MG75 PRO 与 Windows 灯光控制中心",
              {Scale(265), Scale(13), Scale(610), Scale(52)},
              bodyFont_,
              settings_.themeMode == ThemeMode::Dark ? RGB(199, 201, 207) : palette_.navText);

    const EngineStatus status = engine_.Status();
    const bool ready = status.keyboardReady && status.audioReady &&
                       status.dynamicLightingAvailable > 0;
    const int pillWidth = Scale(250);
    const RECT pill{client.right - Scale(18) - pillWidth,
                    Scale(14),
                    client.right - Scale(18),
                    Scale(52)};
    FillRounded(context, pill, palette_.accentSoft, Scale(8));
    DrawStatusDot(context, {pill.left + Scale(18), (pill.top + pill.bottom) / 2}, ready);
    DrawLabel(context,
              StatusText(),
              {pill.left + Scale(32), pill.top, pill.right - Scale(12), pill.bottom},
              smallFont_,
              palette_.accentSoftText,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void TrayApp::DrawNavigation(HDC context, const RECT& bounds) {
    FillRounded(context, bounds, palette_.navigation, Scale(8));

    struct Item {
        int id;
        const wchar_t* label;
        int icon;
    };
    constexpr Item items[] = {
        {kNavLighting, L"灯效", kIconLighting},
        {kNavAudio, L"音乐律动", kIconAudio},
        {kNavDevices, L"设备", kIconDevices},
    };

    int top = bounds.top + Scale(18);
    for (int index = 0; index < static_cast<int>(std::size(items)); ++index) {
        const auto& item = items[index];
        const RECT itemBounds{bounds.left + Scale(10),
                              top,
                              bounds.right - Scale(10),
                              top + Scale(74)};
        const bool selected = selectedPage_ == index;
        const bool hovered = hoveredTarget_ == item.id;
        if (selected || hovered) {
            FillRounded(context,
                        itemBounds,
                        selected ? palette_.selectedNav : palette_.surfaceHover,
                        Scale(8),
                        selected ? std::optional<COLORREF>(palette_.border) : std::nullopt);
        }
        if (selected) {
            RECT marker{itemBounds.left, itemBounds.top + Scale(14), itemBounds.left + Scale(3),
                        itemBounds.bottom - Scale(14)};
            FillRounded(context, marker, kBrand, Scale(2));
        }
        const RECT iconBounds{itemBounds.left + Scale(28),
                              itemBounds.top + Scale(10),
                              itemBounds.left + Scale(50),
                              itemBounds.top + Scale(32)};
        DrawIcon(context, item.icon, iconBounds, selected ? kBrand : palette_.navIcon);
        DrawLabel(context,
                  item.label,
                  {itemBounds.left + Scale(4), itemBounds.top + Scale(38), itemBounds.right - Scale(4),
                   itemBounds.bottom - Scale(5)},
                  smallFont_,
                  selected ? palette_.accentSoftText : palette_.navText,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        AddHitTarget(item.id, itemBounds);
        top += Scale(82);
    }

    const EngineStatus status = engine_.Status();
    DrawStatusDot(context,
                  {bounds.left + Scale(18), bounds.bottom - Scale(47)},
                  status.running && !status.paused);
    DrawLabel(context,
              status.paused ? L"已暂停" : (status.running ? L"同步中" : L"连接中"),
              {bounds.left + Scale(30), bounds.bottom - Scale(60), bounds.right - Scale(7),
               bounds.bottom - Scale(34)},
              smallFont_,
              palette_.hint,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    DrawLabel(context,
              L"v0.2",
              {bounds.left + Scale(12), bounds.bottom - Scale(31), bounds.right - Scale(12),
               bounds.bottom - Scale(10)},
              smallFont_,
              palette_.hint,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void TrayApp::DrawLightingPage(HDC context, const RECT& workspace, const RECT& panel) {
    FillRounded(context, workspace, palette_.workspace, Scale(8));
    FillRounded(context, panel, palette_.panel, Scale(8));

    DrawLabel(context,
              L"IROK MG75 PRO",
              {workspace.left + Scale(26), workspace.top + Scale(18), workspace.right - Scale(170),
               workspace.top + Scale(53)},
              titleFont_,
              palette_.text,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    DrawLabel(context,
              L"灯光预览",
              {workspace.left + Scale(27), workspace.top + Scale(53), workspace.right - Scale(150),
               workspace.top + Scale(78)},
              smallFont_,
              palette_.hint);

    const RECT modeBadge{workspace.right - Scale(150),
                         workspace.top + Scale(22),
                         workspace.right - Scale(24),
                         workspace.top + Scale(57)};
    FillRounded(context, modeBadge, palette_.accentSoft, Scale(8));
    DrawLabel(context,
              LightingModeName(engine_.GetLightingMode()),
              modeBadge,
              smallFont_,
              kBrandDark,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    RECT keyboard{workspace.left + Scale(26),
                  workspace.top + Scale(94),
                  workspace.right - Scale(26),
                  workspace.bottom - Scale(102)};
    DrawKeyboard(context, keyboard);

    const RgbColor first = engine_.GetPrimaryColor();
    const RgbColor second = engine_.GetSecondaryColor();
    const RECT palette{workspace.left + Scale(27),
                       workspace.bottom - Scale(72),
                       workspace.right - Scale(27),
                       workspace.bottom - Scale(56)};
    FillGradient(context, palette, ToColorRef(first), ToColorRef(second));
    const EngineStatus status = engine_.Status();
    DrawLabel(context,
              L"当前输出  " + HexColor(status.color),
              {palette.left, palette.bottom + Scale(8), workspace.right - Scale(27),
               workspace.bottom - Scale(18)},
              smallFont_,
              palette_.hint,
              DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    const int left = panel.left + Scale(22);
    const int right = panel.right - Scale(22);
    int y = panel.top + Scale(16);
    DrawLabel(context,
              L"灯效设置",
              {left, y, right, y + Scale(35)},
              titleFont_,
              palette_.text);
    y += Scale(51);
    DrawLabel(context, L"灯效类型", {left, y, right, y + Scale(24)}, headingFont_, palette_.text);
    y += Scale(29);

    const int gap = Scale(8);
    const int half = (right - left - gap) / 2;
    const int buttonHeight = Scale(36);
    DrawButton(context,
               kModeAudio,
               {left, y, left + half, y + buttonHeight},
               L"音乐律动",
               engine_.GetLightingMode() == LightingMode::Audio);
    DrawButton(context,
               kModeStatic,
               {left + half + gap, y, right, y + buttonHeight},
               L"常亮",
               engine_.GetLightingMode() == LightingMode::Static);
    y += buttonHeight + gap;
    DrawButton(context,
               kModeBreathing,
               {left, y, left + half, y + buttonHeight},
               L"呼吸",
               engine_.GetLightingMode() == LightingMode::Breathing);
    DrawButton(context,
               kModeCycle,
               {left + half + gap, y, right, y + buttonHeight},
               L"色域循环",
               engine_.GetLightingMode() == LightingMode::ColorCycle);
    y += buttonHeight + Scale(17);
    DrawSeparator(context, left, right, y);
    y += Scale(14);

    const LightingMode lightingMode = engine_.GetLightingMode();
    const bool audioGradient = lightingMode == LightingMode::Audio &&
                               engine_.GetAudioColorMode() == AudioColorMode::GradientCycle;
    const bool paletteEnabled = lightingMode == LightingMode::ColorCycle || audioGradient;
    const bool primaryEnabled = lightingMode == LightingMode::Static ||
                                lightingMode == LightingMode::Breathing || paletteEnabled;
    const bool speedEnabled = lightingMode == LightingMode::Breathing || paletteEnabled;

    DrawLabel(context, L"灯光颜色", {left, y, right, y + Scale(23)}, headingFont_, palette_.text);
    y += Scale(28);
    DrawColorSwatch(context,
                    kPrimaryColor,
                    {left, y, left + half, y + Scale(39)},
                    engine_.GetPrimaryColor(),
                    primaryEnabled);
    DrawColorSwatch(context,
                    kSecondaryColor,
                    {left + half + gap, y, right, y + Scale(39)},
                    engine_.GetSecondaryColor(),
                    paletteEnabled);
    y += Scale(51);

    DrawLabel(context,
              L"亮度",
              {left, y, right - Scale(55), y + Scale(22)},
              headingFont_,
              palette_.text);
    DrawLabel(context,
              std::to_wstring(engine_.GetMaxBrightness()) + L"%",
              {right - Scale(54), y, right, y + Scale(22)},
              smallFont_,
              kBrandDark,
              DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    y += Scale(22);
    DrawSlider(context,
               kBrightnessSlider,
               {left, y, right, y + Scale(28)},
               engine_.GetMaxBrightness(),
               10,
               100);
    y += Scale(39);

    DrawLabel(context,
              L"速度",
              {left, y, right - Scale(55), y + Scale(22)},
              headingFont_,
              speedEnabled ? palette_.text : palette_.disabledText);
    DrawLabel(context,
              std::to_wstring(engine_.GetEffectSpeed()),
              {right - Scale(54), y, right, y + Scale(22)},
              smallFont_,
              speedEnabled ? kBrandDark : palette_.disabledText,
              DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    y += Scale(22);
    DrawSlider(context,
               kSpeedSlider,
               {left, y, right, y + Scale(28)},
               engine_.GetEffectSpeed(),
               1,
               100,
               speedEnabled);
    y += Scale(39);

    DrawLabel(context, L"方向", {left, y, right, y + Scale(22)}, headingFont_, palette_.text);
    y += Scale(26);
    DrawButton(context,
               kDirectionForward,
               {left, y, left + half, y + Scale(35)},
               L"正向",
               !engine_.GetReverseDirection(),
               paletteEnabled);
    DrawButton(context,
               kDirectionReverse,
               {left + half + gap, y, right, y + Scale(35)},
               L"反向",
               engine_.GetReverseDirection(),
               paletteEnabled);

    if (panel.bottom - panel.top >= Scale(575)) {
        const RECT footer{left,
                          panel.bottom - Scale(56),
                          right,
                          panel.bottom - Scale(18)};
        FillRounded(context, footer, palette_.surfaceAlt, Scale(8));
        DrawStatusDot(context,
                      {footer.left + Scale(15), (footer.top + footer.bottom) / 2},
                      !engine_.IsPaused());
        DrawLabel(context,
                  engine_.IsPaused() ? L"灯光输出已暂停" : L"全部在线设备已应用",
                  {footer.left + Scale(29), footer.top, footer.right - Scale(8), footer.bottom},
                  smallFont_,
                  palette_.hint,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}

void TrayApp::DrawAudioPage(HDC context, const RECT& workspace, const RECT& panel) {
    FillRounded(context, workspace, palette_.workspace, Scale(8));
    FillRounded(context, panel, palette_.panel, Scale(8));

    DrawLabel(context,
              L"音乐律动",
              {workspace.left + Scale(26), workspace.top + Scale(18), workspace.right - Scale(26),
               workspace.top + Scale(53)},
              titleFont_,
              palette_.text);
    DrawLabel(context,
              L"WASAPI 系统音频实时响应",
              {workspace.left + Scale(27), workspace.top + Scale(53), workspace.right - Scale(26),
               workspace.top + Scale(78)},
              smallFont_,
              palette_.hint);

    const RECT visualizer{workspace.left + Scale(28),
                          workspace.top + Scale(105),
                          workspace.right - Scale(28),
                          workspace.bottom - Scale(130)};
    DrawAudioVisualizer(context, visualizer);

    const EngineStatus status = engine_.Status();
    DrawLabel(context,
              L"实时音量",
              {workspace.left + Scale(28), workspace.bottom - Scale(106), workspace.right - Scale(180),
               workspace.bottom - Scale(79)},
              headingFont_,
              palette_.text);
    DrawLabel(context,
              std::to_wstring(static_cast<int>(std::lround(
                  std::clamp(status.audioLevel, 0.0F, 1.0F) * 100.0F))) +
                  L"%",
              {workspace.right - Scale(160), workspace.bottom - Scale(106),
               workspace.right - Scale(28), workspace.bottom - Scale(79)},
              headingFont_,
              kBrandDark,
              DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    DrawLabel(context,
              status.audioName.empty() ? L"正在等待系统音频设备" : status.audioName,
              {workspace.left + Scale(28), workspace.bottom - Scale(68), workspace.right - Scale(28),
               workspace.bottom - Scale(31)},
              smallFont_,
              palette_.hint,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    const int left = panel.left + Scale(22);
    const int right = panel.right - Scale(22);
    const int gap = Scale(8);
    int y = panel.top + Scale(16);
    DrawLabel(context,
              L"音频设置",
              {left, y, right, y + Scale(35)},
              titleFont_,
              palette_.text);
    y += Scale(51);

    DrawLabel(context, L"色彩映射", {left, y, right, y + Scale(23)}, headingFont_, palette_.text);
    y += Scale(28);
    const int half = (right - left - gap) / 2;
    DrawButton(context,
               kAudioSpectrum,
               {left, y, left + half, y + Scale(36)},
               L"全频谱",
               engine_.GetAudioColorMode() == AudioColorMode::Spectrum);
    DrawButton(context,
               kAudioGradient,
               {left + half + gap, y, right, y + Scale(36)},
               L"自定义色域",
               engine_.GetAudioColorMode() == AudioColorMode::GradientCycle);
    y += Scale(52);

    DrawLabel(context, L"灵敏度", {left, y, right, y + Scale(23)}, headingFont_, palette_.text);
    y += Scale(28);
    const int thirdGap = Scale(6);
    const int third = (right - left - thirdGap * 2) / 3;
    DrawButton(context,
               kSensitivityLow,
               {left, y, left + third, y + Scale(34)},
               L"低",
               engine_.GetSensitivity() == Sensitivity::Low);
    DrawButton(context,
               kSensitivityNormal,
               {left + third + thirdGap, y, left + third * 2 + thirdGap, y + Scale(34)},
               L"标准",
               engine_.GetSensitivity() == Sensitivity::Normal);
    DrawButton(context,
               kSensitivityHigh,
               {left + third * 2 + thirdGap * 2, y, right, y + Scale(34)},
               L"高",
               engine_.GetSensitivity() == Sensitivity::High);
    y += Scale(50);
    DrawSeparator(context, left, right, y);
    y += Scale(14);

    const bool custom = engine_.GetAudioColorMode() == AudioColorMode::GradientCycle;
    DrawLabel(context, L"循环色域", {left, y, right, y + Scale(23)}, headingFont_, palette_.text);
    y += Scale(28);
    DrawColorSwatch(context,
                    kPrimaryColor,
                    {left, y, left + half, y + Scale(39)},
                    engine_.GetPrimaryColor(),
                    custom);
    DrawColorSwatch(context,
                    kSecondaryColor,
                    {left + half + gap, y, right, y + Scale(39)},
                    engine_.GetSecondaryColor(),
                    custom);
    y += Scale(51);

    DrawLabel(context,
              L"循环速度",
              {left, y, right - Scale(55), y + Scale(22)},
              headingFont_,
              custom ? palette_.text : palette_.disabledText);
    DrawLabel(context,
              std::to_wstring(engine_.GetEffectSpeed()),
              {right - Scale(54), y, right, y + Scale(22)},
              smallFont_,
              custom ? kBrandDark : palette_.disabledText,
              DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    y += Scale(22);
    DrawSlider(context,
               kSpeedSlider,
               {left, y, right, y + Scale(28)},
               engine_.GetEffectSpeed(),
               1,
               100,
               custom);
    y += Scale(48);

    DrawSeparator(context, left, right, y);
    y += Scale(15);
    DrawLabel(context,
              L"活动灯效",
              {left, y, right - Scale(58), y + Scale(30)},
              bodyFont_,
              palette_.text,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    DrawToggle(context,
               kEnableAudio,
               {right - Scale(45), y + Scale(3), right, y + Scale(27)},
               engine_.GetLightingMode() == LightingMode::Audio);
    AddHitTarget(kEnableAudio, {left, y, right, y + Scale(30)});

    if (panel.bottom - panel.top >= Scale(575)) {
        const RECT footer{left,
                          panel.bottom - Scale(56),
                          right,
                          panel.bottom - Scale(18)};
        FillRounded(context, footer, palette_.surfaceAlt, Scale(8));
        DrawStatusDot(context,
                      {footer.left + Scale(15), (footer.top + footer.bottom) / 2},
                      status.audioReady);
        DrawLabel(context,
                  status.audioReady ? L"音频采集正常" : L"正在等待音频设备",
                  {footer.left + Scale(29), footer.top, footer.right - Scale(8), footer.bottom},
                  smallFont_,
                  palette_.hint,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}

void TrayApp::DrawDevicesPage(HDC context, const RECT& workspace, const RECT& panel) {
    FillRounded(context, workspace, palette_.workspace, Scale(8));
    FillRounded(context, panel, palette_.panel, Scale(8));

    DrawLabel(context,
              L"设备状态",
              {workspace.left + Scale(26), workspace.top + Scale(18), workspace.right - Scale(26),
               workspace.top + Scale(53)},
              titleFont_,
              palette_.text);
    DrawLabel(context,
              L"IROK 键盘、AM 接收器与 Windows 灯光服务",
              {workspace.left + Scale(27), workspace.top + Scale(53), workspace.right - Scale(26),
               workspace.top + Scale(78)},
              smallFont_,
              palette_.hint);

    const EngineStatus status = engine_.Status();
    const int gap = Scale(14);
    const int left = workspace.left + Scale(26);
    const int right = workspace.right - Scale(26);
    const int cardWidth = (right - left - gap) / 2;
    const int cardHeight = Scale(124);
    const int rowOne = workspace.top + Scale(101);
    const int rowTwo = rowOne + cardHeight + gap;

    DrawDeviceCard(context,
                   {left, rowOne, left + cardWidth, rowOne + cardHeight},
                   L"系统音频",
                   status.audioName.empty() ? L"等待音频端点" : status.audioName,
                   status.audioReady,
                   kIconAudio);
    DrawDeviceCard(context,
                   {left + cardWidth + gap, rowOne, right, rowOne + cardHeight},
                   L"IROK MG75 PRO",
                   status.keyboardName.empty()
                       ? L"等待 USB HID 设备"
                       : status.keyboardName +
                             (status.keyboardFirmware.empty() ? L"" : L"  固件 " + status.keyboardFirmware),
                   status.keyboardReady,
                   kIconKeyboard,
                   kOpenKeyboardDriver,
                   L"打开 IROK 网页驱动  >");

    const bool dynamicReady = status.dynamicLightingAvailable > 0;
    const std::wstring dynamicDetail =
        std::to_wstring(status.dynamicLightingDevices) + L" 台机箱设备，" +
        std::to_wstring(status.dynamicLightingAvailable) + L" 台可用";
    DrawDeviceCard(context,
                   {left, rowTwo, left + cardWidth, rowTwo + cardHeight},
                   L"Windows 动态光效",
                   dynamicDetail,
                   dynamicReady,
                   kIconChassis);
    DrawDeviceCard(context,
                   {left + cardWidth + gap, rowTwo, right, rowTwo + cardHeight},
                   L"AM INFINITY 8K 鼠标",
                   status.angryMiaoReceiverName.empty() ? L"等待 USB HID 接收器"
                                                        : status.angryMiaoReceiverName,
                   status.angryMiaoReceiverReady,
                   kIconMouse,
                   kOpenMouseDriver,
                   L"打开 AM Master  >");

    const RECT note{left,
                    workspace.bottom - Scale(98),
                    right,
                    workspace.bottom - Scale(31)};
    FillRounded(context, note, palette_.note, Scale(8));
    DrawLabel(context,
              L"输出目标  IROK MG75 PRO  ·  AM INFINITY 8K  ·  Windows 动态光效",
              {note.left + Scale(17), note.top + Scale(8), note.right - Scale(17), note.bottom - Scale(8)},
              bodyFont_,
              palette_.noteText,
              DT_LEFT | DT_VCENTER | DT_WORDBREAK);

    const int panelLeft = panel.left + Scale(22);
    const int panelRight = panel.right - Scale(22);
    int y = panel.top + Scale(16);
    DrawLabel(context,
              L"设备与应用",
              {panelLeft, y, panelRight, y + Scale(35)},
              titleFont_,
              palette_.text);
    y += Scale(54);
    DrawLabel(context, L"连接", {panelLeft, y, panelRight, y + Scale(23)}, headingFont_, palette_.text);
    y += Scale(30);
    DrawButton(context,
               kReconnect,
               {panelLeft, y, panelRight, y + Scale(38)},
               L"重新连接全部设备");
    y += Scale(48);
    DrawButton(context,
               kOpenDynamicLighting,
               {panelLeft, y, panelRight, y + Scale(38)},
               L"打开 Windows 动态光效");
    y += Scale(48);
    DrawButton(context,
               kOpenLog,
               {panelLeft, y, panelRight, y + Scale(38)},
               L"打开运行日志");
    y += Scale(55);
    DrawSeparator(context, panelLeft, panelRight, y);
    y += Scale(16);

    DrawLabel(context,
              L"界面主题",
              {panelLeft, y, panelRight, y + Scale(23)},
              headingFont_,
              palette_.text);
    y += Scale(29);
    const int themeGap = Scale(8);
    const int themeHalf = (panelRight - panelLeft - themeGap) / 2;
    DrawButton(context,
               kThemeDark,
               {panelLeft, y, panelLeft + themeHalf, y + Scale(36)},
               L"暗色",
               settings_.themeMode == ThemeMode::Dark);
    DrawButton(context,
               kThemeLight,
               {panelLeft + themeHalf + themeGap, y, panelRight, y + Scale(36)},
               L"亮色",
               settings_.themeMode == ThemeMode::Light);
    y += Scale(52);
    DrawSeparator(context, panelLeft, panelRight, y);
    y += Scale(15);

    DrawLabel(context,
              L"随 Windows 启动",
              {panelLeft, y, panelRight - Scale(58), y + Scale(30)},
              bodyFont_,
              palette_.text);
    DrawToggle(context,
               kStartupToggle,
               {panelRight - Scale(45), y + Scale(3), panelRight, y + Scale(27)},
               IsStartupEnabled());
    AddHitTarget(kStartupToggle, {panelLeft, y, panelRight, y + Scale(30)});
    y += Scale(47);

    DrawLabel(context,
              L"灯光同步",
              {panelLeft, y, panelRight - Scale(58), y + Scale(30)},
              bodyFont_,
              palette_.text);
    DrawToggle(context,
               kPause,
               {panelRight - Scale(45), y + Scale(3), panelRight, y + Scale(27)},
               !engine_.IsPaused());
    AddHitTarget(kPause, {panelLeft, y, panelRight, y + Scale(30)});

    DrawButton(context,
               kExit,
               {panelLeft, panel.bottom - Scale(56), panelRight, panel.bottom - Scale(18)},
               L"退出 LightController");
}

void TrayApp::DrawKeyboard(HDC context, const RECT& bounds) {
    FillRounded(context, bounds, RGB(42, 43, 47), Scale(8), RGB(65, 66, 71));

    const EngineStatus status = engine_.Status();
    RgbColor output = status.color;
    if (!status.running || status.paused) {
        output = {92, 92, 98};
    }
    const RgbColor dark{58, 59, 64};
    const RgbColor keyColor = BlendColor(dark, output, status.paused ? 0.08F : 0.30F);

    struct Key {
        const wchar_t* label;
        float units;
    };
    static const std::vector<std::vector<Key>> rows = {
        {{L"Esc", 1}, {L"F1", 1}, {L"F2", 1}, {L"F3", 1}, {L"F4", 1}, {L"F5", 1},
         {L"F6", 1}, {L"F7", 1}, {L"F8", 1}, {L"F9", 1}, {L"F10", 1}, {L"F11", 1},
         {L"F12", 1}, {L"Del", 1}},
        {{L"~", 1}, {L"1", 1}, {L"2", 1}, {L"3", 1}, {L"4", 1}, {L"5", 1},
         {L"6", 1}, {L"7", 1}, {L"8", 1}, {L"9", 1}, {L"0", 1}, {L"-", 1},
         {L"=", 1}, {L"Back", 2}, {L"Home", 1}},
        {{L"Tab", 1.5F}, {L"Q", 1}, {L"W", 1}, {L"E", 1}, {L"R", 1}, {L"T", 1},
         {L"Y", 1}, {L"U", 1}, {L"I", 1}, {L"O", 1}, {L"P", 1}, {L"[", 1},
         {L"]", 1}, {L"\\", 1.5F}, {L"PgUp", 1}},
        {{L"Caps", 1.75F}, {L"A", 1}, {L"S", 1}, {L"D", 1}, {L"F", 1}, {L"G", 1},
         {L"H", 1}, {L"J", 1}, {L"K", 1}, {L"L", 1}, {L";", 1}, {L"'", 1},
         {L"Enter", 2.25F}, {L"PgDn", 1}},
        {{L"Shift", 2.25F}, {L"Z", 1}, {L"X", 1}, {L"C", 1}, {L"V", 1}, {L"B", 1},
         {L"N", 1}, {L"M", 1}, {L",", 1}, {L".", 1}, {L"/", 1}, {L"Shift", 2.75F},
         {L"Up", 1}, {L"End", 1}},
        {{L"Ctrl", 1.25F}, {L"Win", 1.25F}, {L"Alt", 1.25F}, {L"", 6.25F},
         {L"Alt", 1.25F}, {L"Fn", 1.25F}, {L"Ctrl", 1.25F}, {L"Left", 1}, {L"Down", 1},
         {L"Right", 1}},
    };

    const int margin = Scale(15);
    const int gap = Scale(4);
    const int contentWidth = bounds.right - bounds.left - margin * 2;
    const int contentHeight = bounds.bottom - bounds.top - margin * 2;
    const int rowHeight = std::max(Scale(21), (contentHeight - gap * 5) / 6);
    int y = bounds.top + margin;

    for (std::size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
        const auto& row = rows[rowIndex];
        float totalUnits = 0.0F;
        for (const auto& key : row) {
            totalUnits += key.units;
        }
        const int available = contentWidth - gap * (static_cast<int>(row.size()) - 1);
        int x = bounds.left + margin;
        float usedUnits = 0.0F;
        for (std::size_t index = 0; index < row.size(); ++index) {
            const auto& key = row[index];
            usedUnits += key.units;
            const int next = index + 1 == row.size()
                                 ? bounds.right - margin
                                 : bounds.left + margin +
                                       static_cast<int>(std::lround(available * usedUnits / totalUnits)) +
                                       gap * static_cast<int>(index + 1);
            const RECT keyBounds{x, y, next - (index + 1 == row.size() ? 0 : gap), y + rowHeight};
            FillRounded(context,
                        keyBounds,
                        ToColorRef(keyColor),
                        Scale(4),
                        ToColorRef(BlendColor(keyColor, {210, 210, 215}, 0.22F)));
            RECT light{keyBounds.left + Scale(3),
                       keyBounds.bottom - Scale(3),
                       keyBounds.right - Scale(3),
                       keyBounds.bottom - Scale(1)};
            HBRUSH lightBrush = CreateSolidBrush(ToColorRef(output));
            FillRect(context, &light, lightBrush);
            DeleteObject(lightBrush);
            DrawLabel(context,
                      key.label,
                      keyBounds,
                      keyFont_,
                      RGB(240, 240, 243),
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            x = next;
        }
        y += rowHeight + gap;
    }
}

void TrayApp::DrawAudioVisualizer(HDC context, const RECT& bounds) {
    const EngineStatus status = engine_.Status();
    const float level = std::clamp(status.audioLevel, 0.0F, 1.0F);
    FillRounded(context, bounds, RGB(38, 39, 43), Scale(8), RGB(60, 61, 66));

    const int bars = 38;
    const int gap = Scale(4);
    const int innerWidth = bounds.right - bounds.left - Scale(28);
    const int barWidth = std::max(Scale(3), (innerWidth - gap * (bars - 1)) / bars);
    const int totalBarsWidth = barWidth * bars + gap * (bars - 1);
    int x = bounds.left + (bounds.right - bounds.left - totalBarsWidth) / 2;
    const int baseline = bounds.bottom - Scale(24);
    const int maximumHeight =
        std::max(Scale(20), static_cast<int>(bounds.bottom - bounds.top) - Scale(54));
    const RgbColor first = engine_.GetPrimaryColor();
    const RgbColor second = engine_.GetSecondaryColor();

    for (int index = 0; index < bars; ++index) {
        const float position = static_cast<float>(index) / static_cast<float>(bars - 1);
        const float wave = 0.42F + 0.58F * std::abs(std::sin(
                                             timerTicks_ * 0.105F + index * 0.47F));
        const float shoulder = 0.42F + 0.58F * std::sin(position * 3.14159265F);
        const float idle = 0.045F + 0.025F * std::abs(std::sin(timerTicks_ * 0.04F + index));
        const float magnitude = std::clamp(idle + level * wave * shoulder, 0.03F, 1.0F);
        const int height = std::max(Scale(3), static_cast<int>(std::lround(maximumHeight * magnitude)));
        RECT bar{x, baseline - height, x + barWidth, baseline};
        const RgbColor color = engine_.GetAudioColorMode() == AudioColorMode::Spectrum
                                   ? BlendColor({65, 209, 151}, {255, 82, 124}, position)
                                   : BlendColor(first, second, position);
        HBRUSH brush = CreateSolidBrush(ToColorRef(color));
        FillRect(context, &bar, brush);
        DeleteObject(brush);
        x += barWidth + gap;
    }

    const RECT palette{bounds.left + Scale(14),
                       bounds.bottom - Scale(11),
                       bounds.right - Scale(14),
                       bounds.bottom - Scale(7)};
    FillGradient(context, palette, ToColorRef(first), ToColorRef(second));
}

void TrayApp::DrawDeviceCard(HDC context,
                             const RECT& bounds,
                             const std::wstring& name,
                             const std::wstring& detail,
                             bool ready,
                             int icon,
                             int targetId,
                             const std::wstring& action) {
    const bool interactive = targetId != 0;
    const bool hovered = interactive && hoveredTarget_ == targetId;
    FillRounded(context,
                bounds,
                hovered ? palette_.surfaceHover : palette_.surface,
                Scale(8),
                hovered ? kBrand : palette_.border);
    const RECT iconTile{bounds.left + Scale(16),
                        bounds.top + Scale(17),
                        bounds.left + Scale(55),
                        bounds.top + Scale(56)};
    const COLORREF readyTile = settings_.themeMode == ThemeMode::Dark
                                   ? RGB(28, 64, 52)
                                   : RGB(235, 249, 242);
    const COLORREF waitingTile = settings_.themeMode == ThemeMode::Dark
                                     ? RGB(71, 50, 30)
                                     : RGB(255, 244, 230);
    FillRounded(context, iconTile, ready ? readyTile : waitingTile, Scale(7));
    DrawIcon(context, icon, {iconTile.left + Scale(9), iconTile.top + Scale(9),
                             iconTile.right - Scale(9), iconTile.bottom - Scale(9)},
             ready ? kReady : kWaiting);
    DrawStatusDot(context, {bounds.right - Scale(18), bounds.top + Scale(22)}, ready);
    DrawLabel(context,
              name,
              {bounds.left + Scale(67), bounds.top + Scale(14), bounds.right - Scale(31),
               bounds.top + Scale(43)},
              headingFont_,
              palette_.text,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    DrawLabel(context,
              ready ? L"已连接" : L"等待连接",
              {bounds.left + Scale(67), bounds.top + Scale(42), bounds.right - Scale(15),
               bounds.top + Scale(65)},
              smallFont_,
              ready ? kReady : kWaiting,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawLabel(context,
              detail,
              {bounds.left + Scale(16), bounds.top + Scale(70), bounds.right - Scale(16),
               bounds.top + Scale(94)},
              smallFont_,
              palette_.hint,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (interactive) {
        DrawLabel(context,
                  action,
                  {bounds.left + Scale(16), bounds.bottom - Scale(29), bounds.right - Scale(16),
                   bounds.bottom - Scale(8)},
                  smallFont_,
                  hovered ? kBrand : palette_.accentSoftText,
                  DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        AddHitTarget(targetId, bounds);
    }
}

void TrayApp::FillRounded(HDC context,
                          const RECT& bounds,
                          COLORREF fill,
                          int radius,
                          std::optional<COLORREF> border) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = border ? CreatePen(PS_SOLID, 1, *border)
                      : static_cast<HPEN>(GetStockObject(NULL_PEN));
    HGDIOBJ oldBrush = SelectObject(context, brush);
    HGDIOBJ oldPen = SelectObject(context, pen);
    RoundRect(context,
              bounds.left,
              bounds.top,
              bounds.right,
              bounds.bottom,
              std::max(1, radius * 2),
              std::max(1, radius * 2));
    SelectObject(context, oldBrush);
    SelectObject(context, oldPen);
    DeleteObject(brush);
    if (border) {
        DeleteObject(pen);
    }
}

void TrayApp::DrawLabel(HDC context,
                        const std::wstring& text,
                        RECT bounds,
                        HFONT font,
                        COLORREF color,
                        UINT format) {
    HGDIOBJ oldFont = SelectObject(context, font);
    const COLORREF oldColor = SetTextColor(context, color);
    const int oldMode = SetBkMode(context, TRANSPARENT);
    DrawTextW(context, text.c_str(), -1, &bounds, format);
    SetBkMode(context, oldMode);
    SetTextColor(context, oldColor);
    SelectObject(context, oldFont);
}

void TrayApp::DrawButton(HDC context,
                         int id,
                         const RECT& bounds,
                         const std::wstring& text,
                         bool selected,
                         bool enabled) {
    const bool hovered = enabled && hoveredTarget_ == id;
    COLORREF fill = palette_.surfaceAlt;
    COLORREF border = palette_.border;
    COLORREF color = palette_.text;
    if (!enabled) {
        fill = palette_.panel;
        color = palette_.disabledText;
    } else if (selected) {
        fill = kBrand;
        border = kBrandDark;
        color = RGB(255, 255, 255);
    } else if (hovered) {
        fill = palette_.surfaceHover;
        border = kBrand;
        color = palette_.accentSoftText;
    }
    FillRounded(context, bounds, fill, Scale(7), border);
    DrawLabel(context,
              text,
              bounds,
              bodyFont_,
              color,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (enabled) {
        AddHitTarget(id, bounds);
    }
}

void TrayApp::DrawSlider(HDC context,
                         int id,
                         const RECT& bounds,
                         int value,
                         int minimum,
                         int maximum,
                         bool enabled) {
    const int knobRadius = Scale(7);
    const int trackLeft = bounds.left + knobRadius;
    const int trackRight = bounds.right - knobRadius;
    const int center = (bounds.top + bounds.bottom) / 2;
    const float amount = static_cast<float>(std::clamp(value, minimum, maximum) - minimum) /
                         static_cast<float>(std::max(1, maximum - minimum));
    const int knobX = trackLeft +
                      static_cast<int>(std::lround((trackRight - trackLeft) * amount));
    const RECT track{trackLeft, center - Scale(3), trackRight, center + Scale(3)};
    FillRounded(context,
                track,
                enabled ? palette_.track : palette_.trackDisabled,
                Scale(3));
    if (knobX > trackLeft) {
        FillRounded(context,
                    {trackLeft, track.top, knobX, track.bottom},
                    enabled ? kBrand : palette_.disabledText,
                    Scale(3));
    }

    HBRUSH brush = CreateSolidBrush(palette_.knob);
    HPEN pen = CreatePen(PS_SOLID,
                         Scale(2),
                         enabled ? (hoveredTarget_ == id ? kBrandDark : kBrand)
                                 : palette_.disabledText);
    HGDIOBJ oldBrush = SelectObject(context, brush);
    HGDIOBJ oldPen = SelectObject(context, pen);
    Ellipse(context,
            knobX - knobRadius,
            center - knobRadius,
            knobX + knobRadius + 1,
            center + knobRadius + 1);
    SelectObject(context, oldBrush);
    SelectObject(context, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
    if (enabled) {
        AddHitTarget(id, bounds);
    }
}

void TrayApp::DrawToggle(HDC context, int id, const RECT& bounds, bool checked) {
    const bool hovered = hoveredTarget_ == id;
    FillRounded(context,
                bounds,
                checked ? (hovered ? kBrandDark : kBrand)
                        : (hovered ? palette_.toggleOffHover : palette_.toggleOff),
                (bounds.bottom - bounds.top) / 2);
    const int diameter = bounds.bottom - bounds.top - Scale(6);
    const int left = checked ? bounds.right - Scale(3) - diameter : bounds.left + Scale(3);
    HBRUSH knob = CreateSolidBrush(palette_.knob);
    HGDIOBJ oldBrush = SelectObject(context, knob);
    HGDIOBJ oldPen = SelectObject(context, GetStockObject(NULL_PEN));
    Ellipse(context,
            left,
            bounds.top + Scale(3),
            left + diameter,
            bounds.top + Scale(3) + diameter);
    SelectObject(context, oldBrush);
    SelectObject(context, oldPen);
    DeleteObject(knob);
    AddHitTarget(id, bounds);
}

void TrayApp::DrawColorSwatch(HDC context,
                              int id,
                              const RECT& bounds,
                              RgbColor color,
                              bool enabled) {
    const RgbColor disabledSurface = FromColorRef(palette_.surfaceAlt);
    RgbColor displayed = enabled ? color : BlendColor(color, disabledSurface, 0.72F);
    const COLORREF border = hoveredTarget_ == id && enabled ? kBrandDark : palette_.border;
    FillRounded(context, bounds, ToColorRef(displayed), Scale(7), border);
    const int luminance = displayed.r * 299 + displayed.g * 587 + displayed.b * 114;
    DrawLabel(context,
              HexColor(color),
              bounds,
              smallFont_,
              luminance > 150000 ? RGB(31, 31, 34) : RGB(255, 255, 255),
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (enabled) {
        AddHitTarget(id, bounds);
    }
}

void TrayApp::DrawSeparator(HDC context, int left, int right, int y) {
    RECT line{left, y, right, y + 1};
    HBRUSH brush = CreateSolidBrush(palette_.line);
    FillRect(context, &line, brush);
    DeleteObject(brush);
}

void TrayApp::DrawStatusDot(HDC context, POINT center, bool ready) {
    const int radius = Scale(4);
    HBRUSH brush = CreateSolidBrush(ready ? kReady : kWaiting);
    HGDIOBJ oldBrush = SelectObject(context, brush);
    HGDIOBJ oldPen = SelectObject(context, GetStockObject(NULL_PEN));
    Ellipse(context,
            center.x - radius,
            center.y - radius,
            center.x + radius + 1,
            center.y + radius + 1);
    SelectObject(context, oldBrush);
    SelectObject(context, oldPen);
    DeleteObject(brush);
}

void TrayApp::DrawIcon(HDC context, int icon, const RECT& bounds, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, std::max(1, Scale(2)), color);
    HGDIOBJ oldPen = SelectObject(context, pen);
    HGDIOBJ oldBrush = SelectObject(context, GetStockObject(NULL_BRUSH));
    const int left = bounds.left;
    const int top = bounds.top;
    const int right = bounds.right;
    const int bottom = bounds.bottom;
    const int width = right - left;
    const int height = bottom - top;

    if (icon == kIconLighting) {
        Ellipse(context, left + width / 4, top + height / 8, right - width / 4, top + height * 5 / 8);
        MoveToEx(context, left + width * 2 / 5, top + height * 5 / 8, nullptr);
        LineTo(context, left + width * 2 / 5, bottom - height / 8);
        LineTo(context, right - width * 2 / 5, bottom - height / 8);
        LineTo(context, right - width * 2 / 5, top + height * 5 / 8);
    } else if (icon == kIconAudio) {
        MoveToEx(context, left + width / 6, bottom - height / 5, nullptr);
        LineTo(context, left + width / 6, top + height * 3 / 5);
        MoveToEx(context, left + width * 2 / 5, bottom - height / 5, nullptr);
        LineTo(context, left + width * 2 / 5, top + height / 4);
        MoveToEx(context, left + width * 3 / 5, bottom - height / 5, nullptr);
        LineTo(context, left + width * 3 / 5, top + height / 2);
        MoveToEx(context, left + width * 5 / 6, bottom - height / 5, nullptr);
        LineTo(context, left + width * 5 / 6, top + height / 8);
    } else if (icon == kIconDevices) {
        Rectangle(context, left + width / 8, top + height / 7, right - width / 8, bottom - height / 4);
        MoveToEx(context, left + width / 2, bottom - height / 4, nullptr);
        LineTo(context, left + width / 2, bottom - height / 10);
        MoveToEx(context, left + width / 3, bottom - height / 10, nullptr);
        LineTo(context, right - width / 3, bottom - height / 10);
    } else if (icon == kIconKeyboard) {
        RoundRect(context, left, top + height / 5, right, bottom - height / 6, Scale(3), Scale(3));
        for (int row = 0; row < 2; ++row) {
            for (int column = 0; column < 4; ++column) {
                const int x = left + width / 7 + column * width / 5;
                const int y = top + height * 2 / 5 + row * height / 4;
                Rectangle(context, x, y, x + Scale(2), y + Scale(2));
            }
        }
    } else if (icon == kIconMouse) {
        RoundRect(context,
                  left + width / 5,
                  top,
                  right - width / 5,
                  bottom,
                  Scale(9),
                  Scale(9));
        MoveToEx(context, left + width / 2, top, nullptr);
        LineTo(context, left + width / 2, top + height * 2 / 5);
        MoveToEx(context, left + width / 5, top + height * 2 / 5, nullptr);
        LineTo(context, right - width / 5, top + height * 2 / 5);
    } else if (icon == kIconChassis) {
        RoundRect(context, left + width / 4, top, right - width / 4, bottom, Scale(3), Scale(3));
        Ellipse(context,
                left + width * 3 / 8,
                top + height / 5,
                right - width * 3 / 8,
                top + height * 2 / 5);
        MoveToEx(context, left + width * 3 / 8, bottom - height / 5, nullptr);
        LineTo(context, right - width * 3 / 8, bottom - height / 5);
    } else {
        MoveToEx(context, left + width / 2, top, nullptr);
        LineTo(context, left + width * 3 / 5, top + height * 2 / 5);
        LineTo(context, right, top + height / 2);
        LineTo(context, left + width * 3 / 5, top + height * 3 / 5);
        LineTo(context, left + width / 2, bottom);
        LineTo(context, left + width * 2 / 5, top + height * 3 / 5);
        LineTo(context, left, top + height / 2);
        LineTo(context, left + width * 2 / 5, top + height * 2 / 5);
        LineTo(context, left + width / 2, top);
    }

    SelectObject(context, oldPen);
    SelectObject(context, oldBrush);
    DeleteObject(pen);
}

void TrayApp::AddHitTarget(int id, const RECT& bounds) {
    hitTargets_.push_back({id, bounds});
}

int TrayApp::HitTest(POINT point) const {
    for (auto target = hitTargets_.rbegin(); target != hitTargets_.rend(); ++target) {
        if (Contains(target->bounds, point)) {
            return target->id;
        }
    }
    return 0;
}

void TrayApp::HandleClick(int id) {
    switch (id) {
        case kNavLighting:
            selectedPage_ = 0;
            break;
        case kNavAudio:
            selectedPage_ = 1;
            break;
        case kNavDevices:
            selectedPage_ = 2;
            break;
        case kModeAudio:
            settings_.lightingMode = LightingMode::Audio;
            engine_.SetLightingMode(settings_.lightingMode);
            break;
        case kModeStatic:
            settings_.lightingMode = LightingMode::Static;
            engine_.SetLightingMode(settings_.lightingMode);
            break;
        case kModeBreathing:
            settings_.lightingMode = LightingMode::Breathing;
            engine_.SetLightingMode(settings_.lightingMode);
            break;
        case kModeCycle:
            settings_.lightingMode = LightingMode::ColorCycle;
            engine_.SetLightingMode(settings_.lightingMode);
            break;
        case kPrimaryColor:
            PickColor(true);
            break;
        case kSecondaryColor:
            PickColor(false);
            break;
        case kDirectionForward:
            settings_.reverseDirection = false;
            engine_.SetReverseDirection(false);
            break;
        case kDirectionReverse:
            settings_.reverseDirection = true;
            engine_.SetReverseDirection(true);
            break;
        case kAudioSpectrum:
            settings_.audioColorMode = AudioColorMode::Spectrum;
            engine_.SetAudioColorMode(settings_.audioColorMode);
            break;
        case kAudioGradient:
            settings_.audioColorMode = AudioColorMode::GradientCycle;
            engine_.SetAudioColorMode(settings_.audioColorMode);
            break;
        case kSensitivityLow:
            settings_.sensitivity = Sensitivity::Low;
            engine_.SetSensitivity(settings_.sensitivity);
            break;
        case kSensitivityNormal:
            settings_.sensitivity = Sensitivity::Normal;
            engine_.SetSensitivity(settings_.sensitivity);
            break;
        case kSensitivityHigh:
            settings_.sensitivity = Sensitivity::High;
            engine_.SetSensitivity(settings_.sensitivity);
            break;
        case kEnableAudio:
            settings_.lightingMode = LightingMode::Audio;
            engine_.SetLightingMode(settings_.lightingMode);
            break;
        case kReconnect:
            engine_.RequestReconnect();
            break;
        case kOpenDynamicLighting:
            ShellExecuteW(window_, L"open", L"ms-settings:personalization-lighting", nullptr, nullptr,
                          SW_SHOWNORMAL);
            break;
        case kOpenLog:
            ShellExecuteW(window_,
                          L"open",
                          Logger::Instance().Path().c_str(),
                          nullptr,
                          nullptr,
                          SW_SHOWNORMAL);
            break;
        case kThemeDark:
        case kThemeLight: {
            const ThemeMode theme = id == kThemeDark ? ThemeMode::Dark : ThemeMode::Light;
            if (settings_.themeMode != theme) {
                settings_.themeMode = theme;
                settings_.Save();
                RefreshPalette();
                ApplyWindowTheme();
                DestroyBackBuffer();
            }
            break;
        }
        case kStartupToggle:
            SetStartupEnabled(!IsStartupEnabled());
            break;
        case kPause:
            engine_.TogglePaused();
            break;
        case kOpenKeyboardDriver:
            OpenDeviceDriver(L"https://hid.irok.cn");
            break;
        case kOpenMouseDriver:
            OpenDeviceDriver(L"https://ammaster.angrymiao.com/mouse");
            break;
        case kExit:
            exiting_ = true;
            DestroyWindow(window_);
            break;
        default:
            break;
    }
    if (window_) {
        InvalidateRect(window_, nullptr, FALSE);
    }
}

void TrayApp::OpenDeviceDriver(const wchar_t* url) {
    const bool wasPaused = engine_.IsPaused();
    engine_.SetPaused(true);
    const auto result = reinterpret_cast<INT_PTR>(
        ShellExecuteW(window_, L"open", url, nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) {
        engine_.SetPaused(wasPaused);
        Logger::Instance().Error(L"Could not open device driver");
        MessageBoxW(window_,
                    L"无法打开设备驱动网页。",
                    L"LightController",
                    MB_OK | MB_ICONERROR);
    }
}

void TrayApp::UpdateSlider(int id, int x) {
    const auto target = std::find_if(hitTargets_.begin(), hitTargets_.end(),
                                     [id](const HitTarget& value) { return value.id == id; });
    if (target == hitTargets_.end()) {
        return;
    }
    const int radius = Scale(7);
    const int left = target->bounds.left + radius;
    const int right = target->bounds.right - radius;
    const float amount = static_cast<float>(std::clamp(x, left, right) - left) /
                         static_cast<float>(std::max(1, right - left));
    if (id == kBrightnessSlider) {
        const int value = 10 + static_cast<int>(std::lround(amount * 90.0F));
        if (value != settings_.maxBrightness) {
            settings_.maxBrightness = value;
            engine_.SetMaxBrightness(value);
        }
    } else if (id == kSpeedSlider) {
        const int value = 1 + static_cast<int>(std::lround(amount * 99.0F));
        if (value != settings_.effectSpeed) {
            settings_.effectSpeed = value;
            engine_.SetEffectSpeed(value);
        }
    }
    InvalidateRect(window_, nullptr, FALSE);
}

void TrayApp::PickColor(bool primary) {
    const RgbColor current = primary ? engine_.GetPrimaryColor() : engine_.GetSecondaryColor();
    CHOOSECOLORW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window_;
    dialog.rgbResult = ToColorRef(current);
    dialog.lpCustColors = customColors_.data();
    dialog.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (!ChooseColorW(&dialog)) {
        return;
    }
    const RgbColor selected = FromColorRef(dialog.rgbResult);
    if (primary) {
        settings_.primaryColor = selected;
        engine_.SetPrimaryColor(selected);
    } else {
        settings_.secondaryColor = selected;
        engine_.SetSecondaryColor(selected);
    }
}

void TrayApp::StartMouseTracking() {
    if (trackingMouse_) {
        return;
    }
    TRACKMOUSEEVENT tracking{};
    tracking.cbSize = sizeof(tracking);
    tracking.dwFlags = TME_LEAVE;
    tracking.hwndTrack = window_;
    trackingMouse_ = TrackMouseEvent(&tracking) != FALSE;
}

void TrayApp::AddTrayIcon() {
    trayIcon_ = {};
    trayIcon_.cbSize = sizeof(trayIcon_);
    trayIcon_.hWnd = window_;
    trayIcon_.uID = 1;
    trayIcon_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    trayIcon_.uCallbackMessage = kTrayMessage;
    trayIcon_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(trayIcon_.szTip, L"LightController 正在启动");
    Shell_NotifyIconW(NIM_ADD, &trayIcon_);
    trayIcon_.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &trayIcon_);
}

void TrayApp::RemoveTrayIcon() {
    if (trayIcon_.hWnd) {
        Shell_NotifyIconW(NIM_DELETE, &trayIcon_);
        trayIcon_.hWnd = nullptr;
    }
}

void TrayApp::UpdateTrayIcon(bool showBalloon) {
    if (!trayIcon_.hWnd) {
        return;
    }
    const std::wstring tooltip = Tooltip();
    trayIcon_.uFlags = NIF_TIP;
    wcsncpy_s(trayIcon_.szTip, tooltip.c_str(), _TRUNCATE);
    if (showBalloon) {
        trayIcon_.uFlags |= NIF_INFO;
        wcscpy_s(trayIcon_.szInfoTitle, L"LightController");
        wcscpy_s(trayIcon_.szInfo, L"灯光同步已在后台启动。");
        trayIcon_.dwInfoFlags = NIIF_INFO;
    }
    Shell_NotifyIconW(NIM_MODIFY, &trayIcon_);
}

void TrayApp::ShowMenu(POINT position) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kCommandOpen, L"打开 LightController");
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, StatusText().c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCommandPause, engine_.IsPaused() ? L"继续同步" : L"暂停同步");
    AppendMenuW(menu, MF_STRING, kCommandReconnect, L"重新连接设备");
    AppendMenuW(menu,
                MF_STRING | (IsStartupEnabled() ? MF_CHECKED : 0),
                kCommandStartup,
                L"随 Windows 启动");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCommandDynamicLightingSettings, L"动态光效设置");
    AppendMenuW(menu, MF_STRING, kCommandOpenLog, L"打开日志");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCommandExit, L"退出");

    SetForegroundWindow(window_);
    TrackPopupMenu(menu,
                   TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
                   position.x,
                   position.y,
                   0,
                   window_,
                   nullptr);
    PostMessageW(window_, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

void TrayApp::HandleTrayCommand(UINT command) {
    switch (command) {
        case kCommandOpen:
            ShowMainWindow();
            break;
        case kCommandPause:
            engine_.TogglePaused();
            break;
        case kCommandReconnect:
            engine_.RequestReconnect();
            break;
        case kCommandStartup:
            SetStartupEnabled(!IsStartupEnabled());
            break;
        case kCommandDynamicLightingSettings:
            ShellExecuteW(window_, L"open", L"ms-settings:personalization-lighting", nullptr, nullptr,
                          SW_SHOWNORMAL);
            break;
        case kCommandOpenLog:
            ShellExecuteW(window_,
                          L"open",
                          Logger::Instance().Path().c_str(),
                          nullptr,
                          nullptr,
                          SW_SHOWNORMAL);
            break;
        case kCommandExit:
            exiting_ = true;
            DestroyWindow(window_);
            break;
        default:
            break;
    }
    if (window_) {
        InvalidateRect(window_, nullptr, FALSE);
    }
}

std::wstring TrayApp::Tooltip() const {
    const EngineStatus status = engine_.Status();
    if (status.paused) {
        return L"LightController | 已暂停";
    }
    return L"LightController | 音频 " + std::wstring(status.audioReady ? L"正常" : L"等待") +
           L" | 键盘 " + (status.keyboardReady ? L"正常" : L"等待") + L" | 机箱 " +
           (status.dynamicLightingAvailable > 0 ? L"正常" : L"等待");
}

std::wstring TrayApp::StatusText() const {
    const EngineStatus status = engine_.Status();
    if (!status.running) {
        return L"正在连接设备";
    }
    if (status.paused) {
        return L"同步已暂停";
    }
    const bool chassisReady = status.dynamicLightingAvailable > 0;
    return L"音频 " + std::wstring(status.audioReady ? L"正常" : L"等待") + L"  |  键盘 " +
           (status.keyboardReady ? L"正常" : L"等待") + L"  |  机箱 " +
           (chassisReady ? L"正常" : L"等待");
}

int TrayApp::Scale(int value) const {
    return MulDiv(value, static_cast<int>(dpi_), 96);
}

}  // namespace lightctrl
