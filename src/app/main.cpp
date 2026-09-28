// Copyright (C) 2026 Mannai
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shutterlink.exe: feeds the Shutterlink virtual camera from a Canon EOS body over USB.
//
//   Shutterlink.exe              run in the tray (shows a notification that it's running)
//   Shutterlink.exe --startup    run in the tray quietly (used at login)
//   Shutterlink.exe --install    copy to Program Files, register the source, create the camera
//   Shutterlink.exe --uninstall  remove the camera, unregister, delete files
#include <windows.h>
#include <mfapi.h>
#include <mfvirtualcamera.h>
#include <shellapi.h>
#include <shlobj.h>

#include <atomic>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include "../common/shared_frame.h"
#include "eos_camera.h"
#include "eos_labels.h"
#include "frame_convert.h"
#include "focus_window.h"
#include "frame_writer.h"
#include "resource.h"
#include "version.h"

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kWindowClass[] = L"ShutterlinkTray";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kSettingsKey[] = L"Software\\Shutterlink";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kShowRunningMessage = WM_APP + 2;
constexpr UINT_PTR kTimerStatus = 1, kTimerPromote = 2;

// ---------------------------------------------------------------- logging

std::mutex g_logLock;
fs::path g_logPath;

void Log(const wchar_t* fmt, ...) {
    wchar_t msg[1024];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(msg, _TRUNCATE, fmt, args);
    va_end(args);
    SYSTEMTIME t;
    GetLocalTime(&t);
    std::lock_guard lk(g_logLock);
    FILE* f = nullptr;
    if (!g_logPath.empty() && !_wfopen_s(&f, g_logPath.c_str(), L"a, ccs=UTF-8")) {
        fwprintf(f, L"%02u:%02u:%02u.%03u %ls\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, msg);
        fclose(f);
    }
}

void InitLog() {
    PWSTR base = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base))) {
        fs::path dir = fs::path(base) / L"Shutterlink";
        std::error_code ec;
        fs::create_directories(dir, ec);
        g_logPath = dir / L"shutterlink.log";
        if (fs::exists(g_logPath, ec) && fs::file_size(g_logPath, ec) > 1'000'000) fs::remove(g_logPath, ec);
    }
    CoTaskMemFree(base);
}

// ---------------------------------------------------------------- settings

DWORD ReadSetting(const wchar_t* name, DWORD fallback) {
    DWORD value = fallback, size = sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, name, RRF_RT_REG_DWORD, nullptr, &value, &size);
    return value;
}

void WriteSetting(const wchar_t* name, DWORD value) {
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, name, REG_DWORD, &value, sizeof(value));
}

// ---------------------------------------------------------------- pipeline

struct CameraCommand {
    enum class Kind { SetProperty, Autofocus, DriveFocus, FocusAt } kind;
    uint16_t prop = 0;
    uint32_t value = 0;
    int32_t step = 0;
    float x = 0, y = 0;  // FocusAt: normalized position in the camera's picture
};

class Pipeline {
public:
    void Start() {
        mirror_ = ReadSetting(L"Mirror", 0) != 0;
        captureThread_ = std::thread([this] { CaptureLoop(); });
        mainThread_ = std::thread([this] { MainLoop(); });
    }

    void Stop() {
        quit_ = true;
        cv_.notify_all();
        if (mainThread_.joinable()) mainThread_.join();
        if (captureThread_.joinable()) captureThread_.join();
    }

    std::wstring Status() {
        std::lock_guard lk(statusLock_);
        return status_;
    }

    bool CameraReady() const { return streaming_; }
    EosProps Props() {
        std::lock_guard lk(propsLock_);
        return props_;
    }

    bool LatestJpeg(std::vector<uint8_t>& out) {
        std::lock_guard lk(latestLock_);
        if (latest_.empty()) return false;
        out = latest_;
        return true;
    }

    void Post(const CameraCommand& c) {
        std::lock_guard lk(commandLock_);
        commands_.push_back(c);
    }

    bool Mirror() const { return mirror_; }
    void SetMirror(bool on) {
        mirror_ = on;
        WriteSetting(L"Mirror", on);
    }

private:
    void SetStatusText(const std::wstring& s) {
        std::lock_guard lk(statusLock_);
        if (s != status_) Log(L"status: %ls", s.c_str());
        status_ = s;
    }

