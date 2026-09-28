// Copyright (C) 2026 Mannai
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Lists WPD devices so we can confirm the R6 is visible.
#include "wpd_ptp.h"

#include <cstdio>

int wmain() {
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 1;
    auto devices = WpdPtp::Enumerate();
    wprintf(L"%zu WPD device(s)\n", devices.size());
    for (auto& d : devices)
        wprintf(L"  %ls | %ls\n    %ls\n", d.name.c_str(), d.manufacturer.c_str(), d.id.c_str());
    CoUninitialize();
    return 0;
}
