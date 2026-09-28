// Copyright (C) 2026 Mannai
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Starts Canon EOS live view to the PC, measures frame size and rate, saves sample frames.
// Usage: liveview.exe [outputDevice=2] [seconds=6] [vfParam=0x00200000]
#include "ptp_parse.h"
#include "wpd_ptp.h"

#include <chrono>
#include <cstdio>
#include <cwchar>
#include <set>
#include <thread>

namespace {

enum : uint16_t {
    kOpSetDevicePropValueEx = 0x9110,
    kOpSetRemoteMode = 0x9114,
    kOpSetEventMode = 0x9115,
    kOpGetEvent = 0x9116,
    kOpKeepDeviceOn = 0x911D,
    kOpGetViewFinderData = 0x9153,
    kRespOk = 0x2001,
    kRespBusy = 0x2019,
    kRespNotReady = 0xA102,
};
constexpr uint16_t kPropEvfOutputDevice = 0xD1B0;
constexpr uint16_t kPropEvfMode = 0xD1B1;

using Clock = std::chrono::steady_clock;

PtpResult SetProp(WpdPtp& ptp, uint16_t prop, uint32_t value) {
    std::vector<uint8_t> payload(12);
    uint32_t words[3] = {12, prop, value};
    memcpy(payload.data(), words, sizeof(words));
    return ptp.Write(kOpSetDevicePropValueEx, {}, payload);
}

// Walks EOS GetEvent records and prints property changes relevant to live view.
void DumpEvents(const std::vector<uint8_t>& data, bool verbose) {
    PtpReader p(data);
    while (p.remaining() >= 8) {
        size_t start = p.pos();
        uint32_t size = p.get<uint32_t>();
        uint32_t type = p.get<uint32_t>();
        if (size < 8 || type == 0) break;
        if (type == 0xC189 && size >= 16) {  // PropValueChanged
            uint32_t prop = p.get<uint32_t>();
            uint32_t value = size >= 20 ? p.get<uint32_t>() : 0;
            if (verbose || (prop >= 0xD1B0 && prop <= 0xD1CF))
                printf("    prop 0x%04X = 0x%X\n", prop, value);
        }
        p.seek(start + size);
    }
}

bool JpegSize(const uint8_t* j, size_t n, int& w, int& h) {
    if (n < 4 || j[0] != 0xFF || j[1] != 0xD8) return false;
    size_t i = 2;
    while (i + 9 < n) {
        if (j[i] != 0xFF) return false;
        uint8_t m = j[i + 1];
        size_t len = (j[i + 2] << 8) | j[i + 3];
        if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
            h = (j[i + 5] << 8) | j[i + 6];
            w = (j[i + 7] << 8) | j[i + 8];
            return true;
        }
        i += 2 + len;
    }
    return false;
}

struct Frame {
    const uint8_t* jpeg = nullptr;
    size_t size = 0;
    uint32_t type = 0;
};

// Finds the image block in a GetViewFinderData buffer; also reports block types seen.
Frame FindJpeg(const std::vector<uint8_t>& d, std::set<uint32_t>& types) {
    Frame f;
    size_t pos = 0;
    while (pos + 8 <= d.size()) {
        uint32_t len, type;
        memcpy(&len, &d[pos], 4);
        memcpy(&type, &d[pos + 4], 4);
        if (len < 8 || pos + len > d.size()) break;
        types.insert(type);
        const uint8_t* payload = &d[pos + 8];
        if (!f.jpeg && len > 10 && payload[0] == 0xFF && payload[1] == 0xD8) {
            f.jpeg = payload;
            f.size = len - 8;
            f.type = type;
        }
        pos += len;
    }
    return f;
}

