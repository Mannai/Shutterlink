// Copyright (C) 2026 Mannai
// SPDX-License-Identifier: GPL-3.0-or-later

#include "eos_labels.h"

#include <cstdio>
#include <iterator>

namespace eos {
namespace {

struct Entry {
    uint32_t code;
    const wchar_t* label;
};

template <size_t N>
const wchar_t* Find(const Entry (&table)[N], uint32_t code) {
    for (const Entry& e : table)
        if (e.code == code) return e.label;
    return nullptr;
}

const Entry kAperturesLabels[] = {
    {0x08, L"f/1"},   {0x0B, L"f/1.1"}, {0x0C, L"f/1.2"}, {0x0D, L"f/1.2"}, {0x10, L"f/1.4"},
    {0x13, L"f/1.6"}, {0x14, L"f/1.8"}, {0x15, L"f/1.8"}, {0x18, L"f/2"},   {0x1B, L"f/2.2"},
    {0x1C, L"f/2.5"}, {0x1D, L"f/2.5"}, {0x20, L"f/2.8"}, {0x23, L"f/3.2"}, {0x24, L"f/3.5"},
    {0x25, L"f/3.5"}, {0x28, L"f/4"},   {0x2B, L"f/4.5"}, {0x2C, L"f/4.5"}, {0x2D, L"f/5"},
    {0x30, L"f/5.6"}, {0x33, L"f/6.3"}, {0x34, L"f/6.7"}, {0x35, L"f/7.1"}, {0x38, L"f/8"},
    {0x3B, L"f/9"},   {0x3C, L"f/9.5"}, {0x3D, L"f/10"},  {0x40, L"f/11"},  {0x43, L"f/13"},
    {0x44, L"f/13"},  {0x45, L"f/14"},  {0x48, L"f/16"},  {0x4B, L"f/18"},  {0x4C, L"f/19"},
    {0x4D, L"f/20"},  {0x50, L"f/22"},  {0x53, L"f/25"},  {0x54, L"f/27"},  {0x55, L"f/29"},
    {0x58, L"f/32"},  {0x5B, L"f/36"},  {0x5C, L"f/38"},  {0x5D, L"f/40"},  {0x60, L"f/45"},
    {0x63, L"f/51"},  {0x64, L"f/54"},  {0x65, L"f/57"},  {0x68, L"f/64"},  {0x6B, L"f/72"},
    {0x6C, L"f/76"},  {0x6D, L"f/81"},  {0x70, L"f/91"},  {0x00, L"Auto"},  {0xB0, L"Auto"},
    {0xFFFF, L"Auto"},
};

const Entry kShutterLabels[] = {
    {0x00, L"Auto"},    {0x04, L"Bulb"},    {0x0C, L"Bulb"},    {0x10, L"30\""},    {0x13, L"25\""},
    {0x14, L"20\""},    {0x15, L"20\""},    {0x18, L"15\""},    {0x1B, L"13\""},    {0x1C, L"10\""},
    {0x1D, L"10\""},    {0x20, L"8\""},     {0x23, L"6\""},     {0x24, L"6\""},     {0x25, L"5\""},
    {0x28, L"4\""},     {0x2B, L"3.2\""},   {0x2C, L"3\""},     {0x2D, L"2.5\""},   {0x30, L"2\""},
    {0x33, L"1.6\""},   {0x34, L"1.5\""},   {0x35, L"1.3\""},   {0x38, L"1\""},     {0x3B, L"0.8\""},
    {0x3C, L"0.7\""},   {0x3D, L"0.6\""},   {0x40, L"0.5\""},   {0x43, L"0.4\""},   {0x44, L"0.3\""},
    {0x45, L"0.3\""},   {0x48, L"1/4"},     {0x4B, L"1/5"},     {0x4C, L"1/6"},     {0x4D, L"1/6"},
    {0x50, L"1/8"},     {0x53, L"1/10"},    {0x54, L"1/10"},    {0x55, L"1/13"},    {0x58, L"1/15"},
    {0x5B, L"1/20"},    {0x5C, L"1/20"},    {0x5D, L"1/25"},    {0x60, L"1/30"},    {0x63, L"1/40"},
    {0x64, L"1/45"},    {0x65, L"1/50"},    {0x68, L"1/60"},    {0x6B, L"1/80"},    {0x6C, L"1/90"},
    {0x6D, L"1/100"},   {0x70, L"1/125"},   {0x73, L"1/160"},   {0x74, L"1/180"},   {0x75, L"1/200"},
    {0x78, L"1/250"},   {0x7B, L"1/320"},   {0x7C, L"1/350"},   {0x7D, L"1/400"},   {0x80, L"1/500"},
    {0x83, L"1/640"},   {0x84, L"1/750"},   {0x85, L"1/800"},   {0x88, L"1/1000"},  {0x8B, L"1/1250"},
    {0x8C, L"1/1500"},  {0x8D, L"1/1600"},  {0x90, L"1/2000"},  {0x93, L"1/2500"},  {0x94, L"1/3000"},
    {0x95, L"1/3200"},  {0x98, L"1/4000"},  {0x9B, L"1/5000"},  {0x9C, L"1/6000"},  {0x9D, L"1/6400"},
    {0xA0, L"1/8000"},  {0xA8, L"1/16000"},
};

const Entry kIsoLabels[] = {
    {0x00, L"Auto"},   {0x01, L"Auto"},   {0x28, L"6"},       {0x30, L"12"},      {0x38, L"25"},
    {0x40, L"50"},     {0x43, L"64"},     {0x45, L"80"},      {0x48, L"100"},     {0x4B, L"125"},
    {0x4D, L"160"},    {0x50, L"200"},    {0x53, L"250"},     {0x55, L"320"},     {0x58, L"400"},
    {0x5B, L"500"},    {0x5D, L"640"},    {0x60, L"800"},     {0x63, L"1000"},    {0x65, L"1250"},
    {0x68, L"1600"},   {0x6B, L"2000"},   {0x6D, L"2500"},    {0x70, L"3200"},    {0x73, L"4000"},
    {0x75, L"5000"},   {0x78, L"6400"},   {0x7B, L"8000"},    {0x7D, L"10000"},   {0x80, L"12800"},
    {0x83, L"16000"},  {0x85, L"20000"},  {0x88, L"25600"},   {0x8B, L"32000"},   {0x8D, L"40000"},
    {0x90, L"51200"},  {0x93, L"64000"},  {0x95, L"80000"},   {0x98, L"102400"},  {0xA0, L"204800"},
};

const Entry kWhiteBalanceLabels[] = {
    {0, L"Auto"},        {1, L"Daylight"},    {2, L"Cloudy"},     {3, L"Tungsten"},
    {4, L"Fluorescent"}, {5, L"Flash"},       {6, L"Custom"},     {8, L"Shade"},
    {9, L"Kelvin"},      {10, L"PC-1"},       {11, L"PC-2"},      {12, L"PC-3"},
    {15, L"Custom 2"},   {16, L"Custom 3"},   {18, L"Custom 4"},  {19, L"Custom 5"},
    {20, L"PC-4"},       {21, L"PC-5"},       {23, L"Auto (white priority)"},
};

const Entry kPictureStyleLabels[] = {
    {0x81, L"Standard"}, {0x82, L"Portrait"},  {0x83, L"Landscape"},   {0x84, L"Neutral"},
    {0x85, L"Faithful"}, {0x86, L"Monochrome"}, {0x87, L"Auto"},       {0x88, L"Fine Detail"},
    {0x21, L"User Def. 1"}, {0x22, L"User Def. 2"}, {0x23, L"User Def. 3"},
};

const Entry kExposureModeLabels[] = {
    {0x00, L"P"},  {0x01, L"Tv"}, {0x02, L"Av"}, {0x03, L"M"},   {0x04, L"Bulb"}, {0x07, L"Custom"},
    {0x10, L"C2"}, {0x11, L"C3"}, {0x14, L"Movie"}, {0x16, L"Auto"}, {0x37, L"Fv"},
};

const Entry kFocusModeLabels[] = {
    {0, L"One Shot"}, {1, L"Servo"}, {2, L"AI Focus"}, {3, L"Manual (lens switch)"},
};

const Entry kAfMethodLabels[] = {
    {0, L"Quick"}, {1, L"1-point"}, {2, L"Face + Tracking"}, {3, L"Multi"}, {4, L"Zone"},
    {5, L"Expand (cross)"}, {6, L"Expand (surround)"}, {7, L"Large Zone H"},
    {8, L"Large Zone V"}, {9, L"Catch AF"}, {10, L"Spot AF"}, {14, L"Whole Area"},
};

}  // namespace

Width WidthOf(uint16_t prop) {
    switch (prop) {
        case kAperture: case kShutterSpeed: case kIso: case kExposureMode: return Width::U16;
        case kExposureComp: return Width::S8;
        case kWhiteBalance: case kPictureStyle: return Width::U8;
        default: return Width::U32;
    }
}

uint32_t Normalize(uint16_t prop, uint32_t raw) {
    switch (WidthOf(prop)) {
        case Width::U8: case Width::S8: return raw & 0xFF;
        case Width::U16: return raw & 0xFFFF;
        default: return raw;
    }
}

const wchar_t* PropTitle(uint16_t prop) {
    switch (prop) {
        case kAperture: return L"Aperture";
        case kShutterSpeed: return L"Shutter speed";
        case kIso: return L"ISO";
        case kExposureComp: return L"Exposure compensation";
        case kExposureMode: return L"Shooting mode";
        case kFocusMode: return L"Focus mode";
        case kWhiteBalance: return L"White balance";
        case kColorTemperature: return L"Color temperature";
        case kPictureStyle: return L"Picture style";
        case kMovieServoAf: return L"Continuous autofocus";
        case kAfMethod: return L"AF method";
        default: return L"?";
    }
}

std::wstring ValueLabel(uint16_t prop, uint32_t value) {
    value = Normalize(prop, value);
    const wchar_t* s = nullptr;
    switch (prop) {
        case kAperture: s = Find(kAperturesLabels, value); break;
        case kShutterSpeed: s = Find(kShutterLabels, value); break;
        case kIso: s = Find(kIsoLabels, value); break;
        case kWhiteBalance: s = Find(kWhiteBalanceLabels, value); break;
        case kPictureStyle: s = Find(kPictureStyleLabels, value); break;
        case kExposureMode: s = Find(kExposureModeLabels, value); break;
        case kFocusMode: s = Find(kFocusModeLabels, value); break;
        case kAfMethod: s = Find(kAfMethodLabels, value); break;
        case kMovieServoAf: return value ? L"On" : L"Off";
        case kColorTemperature: return std::to_wstring(value) + L"K";
        case kExposureComp: {
            // Signed eighths of a stop; thirds are encoded as 3/8 and 5/8.
            int v = static_cast<int8_t>(value);
            if (v == 0) return L"0";
            int a = v < 0 ? -v : v, whole = a / 8, frac = a % 8;
            const wchar_t* f = frac == 3 ? L"⅓" : frac == 4 ? L"½" : frac == 5 ? L"⅔" : L"";
            wchar_t buf[32];
            if (whole && *f) swprintf_s(buf, L"%lc%d %ls", v < 0 ? L'−' : L'+', whole, f);
            else if (whole) swprintf_s(buf, L"%lc%d", v < 0 ? L'−' : L'+', whole);
            else swprintf_s(buf, L"%lc%ls", v < 0 ? L'−' : L'+', f);
            return buf;
        }
    }
    if (s) return s;
    wchar_t buf[16];
    swprintf_s(buf, L"0x%X", value);
    return buf;
}

}  // namespace eos
