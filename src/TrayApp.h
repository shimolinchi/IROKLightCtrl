#pragma once

#include "Settings.h"
#include "SyncEngine.h"

#include <Shellapi.h>

#include <array>

namespace lightctrl {

class TrayApp final {
public:
    explicit TrayApp(HINSTANCE instance);
    ~TrayApp();

    int Run(bool showWindow);

private:
    struct UiPalette {
        COLORREF backgroundTop{};
        COLORREF backgroundBottom{};
        COLORREF navigation{};
        COLORREF workspace{};
        COLORREF panel{};
        COLORREF surface{};
        COLORREF surfaceAlt{};
        COLORREF surfaceHover{};
        COLORREF border{};
        COLORREF text{};
        COLORREF hint{};
        COLORREF disabledText{};
        COLORREF line{};
        COLORREF accentSoft{};
        COLORREF accentSoftText{};
        COLORREF note{};
        COLORREF noteText{};
        COLORREF selectedNav{};
        COLORREF navText{};
        COLORREF navIcon{};
        COLORREF track{};
        COLORREF trackDisabled{};
        COLORREF toggleOff{};
        COLORREF toggleOffHover{};
        COLORREF knob{};
        COLORREF caption{};
        COLORREF captionText{};
        COLORREF captionBorder{};
    };

    struct HitTarget {
        int id{};
        RECT bounds{};
    };

    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    bool CreateMainWindow();
    void CreateFonts();
    void DestroyFonts();
    void ShowMainWindow();
    void PaintWindow();
    void EnsureBackBuffer(HDC target, int width, int height);
    void DestroyBackBuffer();

    void DrawInterface(HDC context, const RECT& client);
    void DrawHeader(HDC context, const RECT& client);
    void DrawNavigation(HDC context, const RECT& bounds);
    void DrawLightingPage(HDC context, const RECT& workspace, const RECT& panel);
    void DrawAudioPage(HDC context, const RECT& workspace, const RECT& panel);
    void DrawDevicesPage(HDC context, const RECT& workspace, const RECT& panel);
    void DrawKeyboard(HDC context, const RECT& bounds);
    void DrawAudioVisualizer(HDC context, const RECT& bounds);
    void DrawDeviceCard(HDC context,
                        const RECT& bounds,
                        const std::wstring& name,
                        const std::wstring& detail,
                        bool ready,
                        int icon,
                        int targetId = 0,
                        const std::wstring& action = L"");

    void FillRounded(HDC context,
                     const RECT& bounds,
                     COLORREF fill,
                     int radius,
                     std::optional<COLORREF> border = std::nullopt);
    void DrawLabel(HDC context,
                   const std::wstring& text,
                   RECT bounds,
                   HFONT font,
                   COLORREF color,
                   UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    void DrawButton(HDC context,
                    int id,
                    const RECT& bounds,
                    const std::wstring& text,
                    bool selected = false,
                    bool enabled = true);
    void DrawSlider(HDC context,
                    int id,
                    const RECT& bounds,
                    int value,
                    int minimum,
                    int maximum,
                    bool enabled = true);
    void DrawToggle(HDC context, int id, const RECT& bounds, bool checked);
    void DrawColorSwatch(HDC context,
                         int id,
                         const RECT& bounds,
                         RgbColor color,
                         bool enabled = true);
    void DrawSeparator(HDC context, int left, int right, int y);
    void DrawStatusDot(HDC context, POINT center, bool ready);
    void DrawIcon(HDC context, int icon, const RECT& bounds, COLORREF color);

    void AddHitTarget(int id, const RECT& bounds);
    [[nodiscard]] int HitTest(POINT point) const;
    void HandleClick(int id);
    void UpdateSlider(int id, int x);
    void PickColor(bool primary);
    void OpenDeviceDriver(const wchar_t* url);
    void StartMouseTracking();
    void RefreshPalette();
    void ApplyWindowTheme();

    void AddTrayIcon();
    void RemoveTrayIcon();
    void UpdateTrayIcon(bool showBalloon = false);
    void ShowMenu(POINT position);
    void HandleTrayCommand(UINT command);

    [[nodiscard]] int Scale(int value) const;
    [[nodiscard]] std::wstring Tooltip() const;
    [[nodiscard]] std::wstring StatusText() const;

    HINSTANCE instance_{};
    HWND window_{};
    NOTIFYICONDATAW trayIcon_{};
    Settings settings_;
    SyncEngine engine_;
    UiPalette palette_{};
    UINT dpi_{96};
    int selectedPage_{};
    int hoveredTarget_{};
    int activeSlider_{};
    int timerTicks_{};
    bool trackingMouse_{};
    bool exiting_{};

    HFONT brandFont_{};
    HFONT titleFont_{};
    HFONT headingFont_{};
    HFONT bodyFont_{};
    HFONT smallFont_{};
    HFONT keyFont_{};

    HDC backBufferDc_{};
    HBITMAP backBufferBitmap_{};
    HGDIOBJ backBufferOldBitmap_{};
    SIZE backBufferSize_{};

    std::vector<HitTarget> hitTargets_;
    std::array<COLORREF, 16> customColors_{};
};

}  // namespace lightctrl
