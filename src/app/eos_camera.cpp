// Copyright (C) 2026 Mannai
// SPDX-License-Identifier: GPL-3.0-or-later

#include "eos_camera.h"

#include <algorithm>
#include <cstring>
#include <thread>

#include "eos_labels.h"

namespace {

enum : uint16_t {
    kOpSetDevicePropValueEx = 0x9110,
    kOpSetRemoteMode = 0x9114,
    kOpSetEventMode = 0x9115,
    kOpGetEvent = 0x9116,
    kOpKeepDeviceOn = 0x911D,
    kOpGetViewFinderData = 0x9153,
    kOpDoAf = 0x9154,
    kOpDriveLens = 0x9155,
    kOpTouchAfPosition = 0x915B,
    kOpAfCancel = 0x9160,
    kRespBusy = 0x2019,
    kRespNotReady = 0xA102,
};
enum : uint32_t {
    kEvtPropValueChanged = 0xC189,
    kEvtAvailListChanged = 0xC18A,
};
constexpr uint32_t kBlockSensorSize = 0x0E;  // live view block: u32 width, u32 height
constexpr uint16_t kPropEvfOutputDevice = 0xD1B0;
constexpr uint16_t kPropEvfMode = 0xD1B1;
constexpr uint32_t kEvfOutputTft = 1;
constexpr uint32_t kEvfOutputPc = 2;  // largest live view size ("PC" / Large)

// Splits a GetViewFinderData buffer (blocks of u32 length incl. header, u32 type, payload):
// the first JPEG goes to `jpeg`, every other block to `blocks`.
bool SplitViewfinder(const std::vector<uint8_t>& d, std::vector<uint8_t>& jpeg,
                     std::map<uint32_t, std::vector<uint8_t>>& blocks) {
    bool found = false;
    size_t pos = 0;
    while (pos + 8 <= d.size()) {
        uint32_t len, type;
        memcpy(&len, &d[pos], 4);
        memcpy(&type, &d[pos + 4], 4);
        if (len < 8 || pos + len > d.size()) break;
        const uint8_t* payload = &d[pos + 8];
        if (!found && len > 10 && payload[0] == 0xFF && payload[1] == 0xD8) {
            jpeg.assign(payload, payload + (len - 8));
            found = true;
        } else {
            blocks[type].assign(payload, payload + (len - 8));
        }
        pos += len;
    }
    return found;
}

uint32_t ReadU32(const uint8_t* p, size_t avail) {
    uint32_t v = 0;
    memcpy(&v, p, avail < 4 ? avail : 4);
    return v;
}

}  // namespace

EosCamera::~EosCamera() { Disconnect(); }

PtpResult EosCamera::SetProp(uint16_t prop, uint32_t value) {
    std::vector<uint8_t> payload(12);
    uint32_t words[3] = {12, prop, value};
    memcpy(payload.data(), words, sizeof(words));
    return ptp_.Write(kOpSetDevicePropValueEx, {}, payload);
}

// Folds GetEvent records into the property cache. Record: u32 size, u32 type, payload.
void EosCamera::PollEvents() {
    PtpResult ev = ptp_.Read(kOpGetEvent);
    if (!ev.ok()) return;
    const auto& d = ev.data;
    std::lock_guard lk(propsLock_);
    for (size_t pos = 0; pos + 8 <= d.size();) {
        uint32_t size = ReadU32(&d[pos], 4), type = ReadU32(&d[pos + 4], 4);
        if (size < 8 || type == 0 || pos + size > d.size()) break;
        const uint8_t* p = &d[pos + 8];
        const size_t n = size - 8;
        if (type == kEvtPropValueChanged && n >= 5) {
            uint16_t prop = static_cast<uint16_t>(ReadU32(p, 4));
            EosProp& e = props_[prop];
            e.value = eos::Normalize(prop, ReadU32(p + 4, n - 4));
            e.hasValue = true;
        } else if (type == kEvtAvailListChanged && n >= 12) {
            uint16_t prop = static_cast<uint16_t>(ReadU32(p, 4));
            uint32_t dtype = ReadU32(p + 4, 4), count = ReadU32(p + 8, 4);
            EosProp& e = props_[prop];
            e.allowed.clear();
            if (dtype == 3) {  // enumeration; each value in a 4-byte slot
                for (uint32_t i = 0; i < count && 12 + (i + 1) * 4 <= n; ++i)
                    e.allowed.push_back(eos::Normalize(prop, ReadU32(p + 12 + i * 4, 4)));
            }
        }
        pos += size;
    }
}

EosProps EosCamera::Props() const {
    std::lock_guard lk(propsLock_);
    return props_;
}

