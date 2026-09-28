// Copyright (C) 2026 Mannai
// SPDX-License-Identifier: GPL-3.0-or-later

#include "media_stream.h"

#include <ks.h>
#include <ksmedia.h>
#include <mferror.h>

#include <iterator>

using Microsoft::WRL::ComPtr;

namespace {

struct Format {
    UINT32 width, height, fps;
};

// Camera delivers 1024x576 @ 60; larger sizes are upscaled by Shutterlink.exe.
constexpr Format kFormats[] = {
    {1920, 1080, 60}, {1920, 1080, 30}, {1280, 720, 60}, {1280, 720, 30},
    {1024, 576, 60},  {1024, 576, 30},  {640, 360, 30},
};

HRESULT CreateNv12Type(const Format& f, IMFMediaType** out) {
    ComPtr<IMFMediaType> t;
    HRESULT hr = MFCreateMediaType(&t);
    if (FAILED(hr)) return hr;
    t->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    t->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    MFSetAttributeSize(t.Get(), MF_MT_FRAME_SIZE, f.width, f.height);
    MFSetAttributeRatio(t.Get(), MF_MT_FRAME_RATE, f.fps, 1);
    MFSetAttributeRatio(t.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    t->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    t->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    t->SetUINT32(MF_MT_FIXED_SIZE_SAMPLES, TRUE);
    t->SetUINT32(MF_MT_DEFAULT_STRIDE, f.width);
    t->SetUINT32(MF_MT_SAMPLE_SIZE, f.width * f.height * 3 / 2);
    t->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709);
    t->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235);
    t->SetUINT32(MF_MT_VIDEO_PRIMARIES, MFVideoPrimaries_BT709);
    *out = t.Detach();
    return S_OK;
}

HRESULT SetStreamAttributes(IMFAttributes* a) {
    a->SetGUID(MF_DEVICESTREAM_STREAM_CATEGORY, PINNAME_VIDEO_CAPTURE);
    a->SetUINT32(MF_DEVICESTREAM_STREAM_ID, 0);
    a->SetUINT32(MF_DEVICESTREAM_FRAMESERVER_SHARED, 1);
    return a->SetUINT32(MF_DEVICESTREAM_ATTRIBUTE_FRAMESOURCE_TYPES, MFFrameSourceTypes_Color);
}

}  // namespace

HRESULT MediaStream::RuntimeClassInitialize(IMFMediaSource* source, FrameLink* link) {
    link_ = link;
    source_ = source;
    HRESULT hr = MFCreateEventQueue(&queue_);
    if (FAILED(hr)) return hr;

    IMFMediaType* types[std::size(kFormats)] = {};
    for (size_t i = 0; i < std::size(kFormats) && SUCCEEDED(hr); ++i)
        hr = CreateNv12Type(kFormats[i], &types[i]);
    if (SUCCEEDED(hr))
        hr = MFCreateStreamDescriptor(0, static_cast<DWORD>(std::size(kFormats)), types, &descriptor_);
    if (SUCCEEDED(hr)) {
        ComPtr<IMFMediaTypeHandler> handler;
        hr = descriptor_->GetMediaTypeHandler(&handler);
        if (SUCCEEDED(hr)) hr = handler->SetCurrentMediaType(types[0]);
    }
    for (IMFMediaType* t : types)
        if (t) t->Release();
    if (FAILED(hr)) return hr;

    hr = SetStreamAttributes(descriptor_.Get());
    if (SUCCEEDED(hr)) hr = MFCreateAttributes(&attributes_, 4);
    if (SUCCEEDED(hr)) hr = SetStreamAttributes(attributes_.Get());
    return hr;
}

HRESULT MediaStream::CheckShutdown() const { return queue_ ? S_OK : MF_E_SHUTDOWN; }

HRESULT MediaStream::Start(IMFMediaType* type) {
    std::lock_guard lk(lock_);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;

    UINT32 num = 0, den = 0;
    hr = MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &width_, &height_);
    if (FAILED(hr)) return hr;
    if (FAILED(MFGetAttributeRatio(type, MF_MT_FRAME_RATE, &num, &den)) || !num) num = 60, den = 1;
    frameDuration_ = 10'000'000LL * den / num;
    lastDelivered_ = 0;

    link_->Open();  // best effort: without it we stream black frames
    link_->Request(width_, height_);
    state_ = MF_STREAM_STATE_RUNNING;
    return queue_->QueueEventParamVar(MEStreamStarted, GUID_NULL, S_OK, nullptr);
}

