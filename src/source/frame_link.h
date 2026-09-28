// Copyright (C) 2026 Mannai
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Source-side end of the shared-memory frame hand-off (see common/shared_frame.h).
#pragma once

#include <windows.h>

#include <cstdint>
#include <vector>

#include "../common/shared_frame.h"

class FrameLink {
public:
    ~FrameLink();

    HRESULT Open();
    void Close();

    // Tells the app which NV12 size to produce.
    void Request(UINT32 width, UINT32 height);

    // Fills `out` (width*height*3/2 NV12) with the newest frame, waiting up to
    // `waitMs` for one newer than the last delivered. Repeats the previous frame,
    // or a black frame when the camera is not streaming.
    void Read(uint8_t* out, UINT32 width, UINT32 height, DWORD waitMs);

private:
    bool AppStreaming() const;
    void Heartbeat();

    HANDLE mapping_ = nullptr;
    HANDLE mutex_ = nullptr;
    HANDLE event_ = nullptr;
    SharedFrameHeader* header_ = nullptr;

    std::vector<uint8_t> last_;
    UINT32 lastWidth_ = 0, lastHeight_ = 0;
    LONG64 lastSeq_ = 0;
};
