// Copyright (C) 2026 Mannai
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Decodes camera JPEGs and produces NV12 (BT.709, limited range) at any output size.
#pragma once

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdint>
#include <vector>

class FrameConverter {
public:
    HRESULT Init();

    // Scales to cover width x height (center crop if the aspect differs) and writes
    // width*height*3/2 bytes of NV12 to `nv12`, optionally mirrored left-right.
    HRESULT JpegToNv12(const std::vector<uint8_t>& jpeg, UINT width, UINT height, bool mirror,
                       uint8_t* nv12);

private:
    Microsoft::WRL::ComPtr<IWICImagingFactory> wic_;
    std::vector<uint8_t> bgra_;
};
