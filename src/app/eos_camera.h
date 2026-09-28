// Copyright (C) 2026 Mannai
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Canon EOS live view over PTP (via WPD). Tested on the EOS R6: 1024x576 JPEG at the
// camera's movie frame rate (60 fps at 59.94p over a USB 3 link).
#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "wpd_ptp.h"

// A camera property as last reported by the camera's event stream.
struct EosProp {
    uint32_t value = 0;
    bool hasValue = false;
    std::vector<uint32_t> allowed;  // empty: not settable in the current mode
};
using EosProps = std::map<uint16_t, EosProp>;

class EosCamera {
public:
    enum class Grab { Frame, NotReady, Error };

    ~EosCamera();

    bool Connect();     // finds the first Canon camera and starts live view to the PC
    void Disconnect();  // restores the camera's own screen and leaves remote mode
    bool Connected() const { return connected_; }
    const std::wstring& Name() const { return name_; }

    // Fetches the current live view JPEG. Call in a loop; NotReady means no new frame yet.
    Grab GrabJpeg(std::vector<uint8_t>& jpeg);

    // Controls. Call on the thread that calls GrabJpeg.
    bool SetProperty(uint16_t prop, uint32_t value);
    bool Autofocus();                // starts AF at the current AF point (like a half-press)
    void CancelAutofocus();          // releases the half-press
    bool DriveFocus(int32_t step);   // manual focus: -3..-1 nearer, 1..3 farther
    bool FocusAt(float x, float y);  // move the AF point (normalized 0..1) and focus there

    EosProps Props() const;  // thread-safe snapshot

    // Non-JPEG blocks of the last live view buffer (AF frames, coordinate system, ...),
    // keyed by block type. Same thread as GrabJpeg.
    const std::map<uint32_t, std::vector<uint8_t>>& ViewfinderBlocks() const { return blocks_; }

private:
    PtpResult SetProp(uint16_t prop, uint32_t value);
    void PollEvents();

    WpdPtp ptp_;
    bool connected_ = false;
    std::wstring name_;
    int failures_ = 0;
    std::chrono::steady_clock::time_point lastKeepAlive_{};

    mutable std::mutex propsLock_;
    EosProps props_;
    std::map<uint32_t, std::vector<uint8_t>> blocks_;
};
