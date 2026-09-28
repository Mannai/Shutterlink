// Copyright (C) 2026 Mannai
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Small always-on-top window: a grid over a live thumbnail; clicking a cell moves the
// camera's AF point there.
#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>
#include <vector>

struct FocusWindowHost {
    std::function<bool()> cameraReady;
    std::function<bool(std::vector<uint8_t>&)> latestJpeg;  // copy of the newest camera frame
    std::function<bool()> mirrored;
    std::function<void(float x, float y)> focusAt;          // normalized 0..1, camera orientation
    std::function<void()> autofocus;
};

void ShowFocusWindow(HINSTANCE instance, const FocusWindowHost& host);
