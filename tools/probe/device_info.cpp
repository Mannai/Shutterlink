// Copyright (C) 2026 Mannai
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Dumps the PTP DeviceInfo dataset: supported operations, events and properties.
#include "ptp_parse.h"
#include "wpd_ptp.h"

#include <cstdio>

static void PrintCodes(const char* label, const std::vector<uint16_t>& codes) {
    printf("%s (%zu):", label, codes.size());
    for (size_t i = 0; i < codes.size(); ++i) printf("%s0x%04X", i % 12 ? " " : "\n  ", codes[i]);
    printf("\n");
}

int wmain() {
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 1;
    int rc = 1;
    for (auto& d : WpdPtp::Enumerate()) {
        if (d.manufacturer.find(L"Canon") == std::wstring::npos) continue;
        WpdPtp ptp;
        HRESULT hr = ptp.Open(d.id);
        if (FAILED(hr)) { printf("open failed: 0x%08lX\n", hr); break; }

        PtpResult r = ptp.Read(0x1001);  // GetDeviceInfo
        printf("GetDeviceInfo: hr=0x%08lX resp=0x%04X bytes=%zu\n", r.hr, r.code, r.data.size());
        if (r.ok()) {
            PtpReader p(r.data);
            uint16_t stdVer = p.get<uint16_t>();
            uint32_t vendorId = p.get<uint32_t>();
            uint16_t vendorVer = p.get<uint16_t>();
            std::wstring ext = p.str();
            p.get<uint16_t>();  // functional mode
            auto ops = p.array<uint16_t>();
            auto events = p.array<uint16_t>();
            auto props = p.array<uint16_t>();
            p.array<uint16_t>();  // capture formats
            p.array<uint16_t>();  // image formats
            std::wstring mfr = p.str(), model = p.str(), fw = p.str(), serial = p.str();
            wprintf(L"%ls %ls, firmware %ls, PTP %u.%02u, vendor ext 0x%X v%u \"%ls\"\n",
                    mfr.c_str(), model.c_str(), fw.c_str(), stdVer / 100, stdVer % 100,
                    vendorId, vendorVer, ext.c_str());
            PrintCodes("Operations", ops);
            PrintCodes("Events", events);
            PrintCodes("Properties", props);
            rc = p.ok() ? 0 : 2;
        }
        ptp.Close();
        break;
    }
    CoUninitialize();
    return rc;
}
