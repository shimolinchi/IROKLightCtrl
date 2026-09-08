#pragma once

#include "Common.h"

#include <wrl/client.h>

namespace als {

class AuraController final {
public:
    AuraController() = default;
    ~AuraController();

    AuraController(const AuraController&) = delete;
    AuraController& operator=(const AuraController&) = delete;

    void Start();
    void Stop();
    void SubmitColor(RgbColor color);

    [[nodiscard]] bool IsReady() const noexcept { return ready_.load(); }
    [[nodiscard]] int DeviceCount() const noexcept { return deviceCount_.load(); }
    [[nodiscard]] std::wstring Status() const;

private:
    void ThreadMain();
    void SetStatus(const std::wstring& value);

    std::thread thread_;
    std::atomic<bool> stop_{};
    std::atomic<bool> ready_{};
    std::atomic<int> deviceCount_{};
    std::atomic<std::uint32_t> latestColor_{};
    std::atomic<std::uint64_t> generation_{};
    mutable std::mutex statusMutex_;
    std::wstring status_{L"Not started"};
};

}  // namespace als
