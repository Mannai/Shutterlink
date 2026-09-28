// Copyright (C) 2026 Mannai
// SPDX-License-Identifier: GPL-3.0-or-later

#include "wpd_ptp.h"

#include <algorithm>

namespace {

constexpr DWORD kChunk = 1024 * 1024;

HRESULT CommandHResult(IPortableDeviceValues* results) {
    HRESULT hr = S_OK;
    if (FAILED(results->GetErrorValue(WPD_PROPERTY_COMMON_HRESULT, &hr))) return S_OK;
    return hr;
}

}  // namespace

std::vector<WpdDeviceInfo> WpdPtp::Enumerate() {
    std::vector<WpdDeviceInfo> out;
    ComPtr<IPortableDeviceManager> mgr;
    if (FAILED(CoCreateInstance(CLSID_PortableDeviceManager, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&mgr))))
        return out;
    mgr->RefreshDeviceList();

    DWORD count = 0;
    if (FAILED(mgr->GetDevices(nullptr, &count)) || count == 0) return out;
    std::vector<PWSTR> ids(count);
    if (FAILED(mgr->GetDevices(ids.data(), &count))) return out;

    auto query = [&](PWSTR id, auto fn) {
        DWORD len = 0;
        (mgr.Get()->*fn)(id, nullptr, &len);
        std::wstring s(len, L'\0');
        if (len && SUCCEEDED((mgr.Get()->*fn)(id, s.data(), &len))) s.resize(wcslen(s.c_str()));
        else s.clear();
        return s;
    };

    for (DWORD i = 0; i < count; ++i) {
        WpdDeviceInfo info;
        info.id = ids[i];
        info.name = query(ids[i], &IPortableDeviceManager::GetDeviceFriendlyName);
        info.manufacturer = query(ids[i], &IPortableDeviceManager::GetDeviceManufacturer);
        out.push_back(std::move(info));
        CoTaskMemFree(ids[i]);
    }
    return out;
}

HRESULT WpdPtp::Open(const std::wstring& deviceId) {
    ComPtr<IPortableDeviceValues> client;
    HRESULT hr = CoCreateInstance(CLSID_PortableDeviceValues, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&client));
    if (FAILED(hr)) return hr;
    client->SetStringValue(WPD_CLIENT_NAME, L"Shutterlink");
    client->SetUnsignedIntegerValue(WPD_CLIENT_MAJOR_VERSION, 0);
    client->SetUnsignedIntegerValue(WPD_CLIENT_MINOR_VERSION, 1);
    client->SetUnsignedIntegerValue(WPD_CLIENT_REVISION, 0);
    client->SetUnsignedIntegerValue(WPD_CLIENT_SECURITY_QUALITY_OF_SERVICE, SECURITY_IMPERSONATION);
    client->SetUnsignedIntegerValue(WPD_CLIENT_DESIRED_ACCESS, GENERIC_READ | GENERIC_WRITE);

    hr = CoCreateInstance(CLSID_PortableDeviceFTM, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&device_));
    if (FAILED(hr)) return hr;
    hr = device_->Open(deviceId.c_str(), client.Get());
    if (FAILED(hr)) device_.Reset();
    return hr;
}

void WpdPtp::Close() {
    if (device_) device_->Close();
    device_.Reset();
}

HRESULT WpdPtp::MakeParams(REFPROPERTYKEY command, uint16_t op,
                           std::initializer_list<uint32_t> params,
                           ComPtr<IPortableDeviceValues>& out) {
    HRESULT hr = CoCreateInstance(CLSID_PortableDeviceValues, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&out));
    if (FAILED(hr)) return hr;
    ComPtr<IPortableDevicePropVariantCollection> coll;
    hr = CoCreateInstance(CLSID_PortableDevicePropVariantCollection, nullptr,
                          CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&coll));
    if (FAILED(hr)) return hr;
    for (uint32_t p : params) {
        PROPVARIANT pv;
        PropVariantInit(&pv);
        pv.vt = VT_UI4;
        pv.ulVal = p;
        coll->Add(&pv);
    }
    out->SetGuidValue(WPD_PROPERTY_COMMON_COMMAND_CATEGORY, command.fmtid);
    out->SetUnsignedIntegerValue(WPD_PROPERTY_COMMON_COMMAND_ID, command.pid);
    out->SetUnsignedIntegerValue(WPD_PROPERTY_MTP_EXT_OPERATION_CODE, op);
    return out->SetIPortableDevicePropVariantCollectionValue(WPD_PROPERTY_MTP_EXT_OPERATION_PARAMS,
                                                             coll.Get());
}

