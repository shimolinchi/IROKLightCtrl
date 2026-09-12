# IROKLightCtrl

IROKLightCtrl is a small, driverless Windows control panel that keeps an
IROK MG75 PRO keyboard, an Angry Miao AM INFINITY 8K receiver, and compatible
PC chassis lighting in sync with system audio. It captures the default
multimedia playback endpoint through WASAPI loopback, analyzes bass, mids, and
treble with an FFT, then sends the same color frame to every enabled device.

No kernel driver or always-open command window is required.

## Hardware support

- IROK MG75 PRO (`VID_1CA5`, `PID_0807`) through its vendor HID interface
- Angry Miao AM INFINITY 8K receiver (`VID_3151`, `PID_5007`) through its
  vendor HID interface
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

Start `IROKLightCtrl.exe` to open the control panel. It provides live device
status, lighting modes, brightness, speed, direction, two selectable colors,
audio sensitivity, and either full-spectrum or custom color-range audio
mapping. Closing the window keeps synchronization running in the notification
area; right-click the tray icon to reopen it or exit.

Available software lighting modes are Audio, Static, Breathing, and Color
Cycle. Changes are applied live to the keyboard and chassis and saved
automatically. Enabling startup launches the app with `--background`, so no
window appears at sign-in.

Audio mode keeps a dim idle color when playback is silent, then raises the
brightness from the captured audio level.

Settings and logs are stored in `%LOCALAPPDATA%\IROKLightCtrl`. The original
keyboard and receiver lighting modes are restored when the app exits normally.

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

Test only the AM INFINITY 8K receiver RGB and restore its previous effect:

```powershell
.\build\Release\IROKLightCtrl.exe --receiver-self-test 4
```

## License

[MIT](LICENSE)