bool EosCamera::Connect() {
    Disconnect();
    for (auto& d : WpdPtp::Enumerate()) {
        if (d.manufacturer.find(L"Canon") == std::wstring::npos) continue;
        if (FAILED(ptp_.Open(d.id))) continue;
        name_ = d.name;

        if (!ptp_.Command(kOpSetRemoteMode, {1}).ok() || !ptp_.Command(kOpSetEventMode, {1}).ok()) {
            ptp_.Close();
            continue;
        }
        {
            std::lock_guard lk(propsLock_);
            props_.clear();
        }
        PollEvents();                    // initial dump of every property and allowed list
        SetProp(kPropEvfMode, 1);        // "busy" in movie mode is expected and harmless
        if (!SetProp(kPropEvfOutputDevice, kEvfOutputPc).ok()) {
            ptp_.Command(kOpSetEventMode, {0});
            ptp_.Command(kOpSetRemoteMode, {0});
            ptp_.Close();
            continue;
        }
        connected_ = true;
        failures_ = 0;
        lastKeepAlive_ = std::chrono::steady_clock::now();
        return true;
    }
    return false;
}

void EosCamera::Disconnect() {
    if (!connected_) return;
    connected_ = false;
    ptp_.Command(kOpAfCancel);
    SetProp(kPropEvfOutputDevice, kEvfOutputTft);
    ptp_.Command(kOpSetEventMode, {0});
    ptp_.Command(kOpSetRemoteMode, {0});
    ptp_.Close();
    std::lock_guard lk(propsLock_);
    props_.clear();
}

EosCamera::Grab EosCamera::GrabJpeg(std::vector<uint8_t>& jpeg) {
    if (!connected_) return Grab::Error;

    auto now = std::chrono::steady_clock::now();
    if (now - lastKeepAlive_ > std::chrono::seconds(8)) {  // stop auto power-off
        ptp_.Command(kOpKeepDeviceOn);
        lastKeepAlive_ = now;
    }
    PollEvents();  // the camera expects its event queue to be polled

    PtpResult vf = ptp_.Read(kOpGetViewFinderData, {0x00200000, 0, 0});
    if (vf.code == kRespBusy || vf.code == kRespNotReady) return Grab::NotReady;
    if (!vf.ok()) {
        // A few transient errors happen (e.g. mode dial turned); a run of them means unplugged.
        return ++failures_ > 15 ? Grab::Error : Grab::NotReady;
    }
    failures_ = 0;
    return SplitViewfinder(vf.data, jpeg, blocks_) ? Grab::Frame : Grab::NotReady;
}

bool EosCamera::SetProperty(uint16_t prop, uint32_t value) {
    if (!connected_) return false;
    bool ok = SetProp(prop, eos::Normalize(prop, value)).ok();
    PollEvents();  // pick up the confirmed value (and any lists it changed)
    return ok;
}

bool EosCamera::Autofocus() {
    if (!connected_) return false;
    bool ok = ptp_.Command(kOpDoAf).ok();
    PollEvents();
    return ok;
}

void EosCamera::CancelAutofocus() {
    if (connected_) ptp_.Command(kOpAfCancel);
}

// Touch AF: opcode 0x915B (0x03, x, y) with the AF point's centre in sensor coordinates.
// The sensor size comes from live view block 0x0E (u32 width, u32 height). The 16:9 live
// view is assumed to be the full sensor width with a centred vertical band.
bool EosCamera::FocusAt(float x, float y) {
    if (!connected_) return false;
    uint32_t sensorW = 6000, sensorH = 4000;
    auto it = blocks_.find(kBlockSensorSize);
    if (it != blocks_.end() && it->second.size() >= 8) {
        uint32_t w = ReadU32(it->second.data(), 4), h = ReadU32(it->second.data() + 4, 4);
        if (w > 0 && h > 0) sensorW = w, sensorH = h;
    }
    uint32_t bandH = std::min(sensorH, sensorW * 9 / 16);
    auto clamp01 = [](float v) { return v < 0 ? 0.0f : v > 1 ? 1.0f : v; };
    uint32_t sx = static_cast<uint32_t>(clamp01(x) * (sensorW - 1));
    uint32_t sy = (sensorH - bandH) / 2 + static_cast<uint32_t>(clamp01(y) * (bandH - 1));
    bool ok = ptp_.Command(kOpTouchAfPosition, {3, sx, sy}).ok();
    PollEvents();
    return ok;
}

bool EosCamera::DriveFocus(int32_t step) {
    if (!connected_ || step == 0 || step < -3 || step > 3) return false;
    uint32_t param = step < 0 ? static_cast<uint32_t>(-step) : 0x8000u | static_cast<uint32_t>(step);
    bool ok = ptp_.Command(kOpDriveLens, {param}).ok();
    PollEvents();
    return ok;
}
