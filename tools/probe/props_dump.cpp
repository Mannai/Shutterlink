// Copyright (C) 2026 Mannai
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Dumps every EOS property value and "available values" list the camera reports.
#include "ptp_parse.h"
#include "wpd_ptp.h"

#include <cstdio>
#include <cstring>

int wmain() {
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 1;
    WpdPtp ptp;
    bool opened = false;
    for (auto& d : WpdPtp::Enumerate())
        if (d.manufacturer.find(L"Canon") != std::wstring::npos) { opened = SUCCEEDED(ptp.Open(d.id)); break; }
    if (!opened) { printf("no camera\n"); return 1; }

    ptp.Command(0x9114, {1});
    ptp.Command(0x9115, {1});
    for (int round = 0; round < 3; ++round) {
        PtpResult ev = ptp.Read(0x9116);
        printf("-- GetEvent hr=0x%08lX resp=0x%04X bytes=%zu\n", ev.hr, ev.code, ev.data.size());
        const auto& d = ev.data;
        size_t pos = 0;
        while (pos + 8 <= d.size()) {
            uint32_t size, type;
            memcpy(&size, &d[pos], 4);
            memcpy(&type, &d[pos + 4], 4);
            if (size < 8 || type == 0 || pos + size > d.size()) break;
            const uint8_t* p = &d[pos + 8];
            uint32_t n = size - 8;
            if (type == 0xC189 && n >= 4) {
                uint32_t prop;
                memcpy(&prop, p, 4);
                printf("VAL  0x%04X [%u bytes]:", prop, n - 4);
                for (uint32_t i = 4; i < n && i < 36; i += 4) {
                    uint32_t v = 0;
                    memcpy(&v, p + i, std::min<uint32_t>(4, n - i));
                    printf(" 0x%X", v);
                }
                printf("\n");
            } else if (type == 0xC18A && n >= 12) {
                uint32_t prop, dtype, count;
                memcpy(&prop, p, 4);
                memcpy(&dtype, p + 4, 4);
                memcpy(&count, p + 8, 4);
                printf("LIST 0x%04X type=%u count=%u:", prop, dtype, count);
                for (uint32_t i = 0; i < count && 12 + i * 4 + 4 <= n; ++i) {
                    uint32_t v;
                    memcpy(&v, p + 12 + i * 4, 4);
                    printf(" %X", v);
                }
                printf("\n");
            } else {
                printf("EVT  0x%04X [%u bytes]\n", type, n);
            }
            pos += size;
        }
    }
    ptp.Command(0x9115, {0});
    ptp.Command(0x9114, {0});
    ptp.Close();
    CoUninitialize();
    return 0;
}
