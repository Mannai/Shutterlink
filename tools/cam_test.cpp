// Copyright (C) 2026 Mannai
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Opens a camera by name the way apps do (Media Foundation source reader), measures
// fps and saves one frame as BMP. Usage: cam_test.exe [name prefix=Shutterlink] [width=1920] [height=1080] [seconds=6]
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

constexpr DWORD kVideo = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);

static void SaveNv12AsBmp(const BYTE* nv12, UINT w, UINT h, const wchar_t* path) {
    std::vector<BYTE> bgr(static_cast<size_t>(w) * h * 3);
    const BYTE* uv = nv12 + static_cast<size_t>(w) * h;
    for (UINT y = 0; y < h; ++y)
        for (UINT x = 0; x < w; ++x) {
            int Y = nv12[y * w + x] - 16, U = uv[(y / 2) * w + (x & ~1u)] - 128, V = uv[(y / 2) * w + (x & ~1u) + 1] - 128;
            int r = (298 * Y + 459 * V + 128) >> 8, g = (298 * Y - 55 * U - 136 * V + 128) >> 8, b = (298 * Y + 541 * U + 128) >> 8;
            BYTE* p = &bgr[((h - 1 - y) * static_cast<size_t>(w) + x) * 3];
            p[0] = static_cast<BYTE>(b < 0 ? 0 : b > 255 ? 255 : b);
            p[1] = static_cast<BYTE>(g < 0 ? 0 : g > 255 ? 255 : g);
            p[2] = static_cast<BYTE>(r < 0 ? 0 : r > 255 ? 255 : r);
        }
    BITMAPFILEHEADER fh{0x4D42, 0, 0, 0, sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)};
    BITMAPINFOHEADER ih{sizeof(ih), static_cast<LONG>(w), static_cast<LONG>(h), 1, 24};
    fh.bfSize = fh.bfOffBits + static_cast<DWORD>(bgr.size());
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"wb")) return;
    fwrite(&fh, sizeof(fh), 1, f);
    fwrite(&ih, sizeof(ih), 1, f);
    fwrite(bgr.data(), 1, bgr.size(), f);
    fclose(f);
}

int wmain(int argc, wchar_t** argv) {
    std::wstring want = argc > 1 ? argv[1] : L"Shutterlink";
    UINT width = argc > 2 ? _wtoi(argv[2]) : 1920, height = argc > 3 ? _wtoi(argv[3]) : 1080;
    int seconds = argc > 4 ? _wtoi(argv[4]) : 6;

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION);

    ComPtr<IMFAttributes> attrs;
    MFCreateAttributes(&attrs, 1);
    attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    IMFActivate** devices = nullptr;
    UINT32 count = 0;
    HRESULT hr = MFEnumDeviceSources(attrs.Get(), &devices, &count);
    printf("MFEnumDeviceSources hr=0x%08lX, %u camera(s)\n", hr, count);
    ComPtr<IMFActivate> chosen;
    for (UINT32 i = 0; i < count; ++i) {
        WCHAR* name = nullptr;
        UINT32 len = 0;
        devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &len);
        wprintf(L"  [%u] %ls\n", i, name ? name : L"?");
        if (name && std::wstring(name).rfind(want, 0) == 0 && !chosen) chosen = devices[i];
        CoTaskMemFree(name);
    }
    for (UINT32 i = 0; i < count; ++i) devices[i]->Release();
    CoTaskMemFree(devices);
    if (!chosen) { wprintf(L"camera '%ls' not found\n", want.c_str()); return 1; }

    ComPtr<IMFMediaSource> source;
    hr = chosen->ActivateObject(IID_PPV_ARGS(&source));
    printf("ActivateObject hr=0x%08lX\n", hr);
    if (FAILED(hr)) return 1;

    ComPtr<IMFSourceReader> reader;
    hr = MFCreateSourceReaderFromMediaSource(source.Get(), nullptr, &reader);
    printf("CreateSourceReader hr=0x%08lX\n", hr);
    if (FAILED(hr)) return 1;

    // List native types, pick the requested size at the highest frame rate.
    ComPtr<IMFMediaType> pick;
    UINT bestFps = 0;
    for (DWORD i = 0;; ++i) {
        ComPtr<IMFMediaType> t;
        if (FAILED(reader->GetNativeMediaType(kVideo, i, &t))) break;
        UINT32 w = 0, h = 0, n = 0, d = 1;
        MFGetAttributeSize(t.Get(), MF_MT_FRAME_SIZE, &w, &h);
        MFGetAttributeRatio(t.Get(), MF_MT_FRAME_RATE, &n, &d);
        printf("  type %lu: %ux%u @ %u/%u\n", i, w, h, n, d);
        if (w == width && h == height && n / d > bestFps) { bestFps = n / d; pick = t; }
    }
    if (!pick) { printf("no %ux%u type\n", width, height); return 1; }
    hr = reader->SetCurrentMediaType(kVideo, nullptr, pick.Get());
    printf("SetCurrentMediaType %ux%u@%u hr=0x%08lX\n", width, height, bestFps, hr);

    ULONGLONG start = GetTickCount64(), firstLive = 0;
    int samples = 0, live = 0;
    bool saved = false;
    LONGLONG prevTs = 0;
    double maxGapMs = 0;
    while (GetTickCount64() - start < static_cast<ULONGLONG>(seconds) * 1000) {
        DWORD flags = 0;
        LONGLONG ts = 0;
        ComPtr<IMFSample> sample;
        hr = reader->ReadSample(kVideo, 0, nullptr, &flags, &ts, &sample);
        if (FAILED(hr)) { printf("ReadSample hr=0x%08lX\n", hr); break; }
        if (!sample) continue;
        ++samples;
        ComPtr<IMFMediaBuffer> buf;
        sample->ConvertToContiguousBuffer(&buf);
        BYTE* data = nullptr;
        DWORD len = 0;
        buf->Lock(&data, nullptr, &len);
        bool black = data[len / 3] == 16 && data[len / 2] == 16 && data[len / 5] == 16;
        if (!black) {
            if (!live++) firstLive = GetTickCount64();
            if (prevTs) maxGapMs = std::max(maxGapMs, (ts - prevTs) / 10000.0);
            prevTs = ts;
            if (!saved && live > 30) { SaveNv12AsBmp(data, width, height, L"frame.bmp"); saved = true; }
        }
        buf->Unlock();
    }
    double secs = (GetTickCount64() - start) / 1000.0;
    printf("\n%d samples in %.1fs (%.1f/s); %d camera frames", samples, secs, samples / secs, live);
    if (live) printf(" (%.1f fps after first frame at +%.1fs, max gap %.0f ms)",
                     live / ((GetTickCount64() - firstLive) / 1000.0), (firstLive - start) / 1000.0, maxGapMs);
    printf("\nsaved frame.bmp: %s\n", saved ? "yes" : "no");

    source->Shutdown();
    MFShutdown();
    return 0;
}
