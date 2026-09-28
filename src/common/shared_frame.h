// Copyright (C) 2026 Mannai
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Frame hand-off between Shutterlink.exe (camera side, user session) and ShutterlinkSource.dll
// (virtual camera, loaded by the Frame Server service in session 0).
//
// The source DLL creates the Global\ objects when a client opens the camera and keeps
// the heartbeat fresh while streaming; the app opens them, streams the camera while the
// heartbeat is fresh and writes NV12 frames at the size the source asks for.
#pragma once

#include <windows.h>

#include <cstdint>

// {5941F8D3-1AE9-4AFA-92EE-36EA7D4EF0A2}
inline constexpr GUID CLSID_ShutterlinkSource = {
    0x5941f8d3, 0x1ae9, 0x4afa, {0x92, 0xee, 0x36, 0xea, 0x7d, 0x4e, 0xf0, 0xa2}};
inline constexpr wchar_t kShutterlinkClsidString[] = L"{5941F8D3-1AE9-4AFA-92EE-36EA7D4EF0A2}";
inline constexpr wchar_t kShutterlinkFriendlyName[] = L"Shutterlink";

inline constexpr wchar_t kSharedMappingName[] = L"Global\\Shutterlink_Frames_v1";
inline constexpr wchar_t kSharedMutexName[] = L"Global\\Shutterlink_Lock_v1";
inline constexpr wchar_t kSharedFrameEventName[] = L"Global\\Shutterlink_NewFrame_v1";

// Everyone and SYSTEM get full access; low-integrity writers allowed.
inline constexpr wchar_t kSharedSddl[] = L"D:(A;;GA;;;WD)(A;;GA;;;SY)S:(ML;;NW;;;LW)";

inline constexpr uint32_t kSharedMagic = 0x4B4E4C53;  // 'SLNK'
inline constexpr uint32_t kMaxWidth = 1920;
inline constexpr uint32_t kMaxHeight = 1080;
inline constexpr uint32_t kMaxFrameBytes = kMaxWidth * kMaxHeight * 3 / 2;  // NV12

enum class CameraStatus : LONG {
    NoApp = 0,        // Shutterlink.exe has not connected yet
    Searching = 1,    // app running, camera not found / starting
    Streaming = 2,
};

struct SharedFrameHeader {
    uint32_t magic;
    uint32_t headerSize;

    // Written by the source.
    volatile LONG requestedWidth;
    volatile LONG requestedHeight;
    volatile LONG64 sourceHeartbeat;  // GetTickCount64() of last sample request

    // Written by the app (frame fields under the mutex).
    volatile LONG status;             // CameraStatus
    volatile LONG64 appHeartbeat;     // GetTickCount64() of last app loop
    LONG width;
    LONG height;
    LONG64 frameSeq;
    uint32_t reserved[16];
};

inline constexpr size_t kSharedSize = sizeof(SharedFrameHeader) + kMaxFrameBytes;

inline uint8_t* SharedFrameData(SharedFrameHeader* h) {
    return reinterpret_cast<uint8_t*>(h) + sizeof(SharedFrameHeader);
}

// Heartbeats older than this mean the other side is gone.
inline constexpr ULONGLONG kHeartbeatTimeoutMs = 3000;