    void LogProps(const EosProps& props) {
        for (uint16_t p : {eos::kExposureMode, eos::kIso, eos::kShutterSpeed, eos::kAperture,
                           eos::kExposureComp, eos::kWhiteBalance, eos::kColorTemperature,
                           eos::kPictureStyle, eos::kFocusMode, eos::kMovieServoAf, eos::kAfMethod}) {
            auto it = props.find(p);
            if (it == props.end()) {
                Log(L"  %ls: not reported", eos::PropTitle(p));
                continue;
            }
            std::wstring allowed;
            for (uint32_t v : it->second.allowed) allowed += L" " + eos::ValueLabel(p, v);
            Log(L"  %ls = %ls  [%zu allowed:%ls]", eos::PropTitle(p),
                it->second.hasValue ? eos::ValueLabel(p, it->second.value).c_str() : L"?",
                it->second.allowed.size(), allowed.c_str());
        }
    }

    // Live view side data (AF frame, coordinate system, ...): logged once per connection.
    void LogBlocks(const std::map<uint32_t, std::vector<uint8_t>>& blocks) {
        for (const auto& [type, data] : blocks) {
            std::wstring hex;
            for (size_t i = 0; i < data.size() && i < 48; ++i) {
                wchar_t b[4];
                swprintf_s(b, L"%02X ", data[i]);
                hex += b;
            }
            Log(L"  viewfinder block %u [%zu bytes]: %ls", type, data.size(), hex.c_str());
        }
    }

    void RunCommands(EosCamera& camera) {
        std::deque<CameraCommand> pending;
        {
            std::lock_guard lk(commandLock_);
            pending.swap(commands_);
        }
        for (const CameraCommand& c : pending) {
            bool ok = false;
            switch (c.kind) {
                case CameraCommand::Kind::SetProperty:
                    ok = camera.SetProperty(c.prop, c.value);
                    Log(L"set %ls = %ls: %ls", eos::PropTitle(c.prop), eos::ValueLabel(c.prop, c.value).c_str(),
                        ok ? L"ok" : L"refused");
                    break;
                case CameraCommand::Kind::Autofocus:
                    ok = camera.Autofocus();
                    afReleaseAt_ = GetTickCount64() + 1500;  // hold the "half-press" while AF runs
                    Log(L"autofocus: %ls", ok ? L"ok" : L"refused");
                    break;
                case CameraCommand::Kind::DriveFocus:
                    ok = camera.DriveFocus(c.step);
                    Log(L"focus drive %d: %ls", c.step, ok ? L"ok" : L"refused");
                    break;
                case CameraCommand::Kind::FocusAt:
                    ok = camera.FocusAt(c.x, c.y);
                    Log(L"focus at (%.2f, %.2f): %ls", c.x, c.y, ok ? L"ok" : L"refused");
                    logBlocksAt_ = GetTickCount64() + 700;  // then log where the AF frame went
                    break;
            }
            if (c.kind == CameraCommand::Kind::SetProperty) {  // what the camera now reports
                EosProps now = camera.Props();
                auto it = now.find(c.prop);
                if (it != now.end() && it->second.hasValue)
                    Log(L"  camera reports %ls = %ls", eos::PropTitle(c.prop),
                        eos::ValueLabel(c.prop, it->second.value).c_str());
            }
        }
        if (afReleaseAt_ && GetTickCount64() >= afReleaseAt_) {
            camera.CancelAutofocus();
            afReleaseAt_ = 0;
        }
    }

