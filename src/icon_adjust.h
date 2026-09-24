#pragma once
#include "icon_utils.h"
#include <cstdint>
#include <string>

/// Colour tweaks for a picked icon. Default-constructed = no change.
struct IconAdjust {
    float hue        = 0;    // degrees, -180..180
    float saturation = 100;  // percent, 0..200
    float brightness = 0;    // -100..100
    float contrast   = 100;  // percent, 0..200
    float tintAmount = 0;    // percent, 0..100: recolour towards tintColor, keeping light/shade
    uint32_t tintColor = 0xFF3D8AFF; // 0xAABBGGRR (ImGui IM_COL32 order)

    bool IsIdentity() const;
    /// Short stable text for cache keys / file names ("h30s120b0c100t0").
    std::string Key() const;
};

/// Apply in place: tint, then hue/saturation, then brightness, then contrast.
/// Alpha is untouched.
void ApplyAdjust(Image& img, const IconAdjust& adj);