uint64_t Hash(const uint8_t* p, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    uint32_t outDev = argc > 1 ? wcstoul(argv[1], nullptr, 0) : 2;
    int seconds = argc > 2 ? _wtoi(argv[2]) : 6;
    uint32_t vfParam = argc > 3 ? wcstoul(argv[3], nullptr, 0) : 0x00200000;

    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 1;
    WpdPtp ptp;
    bool opened = false;
    for (auto& d : WpdPtp::Enumerate()) {
        if (d.manufacturer.find(L"Canon") == std::wstring::npos) continue;
        HRESULT hr = ptp.Open(d.id);
        printf("open %ls: 0x%08lX\n", d.name.c_str(), hr);
        opened = SUCCEEDED(hr);
        break;
    }
    if (!opened) { printf("no Canon camera\n"); return 1; }

    auto show = [](const char* what, const PtpResult& r) {
        printf("%-28s hr=0x%08lX resp=0x%04X\n", what, r.hr, r.code);
        return r.ok();
    };

    show("SetRemoteMode(1)", ptp.Command(kOpSetRemoteMode, {1}));
    show("SetEventMode(1)", ptp.Command(kOpSetEventMode, {1}));
    for (int i = 0; i < 3; ++i) {
        PtpResult ev = ptp.Read(kOpGetEvent);
        printf("GetEvent: resp=0x%04X bytes=%zu\n", ev.code, ev.data.size());
        DumpEvents(ev.data, false);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    PtpResult r = SetProp(ptp, kPropEvfMode, 1);
    if (r.code == kRespBusy) printf("EvfMode busy (normal in movie mode)\n");
    show("Set EVFMode=1", r);
    show("Set EVFOutputDevice", SetProp(ptp, kPropEvfOutputDevice, outDev));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    std::set<uint32_t> blockTypes;
    std::set<uint64_t> unique;
    int frames = 0, busy = 0, errors = 0, saved = 0, w = 0, h = 0;
    size_t bytes = 0;
    double callMs = 0;
    auto start = Clock::now(), lastKeep = start;
    while (Clock::now() - start < std::chrono::seconds(seconds)) {
        if (Clock::now() - lastKeep > std::chrono::seconds(8)) {
            ptp.Command(kOpKeepDeviceOn);
            lastKeep = Clock::now();
        }
        ptp.Read(kOpGetEvent);
        auto t0 = Clock::now();
        PtpResult vf = ptp.Read(kOpGetViewFinderData, {vfParam, 0, 0});
        callMs += std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        if (vf.code == kRespBusy || vf.code == kRespNotReady) {
            ++busy;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        if (!vf.ok()) {
            if (++errors <= 3) printf("GetViewFinderData hr=0x%08lX resp=0x%04X\n", vf.hr, vf.code);
            if (errors > 20) break;
            continue;
        }
        Frame f = FindJpeg(vf.data, blockTypes);
        if (!f.jpeg) continue;
        ++frames;
        bytes += f.size;
        unique.insert(Hash(f.jpeg, f.size));
        if (!w) JpegSize(f.jpeg, f.size, w, h);
        if (saved < 3 && frames % 10 == 1) {
            char name[64];
            sprintf_s(name, "bin\\frame_dev%u_%d.jpg", outDev, saved++);
            FILE* fp = nullptr;
            if (!fopen_s(&fp, name, "wb")) { fwrite(f.jpeg, 1, f.size, fp); fclose(fp); }
        }
    }
    double secs = std::chrono::duration<double>(Clock::now() - start).count();
    int calls = frames + busy + errors;

    printf("\n=== outputDevice=%u vfParam=0x%X ===\n", outDev, vfParam);
    printf("resolution     %dx%d\n", w, h);
    printf("frames         %d in %.1fs = %.1f fps (%zu unique = %.1f fps)\n", frames, secs,
           frames / secs, unique.size(), unique.size() / secs);
    printf("avg jpeg       %.0f KB\n", frames ? bytes / 1024.0 / frames : 0.0);
    printf("avg call       %.1f ms (%d busy, %d errors)\n", calls ? callMs / calls : 0.0, busy, errors);
    printf("block types   ");
    for (uint32_t t : blockTypes) printf(" %u", t);
    printf("\n\n");

    show("Restore EVFOutputDevice=1", SetProp(ptp, kPropEvfOutputDevice, 1));
    show("SetEventMode(0)", ptp.Command(kOpSetEventMode, {0}));
    show("SetRemoteMode(0)", ptp.Command(kOpSetRemoteMode, {0}));
    ptp.Close();
    CoUninitialize();
    return frames ? 0 : 2;
}
