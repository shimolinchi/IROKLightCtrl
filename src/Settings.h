#pragma once

#include "Common.h"

namespace als {

struct Settings {
    Sensitivity sensitivity{Sensitivity::Normal};
    LightingMode lightingMode{LightingMode::Audio};
    AudioColorMode audioColorMode{AudioColorMode::GradientCycle};
    RgbColor primaryColor{255, 48, 112};
    RgbColor secondaryColor{24, 176, 255};
    int effectSpeed{55};
    bool reverseDirection{false};
    int frameIntervalMs{50};
    int maxBrightness{100};
    bool keyboardEnabled{true};
    bool angryMiaoReceiverEnabled{true};
    bool dynamicLightingEnabled{true};
    bool auraFallbackEnabled{true};

    static Settings Load();
    void Save() const;
    static std::filesystem::path FilePath();
};

bool IsStartupEnabled();
bool SetStartupEnabled(bool enabled);

}  // namespace als