    // Owns the camera. Streams only while the virtual camera is in use.
    void CaptureLoop() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        EosCamera camera;
        std::vector<uint8_t> jpeg;
        ULONGLONG lastPropsCopy = 0;
        bool blocksLogged = false;
        while (!quit_) {
            if (!wanted_) {
                if (camera.Connected()) {
                    camera.Disconnect();
                    Log(L"camera released");
                }
                streaming_ = false;
                {
                    std::lock_guard lk(commandLock_);
                    commands_.clear();
                }
                Sleep(200);
                continue;
            }
            if (!camera.Connected()) {
                if (!camera.Connect()) {
                    Sleep(1000);
                    continue;
                }
                Log(L"connected to %ls", camera.Name().c_str());
                LogProps(camera.Props());
                blocksLogged = false;
            }
            RunCommands(camera);
            ULONGLONG now = GetTickCount64();
            if (now - lastPropsCopy > 250) {  // refresh what the menu shows
                std::lock_guard lk(propsLock_);
                props_ = camera.Props();
                lastPropsCopy = now;
            }
            switch (camera.GrabJpeg(jpeg)) {
                case EosCamera::Grab::Frame: {
                    if (!blocksLogged || (logBlocksAt_ && GetTickCount64() >= logBlocksAt_)) {
                        LogBlocks(camera.ViewfinderBlocks());
                        blocksLogged = true;
                        logBlocksAt_ = 0;
                    }
                    {
                        std::lock_guard lk(latestLock_);
                        latest_ = jpeg;
                    }
                    std::lock_guard lk(frameLock_);
                    pending_.swap(jpeg);
                    hasPending_ = true;
                    streaming_ = true;
                    cv_.notify_one();
                    break;
                }
                case EosCamera::Grab::NotReady:
                    Sleep(2);
                    break;
                case EosCamera::Grab::Error:
                    Log(L"camera lost");
                    camera.Disconnect();
                    streaming_ = false;
                    Sleep(1000);
                    break;
            }
        }
        camera.Disconnect();
        CoUninitialize();
    }

    // Owns the shared-memory link: tracks demand and converts frames to NV12.
    void MainLoop() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        FrameWriter writer;
        FrameConverter converter;
        if (FAILED(converter.Init())) Log(L"WIC init failed");
        std::vector<uint8_t> jpeg;
        int frames = 0;
        ULONGLONG fpsStart = GetTickCount64();
        UINT w = 0, h = 0;

        while (!quit_) {
            if (!writer.Open() || !writer.SourceActive()) {
                wanted_ = false;
                if (writer.IsOpen()) writer.SetStatus(CameraStatus::Searching);
                SetStatusText(L"Idle — no app is using the camera");
                Sleep(250);
                continue;
            }
            wanted_ = true;
            writer.SetStatus(streaming_ ? CameraStatus::Streaming : CameraStatus::Searching);
            if (!streaming_) SetStatusText(L"Waiting for the camera — turn it on and connect USB");

            {
                std::unique_lock lk(frameLock_);
                cv_.wait_for(lk, std::chrono::milliseconds(100), [this] { return hasPending_ || quit_.load(); });
                if (!hasPending_) continue;
                jpeg.swap(pending_);
                hasPending_ = false;
            }
            if (!writer.Requested(w, h)) continue;
            uint8_t* out = writer.Scratch(w, h);
            if (!out || FAILED(converter.JpegToNv12(jpeg, w, h, mirror_, out))) continue;
            writer.Publish(w, h);

            ++frames;
            ULONGLONG now = GetTickCount64();
            if (now - fpsStart >= 1000) {
                double fps = frames * 1000.0 / (now - fpsStart);
                frames = 0;
                fpsStart = now;
                wchar_t s[128];
                swprintf_s(s, L"Streaming %ux%u at %.0f fps", w, h, fps);
                SetStatusText(s);
            }
        }
        writer.Close();
        CoUninitialize();
    }

    std::atomic<bool> quit_{false};
    std::atomic<bool> wanted_{false};
    std::atomic<bool> streaming_{false};
    std::atomic<bool> mirror_{false};

    std::mutex frameLock_;
    std::condition_variable cv_;
    std::vector<uint8_t> pending_;
    bool hasPending_ = false;

    std::mutex commandLock_;
    std::deque<CameraCommand> commands_;
    ULONGLONG afReleaseAt_ = 0;
    ULONGLONG logBlocksAt_ = 0;

    std::mutex propsLock_;
    EosProps props_;

    std::mutex latestLock_;
    std::vector<uint8_t> latest_;

    std::mutex statusLock_;
    std::wstring status_ = L"Starting";

    std::thread captureThread_, mainThread_;
};

// ---------------------------------------------------------------- tray

Pipeline* g_pipeline = nullptr;
NOTIFYICONDATAW g_nid{};
bool g_announce = true;

enum MenuId : UINT {
    kMenuExit = 1,
    kMenuMirror,
    kMenuAutofocus,
    kMenuContinuousAf,
    kMenuFocusWindow,
    kMenuFocusBase = 20,      // + step (-3..3) + 3
    kMenuChoiceBase = 1000,   // + index into g_choices
};
std::vector<std::pair<uint16_t, uint32_t>> g_choices;  // menu id -> (property, value)

