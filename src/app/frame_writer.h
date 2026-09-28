// Copyright (C) 2026 Mannai
// SPDX-License-Identifier: GPL-3.0-or-later
//
// App-side end of the shared-memory frame hand-off (see common/shared_frame.h).
#pragma once

#include <windows.h>

#include <cstdint>

#include "../common/shared_frame.h"

class FrameWriter {
public:
    ~FrameWriter() { Close(); }

    bool Open();  // succeeds only while the virtual camera is loaded by the Frame Server
    void Close();
    bool IsOpen() const { return header_ != nullptr; }

    bool SourceActive() const;  // a client is pulling frames
    void SetStatus(CameraStatus status);
    bool Requested(UINT& width, UINT& height) const;
    uint8_t* Scratch(UINT width, UINT height);  // not shared; convert into this, then Publish
    void Publish(UINT width, UINT height);

private:
    HANDLE mapping_ = nullptr;
    HANDLE mutex_ = nullptr;
    HANDLE event_ = nullptr;
    SharedFrameHeader* header_ = nullptr;
    uint8_t* scratch_ = nullptr;
};
