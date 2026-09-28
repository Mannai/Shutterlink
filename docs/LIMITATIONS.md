# Known limitations

## Picture

- **USB live view is 1024×576.** That is what the camera sends over USB. The 1280×720 and 1920×1080 formats are
  scaled up from it with high-quality cubic scaling, so they are smooth but hold no more detail. For true 1080p or 4K,
  use the camera's HDMI output with a capture card.
- **Frame rate follows the camera's movie setting.** 60 fps needs 59.94p selected on the camera; 30 fps is what you get
  at 29.97p. A USB 3 link is recommended.

## Sound

- **No microphone over USB.** Canon cameras do not send audio over USB. Use a separate microphone.

## Controls

- **Camera settings appear only while an app is using Shutterlink**, because that is when Shutterlink is connected to
  the camera.
- **What can be changed depends on the camera's mode.** In automatic exposure modes the camera fixes ISO, shutter speed
  and aperture; switch movie exposure to M to control them.
- **Focus point accuracy.** Clicks are mapped assuming the 16:9 live view is the full sensor width. On the EOS R6 the
  camera placed its AF frame within about 1% of the clicked point.

## Platform

- **Windows 11 only.** Shutterlink uses the virtual camera API that Windows 10 does not have.
- **Not code-signed.** SmartScreen may warn on first run.
- **Installing needs administrator rights**, because the camera is registered for all users of the PC.
- **One program at a time.** Canon EOS Utility, Canon's webcam software and Shutterlink cannot control the camera
  simultaneously.

## Camera compatibility

| Camera | Status |
|---|---|
| EOS R6 | Tested: 1024×576 at 60 fps (59.94p, USB 3), white balance, exposure compensation, AF method, autofocus, focus point |
| Other EOS R bodies (R, RP, R5, R6 Mark II, R7, R8, R10, R50, …) | Untested — likely to work |
| EOS DSLRs with live view over USB | Untested — may work; live view size and rate may differ |
| PowerShot, EOS M | Untested — Canon uses a different remote mode on some of these; may not work |

Tried Shutterlink with another camera? Please [open an issue](https://github.com/Mannai/Shutterlink/issues) with the model and what worked — the
diagnostic tools described in [DEVELOPMENT.md](DEVELOPMENT.md#diagnostic-tools) make the report easy.
