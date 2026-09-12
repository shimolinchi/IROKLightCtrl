#include "Logger.h"

#include <iomanip>
#include <sstream>

namespace lightctrl {
namespace {

std::string ToUtf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int count = WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), count, nullptr, nullptr);
    return result;
}

}  // namespace

Logger& Logger::Instance() {
    static Logger logger;
    return logger;
}

void Logger::Initialize() {
    std::lock_guard lock(mutex_);
    path_ = UserLocalDataPath() / L"LightController" / L"LightController.log";
    std::filesystem::create_directories(path_.parent_path());
    std::error_code error;
    if (std::filesystem::exists(path_, error) && std::filesystem::file_size(path_, error) > 1024 * 1024) {
        std::filesystem::remove(path_.wstring() + L".old", error);
        std::filesystem::rename(path_, path_.wstring() + L".old", error);
    }
}

void Logger::Info(const std::wstring& message) {
    Write(L"INFO", message);
}

void Logger::Error(const std::wstring& message) {
    Write(L"ERROR", message);
}

void Logger::Write(const wchar_t* level, const std::wstring& message) {
    std::lock_guard lock(mutex_);
    if (path_.empty()) {
        return;
    }
    SYSTEMTIME time{};
    GetLocalTime(&time);
    std::wostringstream line;
    line << std::setfill(L'0') << L'[' << std::setw(4) << time.wYear << L'-'
         << std::setw(2) << time.wMonth << L'-' << std::setw(2) << time.wDay << L' '
         << std::setw(2) << time.wHour << L':' << std::setw(2) << time.wMinute << L':'
         << std::setw(2) << time.wSecond << L"] [" << level << L"] " << message << L'\n';
    std::ofstream stream(path_, std::ios::binary | std::ios::app);
    const auto utf8 = ToUtf8(line.str());
    stream.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
}

}  // namespace lightctrl
