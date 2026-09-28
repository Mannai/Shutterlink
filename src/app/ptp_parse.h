// Copyright (C) 2026 Mannai
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Little-endian reader for PTP datasets.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

class PtpReader {
public:
    explicit PtpReader(const std::vector<uint8_t>& d, size_t pos = 0) : d_(d), pos_(pos) {}

    bool ok() const { return ok_; }
    size_t pos() const { return pos_; }
    void seek(size_t pos) { pos_ = pos; }
    size_t remaining() const { return pos_ <= d_.size() ? d_.size() - pos_ : 0; }

    template <typename T>
    T get() {
        T v{};
        if (remaining() < sizeof(T)) { ok_ = false; pos_ = d_.size(); return v; }
        for (size_t i = 0; i < sizeof(T); ++i) v |= static_cast<T>(static_cast<T>(d_[pos_ + i]) << (8 * i));
        pos_ += sizeof(T);
        return v;
    }

    std::wstring str() {
        uint8_t n = get<uint8_t>();
        std::wstring s;
        for (uint8_t i = 0; i < n; ++i) {
            wchar_t c = static_cast<wchar_t>(get<uint16_t>());
            if (c) s.push_back(c);
        }
        return s;
    }

    template <typename T>
    std::vector<T> array() {
        uint32_t n = get<uint32_t>();
        std::vector<T> v;
        if (n > remaining() / sizeof(T)) { ok_ = false; return v; }
        v.reserve(n);
        for (uint32_t i = 0; i < n; ++i) v.push_back(get<T>());
        return v;
    }

private:
    const std::vector<uint8_t>& d_;
    size_t pos_;
    bool ok_ = true;
};
