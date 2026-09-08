#pragma once

#include "Common.h"

namespace als {

struct Settings {
    Sensitivity sensitivity{Sensitivity::Normal};
    int frameIntervalMs{50};
    int maxBrightness{100};
    bool keyboardEnabled{true};
    bool dynamicLightingEnabled{true};
    bool auraFallbackEnabled{true};

    static Settings Load();
    void Save() const;
    static std::filesystem::path FilePath();
};

bool IsStartupEnabled();
bool SetStartupEnabled(bool enabled);

}  // namespace als