void ShowBalloon(const wchar_t* title, const wchar_t* text) {
    NOTIFYICONDATAW n = g_nid;
    n.uFlags = NIF_INFO;
    n.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON;
    n.hBalloonIcon = g_nid.hIcon;
    wcscpy_s(n.szInfoTitle, title);
    wcscpy_s(n.szInfo, text);
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

void UpdateTray() {
    std::wstring tip = L"Shutterlink\n" + g_pipeline->Status();
    wcsncpy_s(g_nid.szTip, tip.c_str(), _TRUNCATE);
    g_nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

// Windows 11 hides new tray icons in the overflow; show ours on the taskbar once.
// Later user changes win: we only do this the first time we find our entry.
void PromoteTrayIconOnce() {
    if (ReadSetting(L"TrayPromoted", 0)) return;
    HKEY root;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\NotifyIconSettings", 0,
                      KEY_READ | KEY_SET_VALUE, &root) != ERROR_SUCCESS)
        return;
    wchar_t name[256];
    for (DWORD i = 0;; ++i) {
        DWORD len = ARRAYSIZE(name);
        if (RegEnumKeyExW(root, i, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
        wchar_t path[MAX_PATH] = {};
        DWORD size = sizeof(path);
        if (RegGetValueW(root, name, L"ExecutablePath", RRF_RT_REG_SZ, nullptr, path, &size) != ERROR_SUCCESS)
            continue;
        std::wstring p = path;
        const std::wstring suffix = L"\\shutterlink\\shutterlink.exe";
        for (auto& c : p) c = static_cast<wchar_t>(towlower(c));
        if (p.size() >= suffix.size() && p.compare(p.size() - suffix.size(), suffix.size(), suffix) == 0) {
            DWORD one = 1;
            RegSetKeyValueW(root, name, L"IsPromoted", REG_DWORD, &one, sizeof(one));
            WriteSetting(L"TrayPromoted", 1);
            Log(L"tray icon promoted to the taskbar");
            break;
        }
    }
    RegCloseKey(root);
}

// Adds a submenu listing the camera's allowed values for `prop` (checked = current).
void AddPropertyMenu(HMENU parent, const EosProps& props, uint16_t prop) {
    auto it = props.find(prop);
    const wchar_t* title = eos::PropTitle(prop);
    if (it == props.end() || !it->second.hasValue) return;
    const EosProp& e = it->second;
    std::wstring label = std::wstring(title) + L"\t" + eos::ValueLabel(prop, e.value);
    if (e.allowed.size() < 2) {  // fixed by the camera's current mode
        AppendMenuW(parent, MF_STRING | MF_GRAYED, 0, label.c_str());
        return;
    }
    HMENU sub = CreatePopupMenu();
    int column = 0;
    for (uint32_t v : e.allowed) {
        UINT id = kMenuChoiceBase + static_cast<UINT>(g_choices.size());
        g_choices.emplace_back(prop, v);
        UINT flags = MF_STRING | (v == e.value ? MF_CHECKED : 0);
        if (++column > 20) {  // long lists (shutter speeds) wrap into columns
            flags |= MF_MENUBARBREAK;
            column = 1;
        }
        AppendMenuW(sub, flags, id, eos::ValueLabel(prop, v).c_str());
    }
    AppendMenuW(parent, MF_POPUP, reinterpret_cast<UINT_PTR>(sub), label.c_str());
}

HMENU BuildMenu() {
    g_choices.clear();
    HMENU menu = CreatePopupMenu();
    std::wstring status = g_pipeline->Status();
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, status.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    if (g_pipeline->CameraReady()) {
        EosProps props = g_pipeline->Props();

        AppendMenuW(menu, MF_STRING | MF_DEFAULT, kMenuFocusWindow, L"Focus point…");
        HMENU focus = CreatePopupMenu();
        AppendMenuW(focus, MF_STRING, kMenuAutofocus, L"Autofocus now");
        auto servo = props.find(eos::kMovieServoAf);
        if (servo != props.end() && servo->second.hasValue)
            AppendMenuW(focus, MF_STRING | (servo->second.value ? MF_CHECKED : 0), kMenuContinuousAf,
                        L"Continuous autofocus");
        AppendMenuW(focus, MF_SEPARATOR, 0, nullptr);
        const wchar_t* steps[] = {L"Nearer (large step)", L"Nearer (medium step)", L"Nearer (fine step)", nullptr,
                                  L"Farther (fine step)", L"Farther (medium step)", L"Farther (large step)"};
        for (int s = -3; s <= 3; ++s)
            if (s) AppendMenuW(focus, MF_STRING, kMenuFocusBase + s + 3, steps[s + 3]);
        AppendMenuW(focus, MF_SEPARATOR, 0, nullptr);
        AddPropertyMenu(focus, props, eos::kAfMethod);
        AddPropertyMenu(focus, props, eos::kFocusMode);
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(focus), L"Focus");

        HMENU exposure = CreatePopupMenu();
        AddPropertyMenu(exposure, props, eos::kExposureMode);
        AddPropertyMenu(exposure, props, eos::kIso);
        AddPropertyMenu(exposure, props, eos::kShutterSpeed);
        AddPropertyMenu(exposure, props, eos::kAperture);
        AddPropertyMenu(exposure, props, eos::kExposureComp);
        if (GetMenuItemCount(exposure) == 0)
            AppendMenuW(exposure, MF_STRING | MF_GRAYED, 0, L"Not available in this camera mode");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(exposure), L"Exposure");

        HMENU color = CreatePopupMenu();
        AddPropertyMenu(color, props, eos::kWhiteBalance);
        auto wb = props.find(eos::kWhiteBalance);
        if (wb != props.end() && wb->second.value == 9) AddPropertyMenu(color, props, eos::kColorTemperature);
        AddPropertyMenu(color, props, eos::kPictureStyle);
        if (GetMenuItemCount(color) == 0)
            AppendMenuW(color, MF_STRING | MF_GRAYED, 0, L"Not available in this camera mode");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(color), L"Color");
    } else {
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"Camera settings appear while an app uses the webcam");
    }

    AppendMenuW(menu, MF_STRING | (g_pipeline->Mirror() ? MF_CHECKED : 0), kMenuMirror, L"Mirror image");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuExit, L"Exit");
    return menu;
}

