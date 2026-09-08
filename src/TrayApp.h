#pragma once

#include "Settings.h"
#include "SyncEngine.h"

#include <Shellapi.h>

namespace als {

class TrayApp final {
public:
    explicit TrayApp(HINSTANCE instance);
    ~TrayApp();

    int Run();

private:
    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    bool CreateHiddenWindow();
    void AddTrayIcon();
    void RemoveTrayIcon();
    void UpdateTrayIcon(bool showBalloon = false);
    void ShowMenu(POINT position);
    void HandleCommand(UINT command);
    std::wstring Tooltip() const;
    std::wstring StatusText() const;

    HINSTANCE instance_{};
    HWND window_{};
    NOTIFYICONDATAW icon_{};
    Settings settings_;
    SyncEngine engine_;
};

}  // namespace als
