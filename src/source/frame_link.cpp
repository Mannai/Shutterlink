// Copyright (C) 2026 Mannai
// SPDX-License-Identifier: GPL-3.0-or-later

#include "frame_link.h"

#include <sddl.h>

#include <cstring>
#include <initializer_list>

namespace {

void FillBlack(uint8_t* nv12, UINT32 width, UINT32 height) {
    size_t luma = static_cast<size_t>(width) * height;
    memset(nv12, 16, luma);
    memset(nv12 + luma, 128, luma / 2);
}

}  // namespace

FrameLink::~FrameLink() { Close(); }

HRESULT FrameLink::Open() {
    if (header_) return S_OK;

    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(kSharedSddl, SDDL_REVISION_1, &sd,
                                                              nullptr))
        return HRESULT_FROM_WIN32(GetLastError());
    SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};

    mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0,
                                  static_cast<DWORD>(kSharedSize), kSharedMappingName);
    HRESULT hr = mapping_ ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    bool created = mapping_ && GetLastError() != ERROR_ALREADY_EXISTS;
    if (SUCCEEDED(hr)) {
        mutex_ = CreateMutexW(&sa, FALSE, kSharedMutexName);
        event_ = CreateEventW(&sa, FALSE, FALSE, kSharedFrameEventName);
        if (!mutex_ || !event_) hr = HRESULT_FROM_WIN32(GetLastError());
    }
    LocalFree(sd);

    if (SUCCEEDED(hr)) {
        header_ = static_cast<SharedFrameHeader*>(
            MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, kSharedSize));
        if (!header_) hr = HRESULT_FROM_WIN32(GetLastError());
    }
    if (FAILED(hr)) {
        Close();
        return hr;
    }
    if (created) {
        memset(header_, 0, sizeof(SharedFrameHeader));
        header_->headerSize = sizeof(SharedFrameHeader);
        header_->magic = kSharedMagic;
    }
    Heartbeat();
    return S_OK;
}

void FrameLink::Close() {
    if (header_) UnmapViewOfFile(header_);
    header_ = nullptr;
    for (HANDLE* h : {&mapping_, &mutex_, &event_}) {
        if (*h) CloseHandle(*h);
        *h = nullptr;
    }
}

void FrameLink::Heartbeat() {
    if (header_) InterlockedExchange64(&header_->sourceHeartbeat, static_cast<LONG64>(GetTickCount64()));
}

void FrameLink::Request(UINT32 width, UINT32 height) {
    if (!header_) return;
    InterlockedExchange(&header_->requestedWidth, static_cast<LONG>(width));
    InterlockedExchange(&header_->requestedHeight, static_cast<LONG>(height));
    Heartbeat();
}

bool FrameLink::AppStreaming() const {
    if (!header_ || header_->status != static_cast<LONG>(CameraStatus::Streaming)) return false;
    return GetTickCount64() - static_cast<ULONGLONG>(header_->appHeartbeat) < kHeartbeatTimeoutMs;
}

void FrameLink::Read(uint8_t* out, UINT32 width, UINT32 height, DWORD waitMs) {
    const size_t bytes = static_cast<size_t>(width) * height * 3 / 2;
    Heartbeat();

    if (AppStreaming()) {
        if (header_->frameSeq == lastSeq_) WaitForSingleObject(event_, waitMs);
        DWORD w = WaitForSingleObject(mutex_, 100);
        if (w == WAIT_OBJECT_0 || w == WAIT_ABANDONED) {
            if (header_->frameSeq != lastSeq_ && header_->width == static_cast<LONG>(width) &&
                header_->height == static_cast<LONG>(height)) {
                last_.assign(SharedFrameData(header_), SharedFrameData(header_) + bytes);
                lastWidth_ = width;
                lastHeight_ = height;
                lastSeq_ = header_->frameSeq;
            }
            ReleaseMutex(mutex_);
        }
    } else {
        last_.clear();  // don't freeze on a stale frame once the camera is gone
        Sleep(waitMs);  // keep a steady cadence while waiting for the camera
    }

    if (lastWidth_ == width && lastHeight_ == height && last_.size() == bytes)
        memcpy(out, last_.data(), bytes);
    else
        FillBlack(out, width, height);
}