void WpdPtp::ReadResponse(IPortableDeviceValues* results, PtpResult& r) {
    ULONG code = 0;
    if (SUCCEEDED(results->GetUnsignedIntegerValue(WPD_PROPERTY_MTP_EXT_RESPONSE_CODE, &code)))
        r.code = static_cast<uint16_t>(code);
    ComPtr<IPortableDevicePropVariantCollection> coll;
    if (SUCCEEDED(results->GetIPortableDevicePropVariantCollectionValue(
            WPD_PROPERTY_MTP_EXT_RESPONSE_PARAMS, &coll))) {
        DWORD n = 0;
        coll->GetCount(&n);
        for (DWORD i = 0; i < n; ++i) {
            PROPVARIANT pv;
            PropVariantInit(&pv);
            if (SUCCEEDED(coll->GetAt(i, &pv))) r.params.push_back(pv.ulVal);
            PropVariantClear(&pv);
        }
    }
}

PtpResult WpdPtp::Command(uint16_t op, std::initializer_list<uint32_t> params) {
    PtpResult r;
    ComPtr<IPortableDeviceValues> in, results;
    r.hr = MakeParams(WPD_COMMAND_MTP_EXT_EXECUTE_COMMAND_WITHOUT_DATA_PHASE, op, params, in);
    if (FAILED(r.hr)) return r;
    r.hr = device_->SendCommand(0, in.Get(), &results);
    if (SUCCEEDED(r.hr)) r.hr = CommandHResult(results.Get());
    if (SUCCEEDED(r.hr)) ReadResponse(results.Get(), r);
    return r;
}

HRESULT WpdPtp::EndTransfer(LPCWSTR context, PtpResult& r) {
    ComPtr<IPortableDeviceValues> in, results;
    HRESULT hr = CoCreateInstance(CLSID_PortableDeviceValues, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&in));
    if (FAILED(hr)) return hr;
    in->SetGuidValue(WPD_PROPERTY_COMMON_COMMAND_CATEGORY,
                     WPD_COMMAND_MTP_EXT_END_DATA_TRANSFER.fmtid);
    in->SetUnsignedIntegerValue(WPD_PROPERTY_COMMON_COMMAND_ID,
                                WPD_COMMAND_MTP_EXT_END_DATA_TRANSFER.pid);
    in->SetStringValue(WPD_PROPERTY_MTP_EXT_TRANSFER_CONTEXT, context);
    hr = device_->SendCommand(0, in.Get(), &results);
    if (SUCCEEDED(hr)) hr = CommandHResult(results.Get());
    if (SUCCEEDED(hr)) ReadResponse(results.Get(), r);
    return hr;
}

