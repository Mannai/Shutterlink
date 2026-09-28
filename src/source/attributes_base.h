// Copyright (C) 2026 Mannai
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Forwards every IMFAttributes method to an inner attribute store, for classes whose
// interface derives from IMFAttributes (IMFActivate). Expand inside the class body.
#pragma once

#include <mfobjects.h>

#define FORWARD_IMFATTRIBUTES(inner) \
    IFACEMETHODIMP GetItem(REFGUID k, PROPVARIANT* v) override { return inner->GetItem(k, v); } \
    IFACEMETHODIMP GetItemType(REFGUID k, MF_ATTRIBUTE_TYPE* t) override { return inner->GetItemType(k, t); } \
    IFACEMETHODIMP CompareItem(REFGUID k, REFPROPVARIANT v, BOOL* r) override { return inner->CompareItem(k, v, r); } \
    IFACEMETHODIMP Compare(IMFAttributes* o, MF_ATTRIBUTES_MATCH_TYPE m, BOOL* r) override { return inner->Compare(o, m, r); } \
    IFACEMETHODIMP GetUINT32(REFGUID k, UINT32* v) override { return inner->GetUINT32(k, v); } \
    IFACEMETHODIMP GetUINT64(REFGUID k, UINT64* v) override { return inner->GetUINT64(k, v); } \
    IFACEMETHODIMP GetDouble(REFGUID k, double* v) override { return inner->GetDouble(k, v); } \
    IFACEMETHODIMP GetGUID(REFGUID k, GUID* v) override { return inner->GetGUID(k, v); } \
    IFACEMETHODIMP GetStringLength(REFGUID k, UINT32* n) override { return inner->GetStringLength(k, n); } \
    IFACEMETHODIMP GetString(REFGUID k, LPWSTR s, UINT32 n, UINT32* len) override { return inner->GetString(k, s, n, len); } \
    IFACEMETHODIMP GetAllocatedString(REFGUID k, LPWSTR* s, UINT32* n) override { return inner->GetAllocatedString(k, s, n); } \
    IFACEMETHODIMP GetBlobSize(REFGUID k, UINT32* n) override { return inner->GetBlobSize(k, n); } \
    IFACEMETHODIMP GetBlob(REFGUID k, UINT8* b, UINT32 n, UINT32* len) override { return inner->GetBlob(k, b, n, len); } \
    IFACEMETHODIMP GetAllocatedBlob(REFGUID k, UINT8** b, UINT32* n) override { return inner->GetAllocatedBlob(k, b, n); } \
    IFACEMETHODIMP GetUnknown(REFGUID k, REFIID riid, LPVOID* p) override { return inner->GetUnknown(k, riid, p); } \
    IFACEMETHODIMP SetItem(REFGUID k, REFPROPVARIANT v) override { return inner->SetItem(k, v); } \
    IFACEMETHODIMP DeleteItem(REFGUID k) override { return inner->DeleteItem(k); } \
    IFACEMETHODIMP DeleteAllItems() override { return inner->DeleteAllItems(); } \
    IFACEMETHODIMP SetUINT32(REFGUID k, UINT32 v) override { return inner->SetUINT32(k, v); } \
    IFACEMETHODIMP SetUINT64(REFGUID k, UINT64 v) override { return inner->SetUINT64(k, v); } \
    IFACEMETHODIMP SetDouble(REFGUID k, double v) override { return inner->SetDouble(k, v); } \
    IFACEMETHODIMP SetGUID(REFGUID k, REFGUID v) override { return inner->SetGUID(k, v); } \
    IFACEMETHODIMP SetString(REFGUID k, LPCWSTR v) override { return inner->SetString(k, v); } \
    IFACEMETHODIMP SetBlob(REFGUID k, const UINT8* b, UINT32 n) override { return inner->SetBlob(k, b, n); } \
    IFACEMETHODIMP SetUnknown(REFGUID k, IUnknown* p) override { return inner->SetUnknown(k, p); } \
    IFACEMETHODIMP LockStore() override { return inner->LockStore(); } \
    IFACEMETHODIMP UnlockStore() override { return inner->UnlockStore(); } \
    IFACEMETHODIMP GetCount(UINT32* n) override { return inner->GetCount(n); } \
    IFACEMETHODIMP GetItemByIndex(UINT32 i, GUID* k, PROPVARIANT* v) override { return inner->GetItemByIndex(i, k, v); } \
    IFACEMETHODIMP CopyAllItems(IMFAttributes* d) override { return inner->CopyAllItems(d); } \
    static_assert(true, "")