void HandleMenu(HWND hwnd, UINT cmd) {
    if (cmd == kMenuExit) {
        DestroyWindow(hwnd);
    } else if (cmd == kMenuFocusWindow) {
        FocusWindowHost host;
        host.cameraReady = [] { return g_pipeline->CameraReady(); };
        host.latestJpeg = [](std::vector<uint8_t>& out) { return g_pipeline->LatestJpeg(out); };
        host.mirrored = [] { return g_pipeline->Mirror(); };
        host.focusAt = [](float x, float y) {
            g_pipeline->Post({CameraCommand::Kind::FocusAt, 0, 0, 0, x, y});
        };
        host.autofocus = [] { g_pipeline->Post({CameraCommand::Kind::Autofocus}); };
        ShowFocusWindow(GetModuleHandleW(nullptr), host);
    } else if (cmd == kMenuMirror) {
        g_pipeline->SetMirror(!g_pipeline->Mirror());
    } else if (cmd == kMenuAutofocus) {
        g_pipeline->Post({CameraCommand::Kind::Autofocus});
    } else if (cmd == kMenuContinuousAf) {
        EosProps props = g_pipeline->Props();
        auto it = props.find(eos::kMovieServoAf);
        uint32_t on = it != props.end() && it->second.value ? 0 : 1;
        g_pipeline->Post({CameraCommand::Kind::SetProperty, eos::kMovieServoAf, on});
    } else if (cmd >= kMenuFocusBase && cmd <= kMenuFocusBase + 6) {
        g_pipeline->Post({CameraCommand::Kind::DriveFocus, 0, 0, static_cast<int32_t>(cmd - kMenuFocusBase) - 3});
    } else if (cmd >= kMenuChoiceBase && cmd - kMenuChoiceBase < g_choices.size()) {
        auto [prop, value] = g_choices[cmd - kMenuChoiceBase];
        g_pipeline->Post({CameraCommand::Kind::SetProperty, prop, value});
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    static UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    if (msg == taskbarCreated) {  // Explorer restarted: put the icon back
        Shell_NotifyIconW(NIM_ADD, &g_nid);
        return 0;
    }
    switch (msg) {
        case WM_CREATE:
            g_nid.cbSize = sizeof(g_nid);
            g_nid.hWnd = hwnd;
            g_nid.uID = 1;
            g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
            g_nid.uCallbackMessage = kTrayMessage;
            g_nid.hIcon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_SHUTTERLINK),
                                                        IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                                        GetSystemMetrics(SM_CYSMICON), 0));
            wcscpy_s(g_nid.szTip, L"Shutterlink");
            Shell_NotifyIconW(NIM_ADD, &g_nid);
            g_nid.uVersion = NOTIFYICON_VERSION_4;
            Shell_NotifyIconW(NIM_SETVERSION, &g_nid);
            if (g_announce)
                ShowBalloon(L"Shutterlink is running",
                            L"Pick “Shutterlink” as the camera in your app. Click this icon for camera settings.");
            SetTimer(hwnd, kTimerStatus, 1000, nullptr);
            SetTimer(hwnd, kTimerPromote, 3000, nullptr);
            return 0;
        case WM_TIMER:
            if (wp == kTimerPromote) {
                KillTimer(hwnd, kTimerPromote);
                PromoteTrayIconOnce();
            } else {
                UpdateTray();
            }
            return 0;
        case kShowRunningMessage:
            ShowBalloon(L"Shutterlink is already running", L"Click the camera icon in the taskbar for settings.");
            return 0;
        case kTrayMessage:
            switch (LOWORD(lp)) {
                case WM_CONTEXTMENU:
                case NIN_SELECT:
                case NIN_KEYSELECT: {
                    HMENU menu = BuildMenu();
                    POINT pt;
                    GetCursorPos(&pt);
                    SetForegroundWindow(hwnd);
                    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
                    DestroyMenu(menu);
                    PostMessageW(hwnd, WM_NULL, 0, 0);
                    HandleMenu(hwnd, cmd);
                    break;
                }
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            if (g_nid.hIcon) DestroyIcon(g_nid.hIcon);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int RunTray(HINSTANCE instance, bool announce) {
    HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\ShutterlinkApp");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND w = FindWindowW(kWindowClass, nullptr)) {  // really running: point the user at it
            PostMessageW(w, kShowRunningMessage, 0, 0);
            return 0;
        }
        // No window: the other instance is shutting down (e.g. during an update). Wait for it.
        DWORD r = WaitForSingleObject(single, 15000);
        if (r != WAIT_OBJECT_0 && r != WAIT_ABANDONED) return 0;
    }

    Log(L"Shutterlink starting");
    g_announce = announce;
    Pipeline pipeline;
    g_pipeline = &pipeline;
    pipeline.Start();

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_SHUTTERLINK));
    wc.lpszClassName = kWindowClass;
    RegisterClassW(&wc);
    CreateWindowExW(0, kWindowClass, L"Shutterlink", 0, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    pipeline.Stop();
    Log(L"Shutterlink exiting");
    CloseHandle(single);
    return 0;
}

