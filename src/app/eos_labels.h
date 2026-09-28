// Copyright (C) 2026 Mannai
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Canon EOS property codes and value labels (value encodings as used by libgphoto2's
// camlibs/ptp2/config.c; codes only — no code copied).
#pragma once

#include <cstdint>
#include <string>

namespace eos {

enum Prop : uint16_t {
    kAperture = 0xD101,
    kShutterSpeed = 0xD102,
    kIso = 0xD103,
    kExposureComp = 0xD104,
    kExposureMode = 0xD105,
    kFocusMode = 0xD108,
    kWhiteBalance = 0xD109,
    kColorTemperature = 0xD10A,
    kPictureStyle = 0xD110,
    kMovieServoAf = 0xD179,
    kAfMethod = 0xD1BA,
};

enum class Width { U8, S8, U16, U32 };
Width WidthOf(uint16_t prop);
uint32_t Normalize(uint16_t prop, uint32_t raw);  // keep only the bytes the property uses

const wchar_t* PropTitle(uint16_t prop);
std::wstring ValueLabel(uint16_t prop, uint32_t value);

}  // namespace eos
