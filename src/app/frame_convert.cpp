// Copyright (C) 2026 Mannai
// SPDX-License-Identifier: GPL-3.0-or-later

#include "frame_convert.h"

#include <algorithm>
#include <cmath>
#include <execution>
#include <numeric>

using Microsoft::WRL::ComPtr;

namespace {

inline uint8_t Clamp(int v) { return static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v); }

// BT.709 limited-range RGB -> YCbCr, 8.8 fixed point.
inline uint8_t Luma(int r, int g, int b) { return Clamp(((47 * r + 157 * g + 16 * b + 128) >> 8) + 16); }
inline uint8_t Cb(int r, int g, int b) { return Clamp(((-26 * r - 86 * g + 112 * b + 128) >> 8) + 128); }
inline uint8_t Cr(int r, int g, int b) { return Clamp(((112 * r - 102 * g - 10 * b + 128) >> 8) + 128); }

void BgraToNv12(const uint8_t* bgra, UINT w, UINT h, bool mirror, uint8_t* nv12) {
    uint8_t* yPlane = nv12;
    uint8_t* uvPlane = nv12 + static_cast<size_t>(w) * h;
    std::vector<UINT> pairs(h / 2);
    std::iota(pairs.begin(), pairs.end(), 0u);
    std::for_each(std::execution::par, pairs.begin(), pairs.end(), [&](UINT pr) {
        const UINT y0 = pr * 2;
        const uint8_t* r0 = bgra + static_cast<size_t>(y0) * w * 4;
        const uint8_t* r1 = r0 + static_cast<size_t>(w) * 4;
        uint8_t* yo0 = yPlane + static_cast<size_t>(y0) * w;
        uint8_t* yo1 = yo0 + w;
        uint8_t* uv = uvPlane + static_cast<size_t>(pr) * w;
        for (UINT x = 0; x < w; x += 2) {
            // Mirrored: output column x reads source column w-1-x.
            const UINT x0 = mirror ? w - 1 - x : x, x1 = mirror ? x0 - 1 : x + 1;
            const uint8_t* p[4] = {r0 + x0 * 4, r0 + x1 * 4, r1 + x0 * 4, r1 + x1 * 4};
            int rs = 0, gs = 0, bs = 0;
            for (int i = 0; i < 4; ++i) {
                int b = p[i][0], g = p[i][1], r = p[i][2];
                rs += r, gs += g, bs += b;
                (i < 2 ? yo0 : yo1)[x + (i & 1)] = Luma(r, g, b);
            }
            rs = (rs + 2) >> 2, gs = (gs + 2) >> 2, bs = (bs + 2) >> 2;
            uv[x] = Cb(rs, gs, bs);
            uv[x + 1] = Cr(rs, gs, bs);
        }
    });
}

}  // namespace

HRESULT FrameConverter::Init() {
    return CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic_));
}

HRESULT FrameConverter::JpegToNv12(const std::vector<uint8_t>& jpeg, UINT width, UINT height, bool mirror,
                                   uint8_t* nv12) {
    ComPtr<IWICStream> stream;
    HRESULT hr = wic_->CreateStream(&stream);
    if (SUCCEEDED(hr))
        hr = stream->InitializeFromMemory(const_cast<BYTE*>(jpeg.data()), static_cast<DWORD>(jpeg.size()));
    ComPtr<IWICBitmapDecoder> decoder;
    if (SUCCEEDED(hr))
        hr = wic_->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder);
    ComPtr<IWICBitmapFrameDecode> frame;
    if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
    UINT sw = 0, sh = 0;
    if (SUCCEEDED(hr)) hr = frame->GetSize(&sw, &sh);
    if (FAILED(hr)) return hr;
    if (!sw || !sh) return E_FAIL;

    // Scale to cover the target, then center-crop.
    double scale = std::max(static_cast<double>(width) / sw, static_cast<double>(height) / sh);
    UINT cw = std::max(width, static_cast<UINT>(std::lround(sw * scale)));
    UINT ch = std::max(height, static_cast<UINT>(std::lround(sh * scale)));

    ComPtr<IWICBitmapSource> src = frame;
    if (cw != sw || ch != sh) {
        ComPtr<IWICBitmapScaler> scaler;
        hr = wic_->CreateBitmapScaler(&scaler);
        if (SUCCEEDED(hr))
            hr = scaler->Initialize(frame.Get(), cw, ch, WICBitmapInterpolationModeHighQualityCubic);
        if (FAILED(hr)) return hr;
        src = scaler;
    }
    ComPtr<IWICFormatConverter> converter;
    hr = wic_->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr))
        hr = converter->Initialize(src.Get(), GUID_WICPixelFormat32bppBGR, WICBitmapDitherTypeNone,
                                   nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) return hr;

    WICRect rect{static_cast<INT>((cw - width) / 2), static_cast<INT>((ch - height) / 2),
                 static_cast<INT>(width), static_cast<INT>(height)};
    const UINT stride = width * 4;
    bgra_.resize(static_cast<size_t>(stride) * height);
    hr = converter->CopyPixels(&rect, stride, static_cast<UINT>(bgra_.size()), bgra_.data());
    if (FAILED(hr)) return hr;

    BgraToNv12(bgra_.data(), width, height, mirror, nv12);
    return S_OK;
}