// ---------------------------------------------------------------- install

constexpr wchar_t kUninstallKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Shutterlink";

fs::path InstallDir() {
    PWSTR pf = nullptr;
    SHGetKnownFolderPath(FOLDERID_ProgramFiles, 0, nullptr, &pf);
    fs::path p = fs::path(pf) / L"Shutterlink";
    CoTaskMemFree(pf);
    return p;
}

fs::path SelfPath() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return fs::path(path);
}

bool IsElevated() {
    HANDLE token = nullptr;
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    bool elevated = false;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size))
            elevated = elevation.TokenIsElevated != 0;
        CloseHandle(token);
    }
    return elevated;
}

// Re-runs this exe as administrator (UAC prompt) and waits for it. 1223 = the user said no.
int RunElevated(const wchar_t* args) {
    std::wstring self = SelfPath().wstring();
    SHELLEXECUTEINFOW info{sizeof(info)};
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"runas";
    info.lpFile = self.c_str();
    info.lpParameters = args;
    info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info)) return static_cast<int>(GetLastError());
    WaitForSingleObject(info.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(info.hProcess, &code);
    CloseHandle(info.hProcess);
    return static_cast<int>(code);
}

void StopRunningInstance() {
    for (int i = 0; i < 50; ++i) {
        HWND w = FindWindowW(kWindowClass, nullptr);
        if (!w) return;
        PostMessageW(w, WM_CLOSE, 0, 0);
        Sleep(100);
    }
}

bool ReadFileBytes(const fs::path& path, std::vector<uint8_t>& out) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb")) return false;
    out.clear();
    uint8_t buf[65536];
    for (size_t n; (n = fread(buf, 1, sizeof(buf), f)) > 0;) out.insert(out.end(), buf, buf + n);
    fclose(f);
    return true;
}

// Writes `data` to `to`. A file that is in use (the DLL inside the Frame Server, a running
// exe) is moved aside and deleted at the next restart.
bool WriteReplacing(const fs::path& to, const uint8_t* data, size_t size) {
    std::vector<uint8_t> existing;
    if (ReadFileBytes(to, existing) && existing.size() == size && memcmp(existing.data(), data, size) == 0)
        return true;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(to.parent_path(), ec))  // tidy earlier leftovers
        if (e.path().extension() == L".old") fs::remove(e.path(), ec);
    if (fs::exists(to, ec) && !fs::remove(to, ec)) {
        wchar_t suffix[32];
        swprintf_s(suffix, L".%llu.old", GetTickCount64());
        fs::path old = to;
        old += suffix;
        if (!MoveFileExW(to.c_str(), old.c_str(), 0)) return false;
        MoveFileExW(old.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    }
    FILE* f = nullptr;
    if (_wfopen_s(&f, to.c_str(), L"wb")) return false;
    bool ok = fwrite(data, 1, size, f) == size;
    return fclose(f) == 0 && ok;
}

