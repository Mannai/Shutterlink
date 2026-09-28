// Copyright (C) 2026 Mannai
// SPDX-License-Identifier: GPL-3.0-or-later

#include "media_source.h"

#include <mferror.h>

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::MakeAndInitialize;

HRESULT MediaSource::RuntimeClassInitialize(IMFAttributes* activatorAttributes) {
    HRESULT hr = MFCreateEventQueue(&queue_);
    if (SUCCEEDED(hr)) hr = MFCreateAttributes(&attributes_, 4);
    if (SUCCEEDED(hr) && activatorAttributes) hr = activatorAttributes->CopyAllItems(attributes_.Get());
    if (SUCCEEDED(hr)) hr = MakeAndInitialize<MediaStream>(&stream_, static_cast<IMFMediaSource*>(this), &link_);
    if (FAILED(hr)) return hr;

    IMFStreamDescriptor* sd = stream_->Descriptor();
    hr = MFCreatePresentationDescriptor(1, &sd, &descriptor_);
    if (SUCCEEDED(hr)) hr = descriptor_->SelectStream(0);
    return hr;
}

HRESULT MediaSource::CheckShutdown() const { return queue_ ? S_OK : MF_E_SHUTDOWN; }

IFACEMETHODIMP MediaSource::BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state) {
    std::lock_guard lk(lock_);
    HRESULT hr = CheckShutdown();
    return SUCCEEDED(hr) ? queue_->BeginGetEvent(callback, state) : hr;
}

IFACEMETHODIMP MediaSource::EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event) {
    std::lock_guard lk(lock_);
    HRESULT hr = CheckShutdown();
    return SUCCEEDED(hr) ? queue_->EndGetEvent(result, event) : hr;
}

IFACEMETHODIMP MediaSource::GetEvent(DWORD flags, IMFMediaEvent** event) {
    ComPtr<IMFMediaEventQueue> queue;
    {
        std::lock_guard lk(lock_);
        HRESULT hr = CheckShutdown();
        if (FAILED(hr)) return hr;
        queue = queue_;
    }
    return queue->GetEvent(flags, event);
}

IFACEMETHODIMP MediaSource::QueueEvent(MediaEventType type, REFGUID extType, HRESULT status,
                                       const PROPVARIANT* data) {
    std::lock_guard lk(lock_);
    HRESULT hr = CheckShutdown();
    return SUCCEEDED(hr) ? queue_->QueueEventParamVar(type, extType, status, data) : hr;
}

IFACEMETHODIMP MediaSource::CreatePresentationDescriptor(IMFPresentationDescriptor** pd) {
    if (!pd) return E_POINTER;
    std::lock_guard lk(lock_);
    HRESULT hr = CheckShutdown();
    return SUCCEEDED(hr) ? descriptor_->Clone(pd) : hr;
}

IFACEMETHODIMP MediaSource::GetCharacteristics(DWORD* characteristics) {
    if (!characteristics) return E_POINTER;
    std::lock_guard lk(lock_);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;
    *characteristics = MFMEDIASOURCE_IS_LIVE;
    return S_OK;
}

IFACEMETHODIMP MediaSource::Pause() { return MF_E_INVALID_STATE_TRANSITION; }

IFACEMETHODIMP MediaSource::Shutdown() {
    ComPtr<MediaStream> stream;
    {
        std::lock_guard lk(lock_);
        HRESULT hr = CheckShutdown();
        if (FAILED(hr)) return hr;
        queue_->Shutdown();
        queue_.Reset();
        stream = std::move(stream_);
        descriptor_.Reset();
    }
    if (stream) stream->Shutdown();
    link_.Close();
    return S_OK;
}

IFACEMETHODIMP MediaSource::Start(IMFPresentationDescriptor* pd, const GUID* timeFormat,
                                  const PROPVARIANT* startPosition) {
    if (!pd) return E_POINTER;
    if (timeFormat && *timeFormat != GUID_NULL) return MF_E_UNSUPPORTED_TIME_FORMAT;
    std::lock_guard lk(lock_);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;

    BOOL selected = FALSE;
    ComPtr<IMFStreamDescriptor> sd;
    hr = pd->GetStreamDescriptorByIndex(0, &selected, &sd);
    if (FAILED(hr)) return hr;

    if (selected) {
        ComPtr<IMFMediaTypeHandler> handler;
        ComPtr<IMFMediaType> type;
        hr = sd->GetMediaTypeHandler(&handler);
        if (SUCCEEDED(hr)) hr = handler->GetCurrentMediaType(&type);
        if (FAILED(hr)) return hr;

        ComPtr<IUnknown> unk;
        stream_.As(&unk);
        hr = queue_->QueueEventParamUnk(streamAnnounced_ ? MEUpdatedStream : MENewStream, GUID_NULL,
                                        S_OK, unk.Get());
        if (FAILED(hr)) return hr;
        streamAnnounced_ = true;
        hr = stream_->Start(type.Get());
        if (FAILED(hr)) return hr;
    }

    PROPVARIANT time;
    PropVariantInit(&time);
    time.vt = VT_I8;
    time.hVal.QuadPart = MFGetSystemTime();
    (void)startPosition;  // live source: always starts "now"
    return queue_->QueueEventParamVar(MESourceStarted, GUID_NULL, S_OK, &time);
}

IFACEMETHODIMP MediaSource::Stop() {
    std::lock_guard lk(lock_);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;
    stream_->Stop();
    return queue_->QueueEventParamVar(MESourceStopped, GUID_NULL, S_OK, nullptr);
}

IFACEMETHODIMP MediaSource::GetSourceAttributes(IMFAttributes** attributes) {
    if (!attributes) return E_POINTER;
    std::lock_guard lk(lock_);
    HRESULT hr = CheckShutdown();
    return SUCCEEDED(hr) ? attributes_.CopyTo(attributes) : hr;
}

IFACEMETHODIMP MediaSource::GetStreamAttributes(DWORD streamId, IMFAttributes** attributes) {
    if (!attributes) return E_POINTER;
    if (streamId != 0) return E_INVALIDARG;
    std::lock_guard lk(lock_);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;
    *attributes = stream_->Attributes();
    (*attributes)->AddRef();
    return S_OK;
}

IFACEMETHODIMP MediaSource::SetD3DManager(IUnknown*) { return S_OK; }  // system-memory frames

IFACEMETHODIMP MediaSource::GetService(REFGUID, REFIID, LPVOID* object) {
    if (object) *object = nullptr;
    return MF_E_UNSUPPORTED_SERVICE;
}

IFACEMETHODIMP MediaSource::KsProperty(PKSPROPERTY, ULONG, LPVOID, ULONG, ULONG*) {
    return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

IFACEMETHODIMP MediaSource::KsMethod(PKSMETHOD, ULONG, LPVOID, ULONG, ULONG*) {
    return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

IFACEMETHODIMP MediaSource::KsEvent(PKSEVENT, ULONG, LPVOID, ULONG, ULONG*) {
    return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}
