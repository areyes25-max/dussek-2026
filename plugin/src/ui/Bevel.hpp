// NA_Magicos: the controls' 3D look, one place for every button, chip, field, switch and slider.
// Raised: lit from above (a lighter top, a darker foot), a hairline highlight at the top edge, a soft
// shadow under it and a dark outline; under the pointer a gold outline; on: gold lacquer.
#pragma once
#include "NanoVG.hpp"
#include "Theme.hpp"

#include <algorithm>

namespace pe::bevel {
using DGL_NAMESPACE::Color;
using DGL_NAMESPACE::NanoVG;

inline Color shade(const Color& c, float t) // t > 0 lighter, t < 0 darker
{
    return t >= 0 ? Color(c, Color(255, 255, 255), t) : Color(c, Color(0, 0, 0), -t);
}

/// a raised 3D key: `on` filled with gold lacquer, `hot` outlined in gold
inline void raised(NanoVG& vg, float x, float y, float w, float h, float r, bool on, bool hot)
{
    if (w <= 0 || h <= 0) return;
    const Color base = on ? theme::accent : Color(44, 34, 24);
    // the shadow it casts
    vg.beginPath(); vg.rect(x - 3, y, w + 6, h + 5);
    vg.fillPaint(vg.boxGradient(x, y + 1.5f, w, h, r, 4, Color(0, 0, 0, 0.55f), Color(0, 0, 0, 0.0f))); vg.fill();
    // the body, lit from above
    vg.beginPath(); vg.roundedRect(x, y, w, h, r);
    vg.fillPaint(vg.linearGradient(0, y, 0, y + h, shade(base, on ? 0.28f : 0.14f), shade(base, on ? -0.22f : -0.35f)));
    vg.fill();
    vg.strokeColor(hot ? Color(theme::gold, Color(255, 255, 255), 0.15f) : Color(0, 0, 0, 0.75f));
    vg.strokeWidth(hot ? 1.2f : 1.0f); vg.stroke();
    // the hairline of light along its top edge
    vg.beginPath(); vg.roundedRect(x + 1.5f, y + 1.0f, w - 3.0f, std::min(h * 0.45f, 8.0f), std::max(0.0f, r - 1.0f));
    vg.fillPaint(vg.linearGradient(0, y + 1, 0, y + std::min(h * 0.45f, 8.0f), Color(255, 255, 255, on ? 0.30f : 0.10f),
        Color(255, 255, 255, 0.0f)));
    vg.fill();
}

/// a sunken 3D well (a field being typed into, a slider's track, a switch's slot)
inline void sunken(NanoVG& vg, float x, float y, float w, float h, float r)
{
    if (w <= 0 || h <= 0) return;
    vg.beginPath(); vg.roundedRect(x, y, w, h, r);
    vg.fillPaint(vg.linearGradient(0, y, 0, y + h, Color(8, 6, 3), Color(52, 41, 27)));
    vg.fill();
    vg.strokeColor(Color(120, 92, 48, 0.55f)); vg.strokeWidth(1); vg.stroke();
    vg.beginPath(); vg.moveTo(x + r, y + h + 0.5f); vg.lineTo(x + w - r, y + h + 0.5f); // its lit lower lip
    vg.strokeColor(Color(255, 255, 255, 0.07f)); vg.stroke();
}

/// a slider's track: the sunken well, its filled part in gold lacquer
inline void track(NanoVG& vg, float x, float y, float w, float h, float norm, bool active)
{
    sunken(vg, x, y, w, h, h / 2);
    const float fw = std::max(0.0f, w * std::clamp(norm, 0.0f, 1.0f));
    if (fw < 1) return;
    vg.beginPath(); vg.roundedRect(x + 0.5f, y + 0.5f, fw - 1.0f, h - 1.0f, (h - 1) / 2);
    const Color c = active ? theme::accent : theme::marker;
    vg.fillPaint(vg.linearGradient(0, y, 0, y + h, shade(c, 0.30f), shade(c, -0.25f)));
    vg.fill();
}

/// a slider's knob: a small gold sphere, lit from the upper left, with its shadow
inline void knob(NanoVG& vg, float cx, float cy, float r, bool active, bool hot)
{
    vg.beginPath(); vg.circle(cx + 1, cy + 2, r + 2);
    vg.fillPaint(vg.radialGradient(cx + 1, cy + 2, r * 0.6f, r + 2, Color(0, 0, 0, 0.6f), Color(0, 0, 0, 0.0f))); vg.fill();
    const Color c = active ? (hot ? Color(theme::accent, Color(255, 255, 255), 0.25f) : theme::accent) : theme::marker;
    vg.beginPath(); vg.circle(cx, cy, r);
    vg.fillPaint(vg.radialGradient(cx - r * 0.35f, cy - r * 0.4f, r * 0.1f, r * 1.2f, shade(c, 0.55f), shade(c, -0.45f)));
    vg.fill();
    vg.strokeColor(Color(0, 0, 0, 0.7f)); vg.strokeWidth(1); vg.stroke();
}

} // namespace pe::bevel
