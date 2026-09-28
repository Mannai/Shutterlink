// Copyright (C) 2026 Mannai
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ShutterlinkSource.dll: COM server for the Shutterlink virtual camera media source.
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <wrl.h>
#include <wrl/module.h>

#include <string>

#include "../common/shared_frame.h"
#include "attributes_base.h"
#include "media_source.h"

using Microsoft::WRL::ClassicCom;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::InProc;
using Microsoft::WRL::MakeAndInitialize;
using Microsoft::WRL::Module;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;

// The Frame Server creates this and asks it for the media source.
class __declspec(uuid("5941F8D3-1AE9-4AFA-92EE-36EA7D4EF0A2")) Activator
    : public RuntimeClass<RuntimeClassFlags<ClassicCom>,
                          Microsoft::WRL::ChainInterfaces<IMFActivate, IMFAttributes>> {
public:
    HRESULT RuntimeClassInitialize() { return MFCreateAttributes(&attributes_, 4); }

    FORWARD_IMFATTRIBUTES(attributes_);

    IFACEMETHODIMP ActivateObject(REFIID riid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        ComPtr<MediaSource> source;
        HRESULT hr = MakeAndInitialize<MediaSource>(&source, attributes_.Get());
        return SUCCEEDED(hr) ? source.CopyTo(riid, object) : hr;
    }
    IFACEMETHODIMP ShutdownObject() override { return S_OK; }
    IFACEMETHODIMP DetachObject() override { return S_OK; }

private:
    ComPtr<IMFAttributes> attributes_;
};

CoCreatableClass(Activator);

static HMODULE g_module;

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, LPVOID* object) {
    return Module<InProc>::GetModule().GetClassObject(clsid, riid, object);
}

STDAPI DllCanUnloadNow() {
    return Module<InProc>::GetModule().GetObjectCount() == 0 ? S_OK : S_FALSE;
}

namespace {

const std::wstring kClsidKey = std::wstring(L"Software\\Classes\\CLSID\\") + kShutterlinkClsidString;

LSTATUS SetValue(HKEY key, LPCWSTR name, const std::wstring& value) {
    return RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                          static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}

}  // namespace

STDAPI DllRegisterServer() {
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(g_module, path, MAX_PATH)) return HRESULT_FROM_WIN32(GetLastError());

    HKEY clsid = nullptr, inproc = nullptr;
    LSTATUS s = RegCreateKeyExW(HKEY_LOCAL_MACHINE, kClsidKey.c_str(), 0, nullptr, 0, KEY_WRITE,
                                nullptr, &clsid, nullptr);
    if (s == ERROR_SUCCESS) s = SetValue(clsid, nullptr, L"Shutterlink Source");
    if (s == ERROR_SUCCESS)
        s = RegCreateKeyExW(clsid, L"InprocServer32", 0, nullptr, 0, KEY_WRITE, nullptr, &inproc,
                            nullptr);
    if (s == ERROR_SUCCESS) s = SetValue(inproc, nullptr, path);
    if (s == ERROR_SUCCESS) s = SetValue(inproc, L"ThreadingModel", L"Both");
    if (inproc) RegCloseKey(inproc);
    if (clsid) RegCloseKey(clsid);
    return HRESULT_FROM_WIN32(s);
}

STDAPI DllUnregisterServer() {
    LSTATUS s = RegDeleteTreeW(HKEY_LOCAL_MACHINE, kClsidKey.c_str());
    return s == ERROR_FILE_NOT_FOUND ? S_OK : HRESULT_FROM_WIN32(s);
}
