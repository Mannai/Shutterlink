// Copyright (C) 2026 Mannai
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Raw PTP transport over Windows Portable Devices (WPD MTP extension commands).
// WPD owns the PTP session; we only issue operations inside it.
#pragma once

#include <windows.h>
#include <PortableDeviceApi.h>
#include <PortableDevice.h>
#include <WpdMtpExtensions.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

struct PtpResult {
    HRESULT hr = E_FAIL;
    uint16_t code = 0;              // PTP response code (0x2001 = OK)
    std::vector<uint32_t> params;   // response parameters
    std::vector<uint8_t> data;      // data-in phase payload
    bool ok() const { return SUCCEEDED(hr) && code == 0x2001; }
};

struct WpdDeviceInfo {
    std::wstring id;
    std::wstring name;
    std::wstring manufacturer;
};

class WpdPtp {
public:
    static std::vector<WpdDeviceInfo> Enumerate();

    HRESULT Open(const std::wstring& deviceId);
    void Close();

    // Operation with no data phase.
    PtpResult Command(uint16_t op, std::initializer_list<uint32_t> params = {});
    // Operation with a data-in phase (camera -> host).
    PtpResult Read(uint16_t op, std::initializer_list<uint32_t> params = {});
    // Operation with a data-out phase (host -> camera).
    PtpResult Write(uint16_t op, std::initializer_list<uint32_t> params,
                    const std::vector<uint8_t>& payload);

private:
    HRESULT MakeParams(REFPROPERTYKEY command, uint16_t op,
                       std::initializer_list<uint32_t> params,
                       ComPtr<IPortableDeviceValues>& out);
    static void ReadResponse(IPortableDeviceValues* results, PtpResult& r);
    HRESULT EndTransfer(LPCWSTR context, PtpResult& r);

    ComPtr<IPortableDevice> device_;
};
