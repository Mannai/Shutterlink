# Development

- [How it works](#how-it-works)
- [Building](#building)
- [Releasing](#releasing)
- [Diagnostic tools](#diagnostic-tools)
- [Camera protocol](#camera-protocol)
- [Source layout](#source-layout)

## How it works

```
Canon camera ──USB/PTP──► Shutterlink.exe ──shared memory──► ShutterlinkSource.dll ──► apps
                          live view JPEG,       NV12 frames       Media Foundation source
                          decode + scale,                         loaded by the Windows
                          camera controls,                        Camera Frame Server
                          tray menu
```

**Shutterlink.exe** runs in the signed-in user's session.

- A capture thread talks to the camera through Windows Portable Devices (WPD), which is built into Windows: the
  WPD MTP driver owns the PTP session and Shutterlink sends Canon's vendor operations through
  `WPD_COMMAND_MTP_EXT_EXECUTE_COMMAND_*`. No driver install and no Canon SDK are needed.
- A conversion thread decodes each live view JPEG with WIC, scales it to the size the app asked for
  (high-quality cubic, centre-crop if the aspect differs), converts it to NV12 (BT.709, limited range) and publishes it.
- The camera is only put into PC live view while the source reports that an app is pulling frames.

**ShutterlinkSource.dll** is a Media Foundation media source (`IMFMediaSourceEx`, one video stream, `IKsControl`
stubbed) exposed through an `IMFActivate`. It is embedded in `Shutterlink.exe` as a resource; `--install` unpacks it to
`C:\Program Files\Shutterlink`, registers it and creates a system-wide virtual
camera with `MFCreateVirtualCamera`. The Frame Server service (running as LocalService in session 0) loads it when an
app opens the camera.

The two halves share a `Global\` file mapping, mutex and event created by the DLL with a DACL that lets the user
session in (see `src/common/shared_frame.h`). The DLL writes the requested frame size and a heartbeat; the app writes
frames and its own heartbeat. Either side treats a heartbeat older than 3 s as gone.

## Building

Requirements:

- Visual Studio 2022 or the Build Tools, with the **Desktop development with C++** workload
- Windows 11 SDK 10.0.22000 or newer (for `mfsensorgroup.lib` / `MFCreateVirtualCamera`)

```powershell
.\build.ps1                     # bin\Shutterlink.exe, with bin\ShutterlinkSource.dll embedded in it
.\build.ps1 -Target tools       # diagnostic tools in bin\tools
.\build.ps1 -Package            # also copies the release exe to dist\Shutterlink.exe
```

The version comes from the `VERSION` file (override with `-Version 1.2.3`) and is written into the exe's file
properties and the Settings ▸ Apps entry. Binaries link the C runtime statically (`/MT`), so they need no Visual C++
redistributable.

To try a build, run `bin\Shutterlink.exe --install` (it asks for elevation itself). Re-running `--install` upgrades in
place: it stops the running instance, and if the Frame Server still has the old DLL loaded, moves it aside and
schedules it for deletion at the next restart. `--install` also registers the uninstall entry under
`HKLM\Software\Microsoft\Windows\CurrentVersion\Uninstall\Shutterlink`, which runs `--uninstall`.

`tools\make_icon.ps1` regenerates `src\app\shutterlink.ico` and `docs\media\icon.png`.

## Releasing

Pushing a tag `v<version>` runs the [build workflow](../.github/workflows/build.yml), which builds `Shutterlink.exe` with
the tag's version and publishes it as a GitHub Release. Every push and pull request is built too, with the exe attached
as a workflow artifact. Bump `VERSION` along with the tag.

## Diagnostic tools

Built with `.\build.ps1 -Target tools` into `bin\tools`. The probe tools talk to the camera directly, so quit
Shutterlink (tray ▸ Exit) first.

| Tool | Purpose |
|---|---|
| `list_devices` | Lists the portable devices Windows sees |
| `device_info` | Dumps the camera's supported PTP operations, events and properties |
| `liveview [device] [seconds] [param]` | Starts PC live view, reports resolution, frame rate and JPEG size, saves sample frames |
| `props_dump` | Dumps every property value and allowed-value list the camera reports |
| `cam_test [name] [width] [height] [seconds]` | Opens the Shutterlink camera the way an app does, reports fps and saves a frame |

## Camera protocol

Canon EOS vendor operations used by Shutterlink (all through WPD passthrough):

| Code | Operation | Use |
|---|---|---|
| `0x9114` | SetRemoteMode | `1` on connect, `0` on disconnect |
| `0x9115` | SetEventMode | `1` on connect, `0` on disconnect |
| `0x9116` | GetEvent | Polled every frame: property values (`0xC189`) and allowed-value lists (`0xC18A`) |
| `0x9110` | SetDevicePropValueEx | Data phase `u32 12, u32 property, u32 value` |
| `0x9153` | GetViewFinderData | Params `0x00200000, 0, 0`; returns blocks of `u32 length, u32 type, payload` |
| `0x911D` | KeepDeviceOn | Every 8 s while streaming |
| `0x9154` / `0x9160` | DoAf / AfCancel | *Autofocus now* (half-press, released after 1.5 s) |
| `0x9155` | DriveLens | `1..3` nearer, `0x8001..0x8003` farther |
| `0x915B` | TouchAfPosition | `3, x, y` — AF point centre in sensor coordinates |

Properties: `0xD1B0` EVF output device (`2` = PC, the largest live view size; `1` restores the camera screen), `0xD1B1`
EVF mode, `0xD101` aperture, `0xD102` shutter speed, `0xD103` ISO, `0xD104` exposure compensation, `0xD105` shooting
mode, `0xD108` focus mode, `0xD109` white balance, `0xD10A` color temperature, `0xD110` picture style, `0xD179` movie
servo AF, `0xD1BA` AF method. Value encodings are in `src/app/eos_labels.cpp`.

Live view blocks: type `1` is the JPEG; type `0x0E` is the AF coordinate space (`u32 width, u32 height` — 5128 × 3420
on the EOS R6); type `8` is the current AF frame.

## Source layout

| Path | Contents |
|---|---|
| `src/app` | `Shutterlink.exe`: camera control (`eos_camera`, `wpd_ptp`), labels, frame conversion, tray, focus window, installer |
| `src/source` | `ShutterlinkSource.dll`: media source, stream, activator, shared-memory reader |
| `src/common` | The shared-memory contract used by both |
| `tools` | Test client, icon generator and camera probe tools |
