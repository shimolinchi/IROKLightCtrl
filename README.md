# IROKLightCtrl

IROKLightCtrl is a small, driverless Windows tray application that keeps an
IROK MG75 PRO keyboard and compatible PC chassis lighting in sync with system
audio. It captures playback through WASAPI loopback, analyzes bass, mids, and
treble with an FFT, then sends the same color frame to every enabled device.

No kernel driver or always-open command window is required.

## Hardware support

- IROK MG75 PRO (`VID_1CA5`, `PID_0807`) through its vendor HID interface
- Chassis devices exposed as `LampArrayKind::Chassis` by Windows Dynamic Lighting
- ASUS Aura SDK as a background-compatible chassis fallback when installed

The IROK HID implementation is based on the public protocol used by IROK's
WebHID configurator. IROK does not officially endorse this project.

## Build

Requirements:

- Windows 11
- Visual Studio 2022 or newer with Desktop development with C++
- Windows 11 SDK with C++/WinRT headers

From PowerShell:

```powershell
.\scripts\build.ps1
```

The executable is written to `build\Release\IROKLightCtrl.exe`.

## Use

Start `IROKLightCtrl.exe`; it runs only in the notification area. Right-click
the tray icon to pause synchronization, reconnect devices, change sensitivity,
enable startup, open Windows Dynamic Lighting settings, or inspect the log.

Settings and logs are stored in `%LOCALAPPDATA%\IROKLightCtrl`. The original
keyboard lighting mode is restored when the app exits normally.

Windows may reserve Dynamic Lighting devices for an app with higher foreground
or background priority. Keep Dynamic Lighting enabled and allow IROKLightCtrl
to control compatible devices. If the ASUS Aura SDK is installed, the app also
initializes it on a worker thread; ASUS enumeration can take about one minute.

## Diagnostics

Generate a read-only JSON report:

```powershell
.\scripts\diagnose.ps1
```

Cycle the keyboard and available Windows Dynamic Lighting chassis through test
colors for six seconds:

```powershell
.\build\Release\IROKLightCtrl.exe --self-test 6
```

## License

[MIT](LICENSE)
