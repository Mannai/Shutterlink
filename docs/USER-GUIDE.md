# User guide

- [Camera setup](#camera-setup)
- [Using Shutterlink in an app](#using-shutterlink-in-an-app)
- [The tray menu](#the-tray-menu)
- [Focus point window](#focus-point-window)
- [Troubleshooting](#troubleshooting)

## Camera setup

| Setting (on the camera) | Recommended | Why |
|---|---|---|
| Mode | **Movie** | Gives the 16:9 live view that apps expect |
| Movie recording size | **1920×1080 59.94p** | The live view runs at the movie frame rate: 59.94p gives 60 fps, 29.97p gives 30 fps |
| Movie exposure | **M** if you want manual control | ISO, shutter speed and aperture can only be changed from the PC in M |
| Movie Servo AF | On | Keeps you in focus as you move (also switchable from the tray) |
| Auto power off | Disable | Shutterlink keeps the camera awake while streaming, but this avoids surprises |

**Cable.** Use a USB 3 cable (USB-C to USB-A or USB-C to USB-C rated for 5 Gbps or more) in a USB 3 port. Many phone
cables are USB 2 only.

**Power.** Live view uses a lot of battery. For long calls use Canon's DC coupler (dummy battery) with its mains
adapter — for the EOS R6 that is the DR-E6 with the AC-E6N.

**Other camera software.** Only one program can control the camera at a time. Quit Canon EOS Utility and Canon's
webcam software before using Shutterlink.

## Using Shutterlink in an app

Choose **Shutterlink** in the app's camera or video settings. Windows lists it as
*Shutterlink (Windows Virtual Camera)* in some places.

Shutterlink offers 1920×1080, 1280×720, 1024×576 and 640×360 at 60 or 30 fps; apps pick the one they want. The camera
itself sends 1024×576 over USB — the larger sizes are high-quality upscales of that picture (see
[Limitations](LIMITATIONS.md)).

When no app is using the camera, Shutterlink leaves it alone: the camera's own screen stays on and it can shoot as
normal. As soon as an app opens the Shutterlink camera, live view switches to the PC — usually in under a second.

If the camera is off or disconnected, apps get a black picture rather than an error, and the picture returns by
itself when the camera comes back.

## The tray menu

Click the camera icon in the taskbar. The first line shows the status — for example *Streaming 1920x1080 at 60 fps*.

Camera settings appear while an app is using the camera, because that is when Shutterlink is connected to it. Each
list shows only the values your camera accepts in its current mode, with the current value ticked. Settings that the
current mode fixes (for example ISO in an automatic exposure mode) are shown greyed out with their current value.

| Menu | Contents |
|---|---|
| **Focus point…** | Opens the [focus point window](#focus-point-window) |
| **Focus** | *Autofocus now* · *Continuous autofocus* on/off · focus *Nearer* / *Farther* in fine, medium and large steps · *AF method* (face + tracking, spot, 1-point, zone, …) · *Focus mode* |
| **Exposure** | *Shooting mode* (shown) · *ISO* · *Shutter speed* · *Aperture* · *Exposure compensation* |
| **Color** | *White balance* (auto, daylight, shade, cloudy, tungsten, fluorescent, flash, custom, Kelvin) · *Color temperature* (when white balance is Kelvin) · *Picture style* |
| **Mirror image** | Flips the picture left to right. Remembered between sessions |
| **Exit** | Stops Shutterlink until the next sign-in (apps then see a black picture) |

Settings are changed on the camera itself, so they stay as you left them — also for the camera's own recording.

Tip: for a webcam, **AF method: Face + Tracking** with **Continuous autofocus** on keeps your face sharp as you move.

## Focus point window

**Focus point…** opens a small window that stays on top of other windows. It shows a live preview (about 10 frames a
second) divided into an 8 × 5 grid.

- **Click a square** to move the camera's autofocus point there. The square gets a yellow frame. With continuous
  autofocus on, the camera keeps tracking what you clicked.
- **Autofocus now** focuses once at the current AF point.

The grid follows the *Mirror image* setting, so clicking the left of the picture always means the left of what you
see.

## Troubleshooting

| Problem | What to try |
|---|---|
| Apps don't list Shutterlink | Run `Shutterlink.exe --install` again from an admin terminal, then restart the app |
| Black picture | Check the tray status. *Waiting for the camera* means Shutterlink can't reach it: turn the camera on, check the cable, quit EOS Utility |
| 30 fps instead of 60 | Set movie recording size to 59.94p, and use a USB 3 cable and port |
| ISO / shutter / aperture greyed out | Switch the camera's movie exposure to M |
| Picture freezes after a while | The battery may be flat — use a DC coupler for long sessions |
| The tray icon is hidden | Click **^** in the taskbar, or drag the icon onto the taskbar |

Shutterlink writes a log to `%LOCALAPPDATA%\Shutterlink\shutterlink.log`. Please attach it when you
[report a problem](https://github.com/Mannai/Shutterlink/issues).