// The camera source DLL travels inside the exe as a resource.
bool EmbeddedSourceDll(const uint8_t*& data, size_t& size) {
    HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_SOURCE_DLL), RT_RCDATA);
    HGLOBAL mem = res ? LoadResource(nullptr, res) : nullptr;
    data = mem ? static_cast<const uint8_t*>(LockResource(mem)) : nullptr;
    size = res ? SizeofResource(nullptr, res) : 0;
    return data && size;
}

HRESULT CallDllExport(const fs::path& dll, const char* name) {
    HMODULE m = LoadLibraryW(dll.c_str());
    if (!m) return HRESULT_FROM_WIN32(GetLastError());
    auto fn = reinterpret_cast<HRESULT(STDAPICALLTYPE*)()>(GetProcAddress(m, name));
    HRESULT hr = fn ? fn() : E_NOINTERFACE;
    FreeLibrary(m);
    return hr;
}

HRESULT CreateCamera(ComPtr<IMFVirtualCamera>& camera) {
    return MFCreateVirtualCamera(MFVirtualCameraType_SoftwareCameraSource, MFVirtualCameraLifetime_System,
                                 MFVirtualCameraAccess_AllUsers, kShutterlinkFriendlyName, kShutterlinkClsidString,
                                 nullptr, 0, &camera);
}

int Fail(const wchar_t* step, HRESULT hr) {
    wchar_t msg[256];
    swprintf_s(msg, L"%ls failed (0x%08lX).", step, hr);
    Log(L"install: %ls", msg);
    MessageBoxW(nullptr, msg, L"Shutterlink", MB_ICONERROR);
    return 1;
}

void SetString(HKEY key, const wchar_t* name, const std::wstring& value) {
    RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                   static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}

void SetDword(HKEY key, const wchar_t* name, DWORD value) {
    RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
}

// Makes Shutterlink appear in Settings > Apps (and Control Panel), with Uninstall wired up.
void RegisterUninstallEntry(const fs::path& dir, size_t bytes) {
    HKEY key;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, kUninstallKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS)
        return;
    std::wstring exe = L"\"" + (dir / L"Shutterlink.exe").wstring() + L"\"";
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t date[16];
    swprintf_s(date, L"%04u%02u%02u", t.wYear, t.wMonth, t.wDay);
    SetString(key, L"DisplayName", L"Shutterlink");
    SetString(key, L"DisplayVersion", SL_VERSION_WSTR);
    SetString(key, L"Publisher", L"Mannai");
    SetString(key, L"DisplayIcon", (dir / L"Shutterlink.exe").wstring() + L",0");
    SetString(key, L"InstallLocation", dir.wstring());
    SetString(key, L"InstallDate", date);
    SetString(key, L"UninstallString", exe + L" --uninstall");
    SetString(key, L"QuietUninstallString", exe + L" --uninstall --quiet");
    SetString(key, L"URLInfoAbout", L"https://github.com/Mannai/Shutterlink");
    SetString(key, L"HelpLink", L"https://github.com/Mannai/Shutterlink/issues");
    SetDword(key, L"EstimatedSize", static_cast<DWORD>(bytes / 1024));
    SetDword(key, L"NoModify", 1);
    SetDword(key, L"NoRepair", 1);
    RegCloseKey(key);
}

