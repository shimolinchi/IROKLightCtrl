#pragma once

#include "Common.h"

#include <fstream>

namespace als {

class Logger final {
public:
    static Logger& Instance();

    void Initialize();
    void Info(const std::wstring& message);
    void Error(const std::wstring& message);
    [[nodiscard]] const std::filesystem::path& Path() const noexcept { return path_; }

private:
    Logger() = default;
    void Write(const wchar_t* level, const std::wstring& message);

    std::mutex mutex_;
    std::filesystem::path path_;
};

}  // namespace als
