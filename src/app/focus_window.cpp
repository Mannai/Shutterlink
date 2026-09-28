// Copyright (C) 2026 Mannai
// SPDX-License-Identifier: GPL-3.0-or-later

#include "focus_window.h"

#include <wincodec.h>
#include <algorithm>
#include <windowsx.h>
#include <wrl/client.h>

#include "resource.h"

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kClass[] = L"ShutterlinkFocus";
constexpr int kCols = 8, kRows = 5;
constexpr int kViewW = 512, kViewH = 288;  // 16:9 thumbnail
constexpr int kBarH = 30;                  // bottom strip: hint + AF button
constexpr UINT_PTR kTimerFrame = 1;
constexpr int kAfButtonId = 100;

FocusWindowHost g_host;
HWND g_hwnd = nullptr;
HWND g_afButton = nullptr;
ComPtr<IWICImagingFactory> g_wic;
std::vector<uint8_t> g_pixels;  // kViewW x kViewH BGRA
bool g_havePixels = false;
int g_selCol = -1, g_selRow = -1;
std::vector<uint8_t> g_jpeg;

bool DecodeThumbnail() {
    if (!g_host.latestJpeg || !g_host.latestJpeg(g_jpeg) || g_jpeg.empty()) return false;
    if (!g_wic && FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                          IID_PPV_ARGS(&g_wic))))
        return false;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICBitmapScaler> scaler;
    ComPtr<IWICFormatConverter> conv;
    if (FAILED(g_wic->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromMemory(g_jpeg.data(), static_cast<DWORD>(g_jpeg.size()))) ||
        FAILED(g_wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)) || FAILED(g_wic->CreateBitmapScaler(&scaler)) ||
        FAILED(scaler->Initialize(frame.Get(), kViewW, kViewH, WICBitmapInterpolationModeLinear)) ||
        FAILED(g_wic->CreateFormatConverter(&conv)) ||
        FAILED(conv->Initialize(scaler.Get(), GUID_WICPixelFormat32bppBGR, WICBitmapDitherTypeNone, nullptr, 0,
                                WICBitmapPaletteTypeCustom)))
        return false;
    g_pixels.resize(static_cast<size_t>(kViewW) * kViewH * 4);
    if (FAILED(conv->CopyPixels(nullptr, kViewW * 4, static_cast<UINT>(g_pixels.size()), g_pixels.data())))
        return false;
    if (g_host.mirrored && g_host.mirrored()) {
        for (int y = 0; y < kViewH; ++y) {
            auto* row = reinterpret_cast<uint32_t*>(&g_pixels[static_cast<size_t>(y) * kViewW * 4]);
            std::reverse(row, row + kViewW);
        }
    }
    return true;
}

void Paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, kViewW, kViewH);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);

    RECT view{0, 0, kViewW, kViewH};
    bool ready = g_host.cameraReady && g_host.cameraReady();
    if (ready && g_havePixels) {
        BITMAPINFO bi{};
        bi.bmiHeader = {sizeof(BITMAPINFOHEADER), kViewW, -kViewH, 1, 32, BI_RGB};
        SetDIBitsToDevice(mem, 0, 0, kViewW, kViewH, 0, 0, 0, kViewH, g_pixels.data(), &bi, DIB_RGB_COLORS);
    } else {
        FillRect(mem, &view, static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH)));
        SetBkMode(mem, TRANSPARENT);
        SetTextColor(mem, RGB(230, 230, 230));
        HGDIOBJ oldFont = SelectObject(mem, GetStockObject(DEFAULT_GUI_FONT));
        DrawTextW(mem, L"Open the webcam in an app to use the focus grid", -1, &view,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(mem, oldFont);
    }

    // Grid lines, drawn twice (dark then light, offset) so they show on any picture.
    HPEN dark = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
    HPEN light = CreatePen(PS_SOLID, 1, RGB(200, 200, 200));
    for (HPEN pen : {dark, light}) {
        HGDIOBJ old = SelectObject(mem, pen);
        int o = pen == dark ? 1 : 0;
        for (int c = 1; c < kCols; ++c) {
            int x = c * kViewW / kCols + o;
            MoveToEx(mem, x, 0, nullptr);
            LineTo(mem, x, kViewH);
        }
        for (int r = 1; r < kRows; ++r) {
            int y = r * kViewH / kRows + o;
            MoveToEx(mem, 0, y, nullptr);
            LineTo(mem, kViewW, y);
        }
        SelectObject(mem, old);
    }
    if (g_selCol >= 0) {  // chosen cell: yellow AF box
        HPEN yellow = CreatePen(PS_SOLID, 3, RGB(255, 210, 0));
        HGDIOBJ oldPen = SelectObject(mem, yellow);
        HGDIOBJ oldBrush = SelectObject(mem, GetStockObject(NULL_BRUSH));
        Rectangle(mem, g_selCol * kViewW / kCols + 2, g_selRow * kViewH / kRows + 2,
                  (g_selCol + 1) * kViewW / kCols - 1, (g_selRow + 1) * kViewH / kRows - 1);
        SelectObject(mem, oldBrush);
        SelectObject(mem, oldPen);
        DeleteObject(yellow);
    }
    DeleteObject(dark);
    DeleteObject(light);

    BitBlt(dc, 0, 0, kViewW, kViewH, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);

    RECT bar{0, kViewH, kViewW, kViewH + kBarH};
    FillRect(dc, &bar, GetSysColorBrush(COLOR_BTNFACE));
    RECT text{8, kViewH, kViewW - 130, kViewH + kBarH};
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
    HGDIOBJ oldFont = SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
    DrawTextW(dc, L"Click a square to focus there", -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, oldFont);
    EndPaint(hwnd, &ps);
}

void OnClick(int x, int y) {
    if (y >= kViewH || !g_host.cameraReady || !g_host.cameraReady()) return;
    g_selCol = x * kCols / kViewW;
    g_selRow = y * kRows / kViewH;
    float fx = (g_selCol + 0.5f) / kCols, fy = (g_selRow + 0.5f) / kRows;
    if (g_host.mirrored && g_host.mirrored()) fx = 1.0f - fx;  // grid shows the mirrored picture
    if (g_host.focusAt) g_host.focusAt(fx, fy);
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            g_afButton = CreateWindowW(L"BUTTON", L"Autofocus now", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       kViewW - 124, kViewH + 3, 118, kBarH - 6, hwnd,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAfButtonId)), nullptr, nullptr);
            SendMessageW(g_afButton, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
            SetTimer(hwnd, kTimerFrame, 100, nullptr);  // ~10 fps thumbnail is plenty
            return 0;
        case WM_TIMER:
            g_havePixels = DecodeThumbnail() || g_havePixels;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_COMMAND:
            if (LOWORD(wp) == kAfButtonId && g_host.autofocus) g_host.autofocus();
            return 0;
        case WM_LBUTTONDOWN:
            OnClick(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            Paint(hwnd);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, kTimerFrame);
            g_hwnd = nullptr;
            g_havePixels = false;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

void ShowFocusWindow(HINSTANCE instance, const FocusWindowHost& host) {
    if (g_hwnd) {
        ShowWindow(g_hwnd, SW_SHOWNORMAL);
        SetForegroundWindow(g_hwnd);
        return;
    }
    g_host = host;
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = Proc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_CROSS);
        wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_SHUTTERLINK));
        wc.lpszClassName = kClass;
        registered = RegisterClassW(&wc) != 0;
    }
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT r{0, 0, kViewW, kViewH + kBarH};
    AdjustWindowRectEx(&r, style, FALSE, WS_EX_TOPMOST);
    g_hwnd = CreateWindowExW(WS_EX_TOPMOST, kClass, L"Shutterlink — Focus point", style, CW_USEDEFAULT,
                             CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr, instance, nullptr);
    ShowWindow(g_hwnd, SW_SHOWNORMAL);
    SetForegroundWindow(g_hwnd);
}
