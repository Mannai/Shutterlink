// Copyright (C) 2026 Mannai
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <wrl.h>

#include <mutex>

#include "frame_link.h"

class MediaStream
    : public Microsoft::WRL::RuntimeClass<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
          Microsoft::WRL::ChainInterfaces<IMFMediaStream2, IMFMediaStream, IMFMediaEventGenerator>> {
public:
    HRESULT RuntimeClassInitialize(IMFMediaSource* source, FrameLink* link);

    HRESULT Start(IMFMediaType* type);
    HRESULT Stop();
    void Shutdown();

    IMFStreamDescriptor* Descriptor() const { return descriptor_.Get(); }
    IMFAttributes* Attributes() const { return attributes_.Get(); }

    // IMFMediaEventGenerator
    IFACEMETHODIMP BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state) override;
    IFACEMETHODIMP EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event) override;
    IFACEMETHODIMP GetEvent(DWORD flags, IMFMediaEvent** event) override;
    IFACEMETHODIMP QueueEvent(MediaEventType type, REFGUID extType, HRESULT status,
                              const PROPVARIANT* value) override;

    // IMFMediaStream
    IFACEMETHODIMP GetMediaSource(IMFMediaSource** source) override;
    IFACEMETHODIMP GetStreamDescriptor(IMFStreamDescriptor** descriptor) override;
    IFACEMETHODIMP RequestSample(IUnknown* token) override;

    // IMFMediaStream2
    IFACEMETHODIMP SetStreamState(MF_STREAM_STATE state) override;
    IFACEMETHODIMP GetStreamState(MF_STREAM_STATE* state) override;

private:
    HRESULT CheckShutdown() const;

    std::mutex lock_;
    Microsoft::WRL::ComPtr<IMFMediaSource> source_;  // weak in spirit; cleared on shutdown
    Microsoft::WRL::ComPtr<IMFMediaEventQueue> queue_;
    Microsoft::WRL::ComPtr<IMFStreamDescriptor> descriptor_;
    Microsoft::WRL::ComPtr<IMFAttributes> attributes_;
    FrameLink* link_ = nullptr;

    MF_STREAM_STATE state_ = MF_STREAM_STATE_STOPPED;
    UINT32 width_ = 0, height_ = 0;
    LONGLONG frameDuration_ = 0;  // 100 ns units
    LONGLONG lastDelivered_ = 0;  // MFGetSystemTime() of last sample
};
