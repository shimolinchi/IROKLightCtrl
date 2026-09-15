# LightController

LightController is a small, driverless Windows control panel that keeps an
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

The executable is written to `build\Release\LightController.exe`.

## Use

Start `LightController.exe` to open the control panel. It provides live device
status, lighting modes, brightness, speed, direction, two selectable colors,
audio sensitivity, and either full-spectrum or custom color-range audio
mapping. Closing the window keeps synchronization running in the notification
area; right-click the tray icon to reopen it or exit.

Available software lighting modes are Audio, Static, Breathing, and Color
Cycle. Changes are applied live to the keyboard and chassis and saved
automatically. Enabling startup launches the app with `--background`, so no
window appears at sign-in.

Audio mode keeps only a very faint keyboard and chassis glow while playback is
silent, with continuously interpolated color and brightness transitions. The
non-streaming receiver turns fully off after silence and updates only on distinct
audio pulses, reducing effect-reload flicker during steady audio.

On the Devices page, the IROK keyboard and AM Infinity mouse cards open their
official WebHID control panels. LightController pauses its own lighting output
first so the vendor driver can adjust settings without competing HID writes.

Settings and logs are stored in `%LOCALAPPDATA%\LightController`. The interface
theme defaults to dark and can be switched between dark and light on the Devices page.
The keyboard and receiver lighting modes are restored when the app exits normally.

Run `scripts\install-ambient.ps1` once after building to register the sparse
MSIX identity required by Windows for background lighting control. Keep Dynamic
Lighting enabled, then place LightController first under Settings > Personalization
> Dynamic Lighting > Background light control. LightController does not call the
ASUS Aura SDK or Armoury Crate; all effects and color frames originate in this app.

## Diagnostics

Generate a read-only JSON report:

```powershell
.\scripts\diagnose.ps1
```

Cycle the keyboard and available Windows Dynamic Lighting chassis through test
colors for six seconds:

```powershell
.\build\Release\LightController.exe --self-test 6
```

Test only the AM INFINITY 8K receiver RGB and restore its previous effect:

```powershell
.\build\Release\LightController.exe --receiver-self-test 4
```

## License

[MIT](LICENSE)
