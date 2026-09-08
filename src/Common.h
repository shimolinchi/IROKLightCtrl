#pragma once

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace als {

struct RgbColor {
    std::uint8_t r{};
    std::uint8_t g{};
    std::uint8_t b{};

    [[nodiscard]] bool operator==(const RgbColor&) const = default;
    [[nodiscard]] std::uint32_t Packed() const noexcept {
        return (static_cast<std::uint32_t>(r) << 16U) |
               (static_cast<std::uint32_t>(g) << 8U) |
               static_cast<std::uint32_t>(b);
    }

    static RgbColor FromPacked(std::uint32_t value) noexcept {
        return {
            static_cast<std::uint8_t>((value >> 16U) & 0xffU),
            static_cast<std::uint8_t>((value >> 8U) & 0xffU),
            static_cast<std::uint8_t>(value & 0xffU),
        };
    }
};

enum class Sensitivity : int {
    Low = 0,
    Normal = 1,
    High = 2,
};

enum class LightingMode : int {
    Audio = 0,
    Static = 1,
    Breathing = 2,
    ColorCycle = 3,
};

enum class AudioColorMode : int {
    Spectrum = 0,
    GradientCycle = 1,
};

inline std::wstring HResultMessage(HRESULT result) {
    wchar_t* message = nullptr;
    const DWORD size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        static_cast<DWORD>(result),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<wchar_t*>(&message),
        0,
        nullptr);
    std::wstring text = size && message ? std::wstring(message, size) : L"Unknown error";
    if (message) {
        LocalFree(message);
    }
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n')) {
        text.pop_back();
    }
    return text;
}

inline std::wstring Win32Message(DWORD error = GetLastError()) {
    return HResultMessage(HRESULT_FROM_WIN32(error));
}

}  // namespace als