HRESULT MediaStream::Stop() {
    std::lock_guard lk(lock_);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;
    state_ = MF_STREAM_STATE_STOPPED;
    return queue_->QueueEventParamVar(MEStreamStopped, GUID_NULL, S_OK, nullptr);
}

void MediaStream::Shutdown() {
    std::lock_guard lk(lock_);
    if (queue_) queue_->Shutdown();
    queue_.Reset();
    source_.Reset();
    state_ = MF_STREAM_STATE_STOPPED;
}

IFACEMETHODIMP MediaStream::BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state) {
    std::lock_guard lk(lock_);
    HRESULT hr = CheckShutdown();
    return SUCCEEDED(hr) ? queue_->BeginGetEvent(callback, state) : hr;
}

IFACEMETHODIMP MediaStream::EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event) {
    std::lock_guard lk(lock_);
    HRESULT hr = CheckShutdown();
    return SUCCEEDED(hr) ? queue_->EndGetEvent(result, event) : hr;
}

IFACEMETHODIMP MediaStream::GetEvent(DWORD flags, IMFMediaEvent** event) {
    ComPtr<IMFMediaEventQueue> queue;
    {
        std::lock_guard lk(lock_);
        HRESULT hr = CheckShutdown();
        if (FAILED(hr)) return hr;
        queue = queue_;
    }
    return queue->GetEvent(flags, event);  // may block; don't hold the lock
}

IFACEMETHODIMP MediaStream::QueueEvent(MediaEventType type, REFGUID extType, HRESULT status,
                                       const PROPVARIANT* data) {
    std::lock_guard lk(lock_);
    HRESULT hr = CheckShutdown();
    return SUCCEEDED(hr) ? queue_->QueueEventParamVar(type, extType, status, data) : hr;
}

IFACEMETHODIMP MediaStream::GetMediaSource(IMFMediaSource** source) {
    if (!source) return E_POINTER;
    std::lock_guard lk(lock_);
    HRESULT hr = CheckShutdown();
    return SUCCEEDED(hr) ? source_.CopyTo(source) : hr;
}

IFACEMETHODIMP MediaStream::GetStreamDescriptor(IMFStreamDescriptor** descriptor) {
    if (!descriptor) return E_POINTER;
    std::lock_guard lk(lock_);
    HRESULT hr = CheckShutdown();
    return SUCCEEDED(hr) ? descriptor_.CopyTo(descriptor) : hr;
}

IFACEMETHODIMP MediaStream::RequestSample(IUnknown* token) {
    std::lock_guard lk(lock_);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;
    if (state_ != MF_STREAM_STATE_RUNNING) return MF_E_INVALIDREQUEST;

    // Pace to the negotiated frame rate, then take the freshest camera frame.
    LONGLONG now = MFGetSystemTime();
    LONGLONG minGap = frameDuration_ * 9 / 10;
    if (lastDelivered_ && now - lastDelivered_ < minGap)
        Sleep(static_cast<DWORD>((minGap - (now - lastDelivered_)) / 10'000));

    const DWORD size = width_ * height_ * 3 / 2;
    ComPtr<IMFMediaBuffer> buffer;
    hr = MFCreateAlignedMemoryBuffer(size, MF_64_BYTE_ALIGNMENT, &buffer);
    if (FAILED(hr)) return hr;
    BYTE* data = nullptr;
    hr = buffer->Lock(&data, nullptr, nullptr);
    if (FAILED(hr)) return hr;
    link_->Read(data, width_, height_, static_cast<DWORD>(frameDuration_ / 10'000));
    buffer->Unlock();
    buffer->SetCurrentLength(size);

    ComPtr<IMFSample> sample;
    hr = MFCreateSample(&sample);
    if (SUCCEEDED(hr)) hr = sample->AddBuffer(buffer.Get());
    if (FAILED(hr)) return hr;
    lastDelivered_ = MFGetSystemTime();
    sample->SetSampleTime(lastDelivered_);
    sample->SetSampleDuration(frameDuration_);
    if (token) sample->SetUnknown(MFSampleExtension_Token, token);
    return queue_->QueueEventParamUnk(MEMediaSample, GUID_NULL, S_OK, sample.Get());
}

IFACEMETHODIMP MediaStream::SetStreamState(MF_STREAM_STATE state) {
    std::lock_guard lk(lock_);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;
    state_ = state;
    return S_OK;
}

IFACEMETHODIMP MediaStream::GetStreamState(MF_STREAM_STATE* state) {
    if (!state) return E_POINTER;
    std::lock_guard lk(lock_);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;
    *state = state_;
    return S_OK;
}
