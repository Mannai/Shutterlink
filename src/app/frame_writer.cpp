// Copyright (C) 2026 Mannai
// SPDX-License-Identifier: GPL-3.0-or-later

#include "frame_writer.h"

#include <cstring>
#include <initializer_list>

bool FrameWriter::Open() {
    if (header_) return true;
    mapping_ = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, kSharedMappingName);
    mutex_ = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, kSharedMutexName);
    event_ = OpenEventW(EVENT_MODIFY_STATE, FALSE, kSharedFrameEventName);
    if (mapping_ && mutex_ && event_)
        header_ = static_cast<SharedFrameHeader*>(
            MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, kSharedSize));
    if (!header_ || header_->magic != kSharedMagic) {
        Close();
        return false;
    }
    if (!scratch_)
        scratch_ = static_cast<uint8_t*>(VirtualAlloc(nullptr, kMaxFrameBytes, MEM_COMMIT | MEM_RESERVE,
                                                      PAGE_READWRITE));
    return scratch_ != nullptr;
}

void FrameWriter::Close() {
    if (header_) {
        InterlockedExchange(&header_->status, static_cast<LONG>(CameraStatus::NoApp));
        UnmapViewOfFile(header_);
    }
    header_ = nullptr;
    for (HANDLE* h : {&mapping_, &mutex_, &event_}) {
        if (*h) CloseHandle(*h);
        *h = nullptr;
    }
    if (scratch_) VirtualFree(scratch_, 0, MEM_RELEASE);
    scratch_ = nullptr;
}

bool FrameWriter::SourceActive() const {
    return header_ &&
           GetTickCount64() - static_cast<ULONGLONG>(header_->sourceHeartbeat) < kHeartbeatTimeoutMs;
}

void FrameWriter::SetStatus(CameraStatus status) {
    if (!header_) return;
    InterlockedExchange(&header_->status, static_cast<LONG>(status));
    InterlockedExchange64(&header_->appHeartbeat, static_cast<LONG64>(GetTickCount64()));
}

bool FrameWriter::Requested(UINT& width, UINT& height) const {
    if (!header_) return false;
    LONG w = header_->requestedWidth, h = header_->requestedHeight;
    if (w < 2 || h < 2 || w > static_cast<LONG>(kMaxWidth) || h > static_cast<LONG>(kMaxHeight) ||
        (w & 1) || (h & 1))
        return false;
    width = static_cast<UINT>(w);
    height = static_cast<UINT>(h);
    return true;
}

uint8_t* FrameWriter::Scratch(UINT width, UINT height) {
    return static_cast<size_t>(width) * height * 3 / 2 <= kMaxFrameBytes ? scratch_ : nullptr;
}

void FrameWriter::Publish(UINT width, UINT height) {
    if (!header_) return;
    DWORD w = WaitForSingleObject(mutex_, 100);
    if (w != WAIT_OBJECT_0 && w != WAIT_ABANDONED) return;
    memcpy(SharedFrameData(header_), scratch_, static_cast<size_t>(width) * height * 3 / 2);
    header_->width = static_cast<LONG>(width);
    header_->height = static_cast<LONG>(height);
    ++header_->frameSeq;
    ReleaseMutex(mutex_);
    SetEvent(event_);
}
