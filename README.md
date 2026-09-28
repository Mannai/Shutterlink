# Shutterlink

Use your Canon EOS camera as a webcam on Windows 11, over a single USB cable.
No drivers to hunt down, no subscription, no capture card.

Shutterlink talks to the camera directly over USB (PTP) and publishes it as a regular
Windows camera, so it shows up in Zoom, Teams, Discord, OBS, browsers and the Windows
Camera app like any other webcam.

> Shutterlink is an independent open-source project. It is not affiliated with, endorsed
> by or sponsored by Canon Inc. Canon and EOS are trademarks of Canon Inc.

## Features

- **60 fps** live video from the camera (set the camera's movie mode to 59.94p and use a USB 3 cable)
- Formats for every app: 1920×1080, 1280×720, 1024×576 (native) and 640×360, at 60 or 30 fps
- **Tray menu with camera controls** (whatever the camera allows in its current mode):
  - Focus: autofocus now, continuous AF on/off, AF method (face + tracking, spot, zone, …),
    manual focus nearer/farther in three step sizes
  - Exposure: ISO, shutter speed, aperture, exposure compensation
  - Color: white balance presets, Kelvin temperature, picture style
  - Mirror image
- **Focus point window**: click a square on a live thumbnail to move the autofocus point there
- The camera is only switched to PC live view while an app is actually using the webcam,
  and it is kept awake (no auto power-off) during calls
- If the camera is off or unplugged, apps get a black picture instead of an error, and it
  reconnects automatically

## Requirements

- Windows 11 (uses the Windows 11 virtual camera API)
- A Canon EOS camera with USB. Developed and tested on the **EOS R6**; other EOS bodies that
  support PC live view will probably work but are untested — reports welcome
- A USB 3 cable is recommended (60 fps was measured on a USB 3 link)
- For long sessions, a mains adapter/dummy battery: live view drains the battery quickly

## Install

1. Download `Shutterlink-<version>-win64.zip` from the [Releases](../../releases) page and unzip it.
2. Right-click a Command Prompt or PowerShell, choose **Run as administrator**, then run:
   ```
   Shutterlink.exe --install
   ```
   This copies Shutterlink to `C:\Program Files\Shutterlink`, registers the camera with
   Windows and starts it at login. A camera icon appears in the taskbar.
3. Connect the camera, turn it on, and pick **Shutterlink** as the camera in your app.

Windows SmartScreen may warn that the app is unrecognized, because the binaries are not
code-signed. Choose **More info → Run anyway**, or build it yourself from source.

### Uninstall

From an administrator prompt:
```
"C:\Program Files\Shutterlink\Shutterlink.exe" --uninstall
```

## Camera setup tips

- Put the camera in **movie mode**. Set movie recording size to **1920×1080 59.94p** for 60 fps.
- Set the movie exposure mode to **M** if you want to control ISO, shutter speed and aperture
  from the tray; in automatic modes the camera doesn't allow changing them.
- Turn off **Auto power off**, or rely on Shutterlink's keep-alive while streaming.
- Close Canon's own EOS Utility / webcam software first; only one program can control the
  camera at a time.

## Limitations

- Over USB the camera sends its live view at **1024×576**. The 720p and 1080p formats are
  high-quality upscales of that picture. True 4K needs the camera's HDMI output and a capture card.
- The camera's microphone is not available over USB; use a separate microphone.

## How it works

```
Canon camera ──USB/PTP──► Shutterlink.exe ──shared memory──► ShutterlinkSource.dll ──► apps
                          (live view JPEG,      (NV12 frames)     (Windows Frame Server
                           decode + scale,                         virtual camera source)
                           camera controls)
```

- `Shutterlink.exe` runs in your session. It sends PTP commands through Windows Portable
  Devices (built into Windows), fetches live view frames, converts them to NV12 at the size the
  app asked for, and hosts the tray menu.
- `ShutterlinkSource.dll` is a Media Foundation media source registered with
  `MFCreateVirtualCamera`. The Windows Camera Frame Server loads it when an app opens the camera.

## Building from source

Requirements: Visual Studio 2022 (or the Build Tools) with the **Desktop development with C++**
workload and a Windows 11 SDK (10.0.22000 or newer).

```powershell
.\build.ps1                     # bin\Shutterlink.exe and bin\ShutterlinkSource.dll
.\build.ps1 -Target tools       # diagnostic tools in bin\tools
.\build.ps1 -Package 0.1.0      # also writes dist\Shutterlink-0.1.0-win64.zip
```

Then run `bin\Shutterlink.exe --install` from an administrator prompt.

### Diagnostic tools

| Tool | Purpose |
| --- | --- |
| `list_devices` | Lists the portable devices Windows sees |
| `device_info` | Dumps the camera's supported PTP operations and properties |
| `liveview` | Measures live view resolution and frame rate, saves sample frames |
| `props_dump` | Dumps camera property values and allowed-value lists |
| `cam_test` | Opens the virtual camera like an app would; reports fps and saves a frame |

A log is written to `%LOCALAPPDATA%\Shutterlink\shutterlink.log`.

## Support

Shutterlink is free and always will be. If it saved you a subscription or a capture card,
you can buy me a coffee on [Ko-fi](https://ko-fi.com/mmannai).

## Credits

Canon's PTP extensions are not publicly documented by Canon. Shutterlink relies on protocol
knowledge published by the [libgphoto2](https://github.com/gphoto/libgphoto2) project and on
Julian Schroden's write-up of
[remote live view on Canon EOS cameras](https://julianschroden.com/post/2023-08-19-remote-live-view-using-ptp-ip-on-canon-eos-cameras/).
No code from those projects is included.

## License

Copyright (C) 2026 Mannai

Shutterlink is free software: you can redistribute it and/or modify it under the terms of the
[GNU General Public License](LICENSE), version 3 or (at your option) any later version.