PtpResult WpdPtp::Read(uint16_t op, std::initializer_list<uint32_t> params) {
    PtpResult r;
    ComPtr<IPortableDeviceValues> in, results;
    r.hr = MakeParams(WPD_COMMAND_MTP_EXT_EXECUTE_COMMAND_WITH_DATA_TO_READ, op, params, in);
    if (FAILED(r.hr)) return r;
    r.hr = device_->SendCommand(0, in.Get(), &results);
    if (SUCCEEDED(r.hr)) r.hr = CommandHResult(results.Get());
    if (FAILED(r.hr)) return r;

    PWSTR context = nullptr;
    ULONGLONG total = 0;
    r.hr = results->GetStringValue(WPD_PROPERTY_MTP_EXT_TRANSFER_CONTEXT, &context);
    if (FAILED(r.hr)) return r;
    results->GetUnsignedLargeIntegerValue(WPD_PROPERTY_MTP_EXT_TRANSFER_TOTAL_DATA_SIZE, &total);

    r.data.reserve(static_cast<size_t>(total));
    std::vector<uint8_t> buf(kChunk);
    while (r.data.size() < total) {
        DWORD want = static_cast<DWORD>(std::min<ULONGLONG>(kChunk, total - r.data.size()));
        ComPtr<IPortableDeviceValues> rin, rout;
        r.hr = CoCreateInstance(CLSID_PortableDeviceValues, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&rin));
        if (FAILED(r.hr)) break;
        rin->SetGuidValue(WPD_PROPERTY_COMMON_COMMAND_CATEGORY, WPD_COMMAND_MTP_EXT_READ_DATA.fmtid);
        rin->SetUnsignedIntegerValue(WPD_PROPERTY_COMMON_COMMAND_ID, WPD_COMMAND_MTP_EXT_READ_DATA.pid);
        rin->SetStringValue(WPD_PROPERTY_MTP_EXT_TRANSFER_CONTEXT, context);
        rin->SetUnsignedLargeIntegerValue(WPD_PROPERTY_MTP_EXT_TRANSFER_NUM_BYTES_TO_READ, want);
        rin->SetBufferValue(WPD_PROPERTY_MTP_EXT_TRANSFER_DATA, buf.data(), want);
        r.hr = device_->SendCommand(0, rin.Get(), &rout);
        if (SUCCEEDED(r.hr)) r.hr = CommandHResult(rout.Get());
        if (FAILED(r.hr)) break;

        BYTE* got = nullptr;
        DWORD gotLen = 0;
        ULONGLONG nread = 0;
        rout->GetUnsignedLargeIntegerValue(WPD_PROPERTY_MTP_EXT_TRANSFER_NUM_BYTES_READ, &nread);
        if (SUCCEEDED(rout->GetBufferValue(WPD_PROPERTY_MTP_EXT_TRANSFER_DATA, &got, &gotLen))) {
            DWORD n = static_cast<DWORD>(std::min<ULONGLONG>(nread ? nread : gotLen, gotLen));
            r.data.insert(r.data.end(), got, got + n);
            CoTaskMemFree(got);
            if (n == 0) break;
        } else {
            break;
        }
    }

    HRESULT endHr = EndTransfer(context, r);
    if (SUCCEEDED(r.hr)) r.hr = endHr;
    CoTaskMemFree(context);
    return r;
}

PtpResult WpdPtp::Write(uint16_t op, std::initializer_list<uint32_t> params,
                        const std::vector<uint8_t>& payload) {
    PtpResult r;
    ComPtr<IPortableDeviceValues> in, results;
    r.hr = MakeParams(WPD_COMMAND_MTP_EXT_EXECUTE_COMMAND_WITH_DATA_TO_WRITE, op, params, in);
    if (FAILED(r.hr)) return r;
    in->SetUnsignedLargeIntegerValue(WPD_PROPERTY_MTP_EXT_TRANSFER_TOTAL_DATA_SIZE, payload.size());
    r.hr = device_->SendCommand(0, in.Get(), &results);
    if (SUCCEEDED(r.hr)) r.hr = CommandHResult(results.Get());
    if (FAILED(r.hr)) return r;

    PWSTR context = nullptr;
    r.hr = results->GetStringValue(WPD_PROPERTY_MTP_EXT_TRANSFER_CONTEXT, &context);
    if (FAILED(r.hr)) return r;

    ComPtr<IPortableDeviceValues> win, wout;
    r.hr = CoCreateInstance(CLSID_PortableDeviceValues, nullptr, CLSCTX_INPROC_SERVER,
                            IID_PPV_ARGS(&win));
    if (SUCCEEDED(r.hr)) {
        win->SetGuidValue(WPD_PROPERTY_COMMON_COMMAND_CATEGORY, WPD_COMMAND_MTP_EXT_WRITE_DATA.fmtid);
        win->SetUnsignedIntegerValue(WPD_PROPERTY_COMMON_COMMAND_ID, WPD_COMMAND_MTP_EXT_WRITE_DATA.pid);
        win->SetStringValue(WPD_PROPERTY_MTP_EXT_TRANSFER_CONTEXT, context);
        win->SetUnsignedLargeIntegerValue(WPD_PROPERTY_MTP_EXT_TRANSFER_NUM_BYTES_TO_WRITE,
                                          payload.size());
        win->SetBufferValue(WPD_PROPERTY_MTP_EXT_TRANSFER_DATA,
                            const_cast<BYTE*>(payload.data()), static_cast<DWORD>(payload.size()));
        r.hr = device_->SendCommand(0, win.Get(), &wout);
        if (SUCCEEDED(r.hr)) r.hr = CommandHResult(wout.Get());
    }

    HRESULT endHr = EndTransfer(context, r);
    if (SUCCEEDED(r.hr)) r.hr = endHr;
    CoTaskMemFree(context);
    return r;
}
