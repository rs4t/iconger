#include "icon_adjust.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

bool IconAdjust::IsIdentity() const
{
    return hue == 0 && saturation == 100 && brightness == 0 && contrast == 100 && tintAmount == 0;
}

std::string IconAdjust::Key() const
{
    char buf[96];
    snprintf(buf, sizeof(buf), "h%.0fs%.0fb%.0fc%.0ft%.0f-%08x", hue, saturation, brightness, contrast,
             tintAmount, tintAmount > 0 ? tintColor : 0u);
    return buf;
}

namespace {

void RgbToHsl(float r, float g, float b, float& h, float& s, float& l)
{
    float mx = std::max({ r, g, b }), mn = std::min({ r, g, b });
    l = (mx + mn) * 0.5f;
    float d = mx - mn;
    if (d < 1e-6f) { h = s = 0; return; }
    s = l > 0.5f ? d / (2 - mx - mn) : d / (mx + mn);
    if (mx == r)      h = (g - b) / d + (g < b ? 6.0f : 0.0f);
    else if (mx == g) h = (b - r) / d + 2;
    else              h = (r - g) / d + 4;
    h /= 6;
}

float HueToRgb(float p, float q, float t)
{
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1.0f / 6) return p + (q - p) * 6 * t;
    if (t < 0.5f)     return q;
    if (t < 2.0f / 3) return p + (q - p) * (2.0f / 3 - t) * 6;
    return p;
}

void HslToRgb(float h, float s, float l, float& r, float& g, float& b)
{
    if (s < 1e-6f) { r = g = b = l; return; }
    float q = l < 0.5f ? l * (1 + s) : l + s - l * s;
    float p = 2 * l - q;
    r = HueToRgb(p, q, h + 1.0f / 3);
    g = HueToRgb(p, q, h);
    b = HueToRgb(p, q, h - 1.0f / 3);
}

float Clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

} // namespace

void ApplyAdjust(Image& img, const IconAdjust& adj)
{
    if (adj.IsIdentity() || img.empty()) return;

    const float hueShift = adj.hue / 360.0f;
    const float sat = adj.saturation / 100.0f;
    const float bright = adj.brightness / 100.0f;
    const float contrast = adj.contrast / 100.0f;
    const float tint = adj.tintAmount / 100.0f;
    float th = 0, ts = 0, tl = 0;
    RgbToHsl((adj.tintColor & 0xFF) / 255.0f, ((adj.tintColor >> 8) & 0xFF) / 255.0f,
             ((adj.tintColor >> 16) & 0xFF) / 255.0f, th, ts, tl);

    for (size_t i = 0; i + 3 < img.rgba.size(); i += 4) {
        if (img.rgba[i + 3] == 0) continue;
        float r = img.rgba[i] / 255.0f, g = img.rgba[i + 1] / 255.0f, b = img.rgba[i + 2] / 255.0f;
        float h, s, l;
        RgbToHsl(r, g, b, h, s, l);

        if (tint > 0) {
            // colourise: tint's hue + saturation, the pixel's own lightness
            float cr, cg, cb;
            HslToRgb(th, ts, l, cr, cg, cb);
            r += (cr - r) * tint; g += (cg - g) * tint; b += (cb - b) * tint;
            RgbToHsl(r, g, b, h, s, l);
        }
        if (hueShift != 0 || sat != 1) {
            h = std::fmod(h + hueShift + 1.0f, 1.0f);
            s = Clamp01(s * sat);
            HslToRgb(h, s, l, r, g, b);
        }
        if (bright != 0) {
            // lift towards white / press towards black, so highlights don't clip flat
            auto f = [bright](float c) { return bright > 0 ? c + (1 - c) * bright : c * (1 + bright); };
            r = f(r); g = f(g); b = f(b);
        }
        if (contrast != 1) {
            r = (r - 0.5f) * contrast + 0.5f; g = (g - 0.5f) * contrast + 0.5f; b = (b - 0.5f) * contrast + 0.5f;
        }
        img.rgba[i]     = (uint8_t)std::lround(Clamp01(r) * 255);
        img.rgba[i + 1] = (uint8_t)std::lround(Clamp01(g) * 255);
        img.rgba[i + 2] = (uint8_t)std::lround(Clamp01(b) * 255);
    }
}