int Install() {
    if (!IsElevated()) return RunElevated(L"--install");
    Log(L"install: starting (version %ls)", SL_VERSION_WSTR);
    StopRunningInstance();
    fs::path dir = InstallDir();
    std::error_code ec;
    fs::create_directories(dir, ec);

    std::vector<uint8_t> exe;
    const uint8_t* dll = nullptr;
    size_t dllSize = 0;
    if (!ReadFileBytes(SelfPath(), exe) || !EmbeddedSourceDll(dll, dllSize))
        return Fail(L"Reading the installer", HRESULT_FROM_WIN32(GetLastError()));
    if (!WriteReplacing(dir / L"Shutterlink.exe", exe.data(), exe.size()) ||
        !WriteReplacing(dir / L"ShutterlinkSource.dll", dll, dllSize))
        return Fail(L"Copying files", HRESULT_FROM_WIN32(GetLastError()));

    HRESULT hr = CallDllExport(dir / L"ShutterlinkSource.dll", "DllRegisterServer");
    if (FAILED(hr)) return Fail(L"Registering the camera source", hr);

    ComPtr<IMFVirtualCamera> camera;
    hr = CreateCamera(camera);
    if (SUCCEEDED(hr)) hr = camera->Start(nullptr);
    if (FAILED(hr)) return Fail(L"Creating the virtual camera", hr);

    HKEY run;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kRunKey, 0, KEY_SET_VALUE, &run) == ERROR_SUCCESS) {
        SetString(run, L"Shutterlink", L"\"" + (dir / L"Shutterlink.exe").wstring() + L"\" --startup");
        RegCloseKey(run);
    }
    RegisterUninstallEntry(dir, exe.size() + dllSize);
    ShellExecuteW(nullptr, nullptr, (dir / L"Shutterlink.exe").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    Log(L"install: done");
    return 0;
}

int Uninstall(bool quiet) {
    if (!IsElevated()) return RunElevated(quiet ? L"--uninstall --quiet" : L"--uninstall");
    Log(L"uninstall: starting");
    StopRunningInstance();
    ComPtr<IMFVirtualCamera> camera;
    HRESULT hr = CreateCamera(camera);
    if (SUCCEEDED(hr)) hr = camera->Remove();
    Log(L"uninstall: remove camera 0x%08lX", hr);

    fs::path dir = InstallDir();
    CallDllExport(dir / L"ShutterlinkSource.dll", "DllUnregisterServer");
    HKEY run;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kRunKey, 0, KEY_SET_VALUE, &run) == ERROR_SUCCESS) {
        RegDeleteValueW(run, L"Shutterlink");
        RegCloseKey(run);
    }
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, kUninstallKey);
    RegDeleteTreeW(HKEY_CURRENT_USER, kSettingsKey);

    // Delete what we can now. Files still in use (this exe, a DLL the camera service holds)
    // go at the next restart, and a helper removes the folder once this process has exited.
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dir, ec))
        if (!fs::remove(e.path(), ec)) MoveFileExW(e.path().c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    if (!fs::remove(dir, ec)) {
        MoveFileExW(dir.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
        std::wstring cmd = L"powershell.exe -NoProfile -NonInteractive -WindowStyle Hidden -Command \"Wait-Process -Id " +
                           std::to_wstring(GetCurrentProcessId()) + L" -ErrorAction SilentlyContinue; Remove-Item -LiteralPath '" +
                           dir.wstring() + L"' -Recurse -Force -ErrorAction SilentlyContinue\"";
        STARTUPINFOW si{sizeof(si)};
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                           &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }
    Log(L"uninstall: done");
    if (!quiet) MessageBoxW(nullptr, L"Shutterlink has been removed from this PC.", L"Shutterlink", MB_ICONINFORMATION);
    return 0;
}

// Double-clicked outside Program Files (e.g. straight from Downloads): offer to install.
int OfferInstall() {
    std::error_code ec;
    bool installed = fs::exists(InstallDir() / L"Shutterlink.exe", ec);
    std::wstring text = installed
        ? std::wstring(L"Update Shutterlink to version ") + SL_VERSION_WSTR + L"?"
        : std::wstring(L"Install Shutterlink ") + SL_VERSION_WSTR +
              L"?\n\nYour Canon camera will appear as a webcam named “Shutterlink” in every app. "
              L"Shutterlink starts with Windows and can be removed any time from Settings ▸ Apps.";
    if (MessageBoxW(nullptr, text.c_str(), L"Shutterlink", MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
    return Install();
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR cmdLine, int) {
    InitLog();
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;
    MFStartup(MF_VERSION);
    std::wstring args = cmdLine ? cmdLine : L"";
    auto has = [&](const wchar_t* flag) { return args.find(flag) != std::wstring::npos; };
    std::error_code ec;
    bool fromInstallDir = fs::equivalent(SelfPath().parent_path(), InstallDir(), ec);
    int rc;
    if (has(L"--uninstall")) rc = Uninstall(has(L"--quiet"));
    else if (has(L"--install")) rc = Install();
    else if (!fromInstallDir) rc = OfferInstall();
    else rc = RunTray(instance, !has(L"--startup"));
    MFShutdown();
    CoUninitialize();
    return rc;
}
