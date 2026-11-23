#include "HomeView.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace pe {
using DGL_NAMESPACE::Color;
using DGL_NAMESPACE::NanoImage;
using DGL_NAMESPACE::NanoVG;

#include "HomeArt.inc"

namespace {

const Color kGold(232, 184, 92), kGoldHi(255, 222, 150), kGoldDeep(168, 112, 40), kInk(10, 8, 6), kText(238, 226, 205),
    kDim(170, 156, 132);

Color alpha(const Color& c, float a) { Color o = c; o.alpha = a; return o; }

/// a dark glass panel: see-through ink, a gold rim lit from above, a soft gold glow around it
void glass(NanoVG& vg, const Rect& r, float rad, float glow = 1.0f)
{
    vg.beginPath(); vg.rect(r.x - 18, r.y - 18, r.w + 36, r.h + 36);
    vg.fillPaint(vg.boxGradient(r.x, r.y, r.w, r.h, rad, 22, alpha(kGold, 0.16f * glow), alpha(kGold, 0.0f))); vg.fill();
    vg.beginPath(); vg.roundedRect(r.x, r.y, r.w, r.h, rad);
    vg.fillPaint(vg.linearGradient(0, r.y, 0, r.y + r.h, Color(22, 18, 14, 0.86f), Color(6, 5, 4, 0.90f))); vg.fill();
    vg.strokePaint(vg.linearGradient(0, r.y, 0, r.y + r.h, alpha(kGoldHi, 0.85f), alpha(kGoldDeep, 0.35f)));
    vg.strokeWidth(1.4f); vg.stroke();
    vg.beginPath(); vg.roundedRect(r.x + 1.5f, r.y + 1.5f, r.w - 3, std::min(r.h * 0.4f, 40.0f), rad - 1);
    vg.fillPaint(vg.linearGradient(0, r.y, 0, r.y + std::min(r.h * 0.4f, 40.0f), Color(255, 255, 255, 0.05f), Color(255, 255, 255, 0.0f)));
    vg.fill();
}

/// a gold lacquer pill (lit: filled gold; else dark glass with a gold rim)
void pill(NanoVG& vg, const Rect& r, bool lit, bool hot)
{
    const float rad = r.h / 2;
    if (lit) {
        vg.beginPath(); vg.rect(r.x - 14, r.y - 14, r.w + 28, r.h + 28);
        vg.fillPaint(vg.boxGradient(r.x, r.y, r.w, r.h, rad, 14, alpha(kGold, 0.45f), alpha(kGold, 0.0f))); vg.fill();
    }
    vg.beginPath(); vg.roundedRect(r.x, r.y, r.w, r.h, rad);
    vg.fillPaint(lit ? vg.linearGradient(0, r.y, 0, r.y + r.h, Color(255, 214, 128), Color(196, 132, 44))
                     : vg.linearGradient(0, r.y, 0, r.y + r.h, Color(34, 28, 22, 0.92f), Color(10, 8, 6, 0.92f)));
    vg.fill();
    vg.strokeColor(lit ? alpha(kGoldHi, 0.95f) : hot ? alpha(kGoldHi, 0.9f) : alpha(kGold, 0.45f));
    vg.strokeWidth(hot ? 1.6f : 1.1f); vg.stroke();
    vg.beginPath(); vg.roundedRect(r.x + 2, r.y + 1.5f, r.w - 4, r.h * 0.45f, rad - 2);
    vg.fillPaint(vg.linearGradient(0, r.y, 0, r.y + r.h * 0.45f, Color(255, 255, 255, lit ? 0.35f : 0.07f), Color(255, 255, 255, 0.0f)));
    vg.fill();
}

void text(NanoVG& vg, float x, float y, float size, const char* s, const Color& c, int align)
{
    vg.fontSize(size);
    vg.textAlign(NanoVG::Align(align));
    vg.fillColor(c);
    vg.text(x, y, s, nullptr);
}
constexpr int kCenter = NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, kLeft = NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE,
              kRight = NanoVG::ALIGN_RIGHT | NanoVG::ALIGN_MIDDLE;

void stroke(NanoVG& vg, const Color& c, float w)
{
    vg.strokeColor(c); vg.strokeWidth(w); vg.lineCap(NanoVG::ROUND); vg.lineJoin(NanoVG::ROUND); vg.stroke();
}

// ---- icons (centered at x, y, about s tall), drawn in c
void iconHome(NanoVG& vg, float x, float y, float s, const Color& c)
{
    vg.beginPath(); vg.moveTo(x - s * 0.55f, y - s * 0.02f); vg.lineTo(x, y - s * 0.5f); vg.lineTo(x + s * 0.55f, y - s * 0.02f);
    stroke(vg, c, s * 0.12f);
    vg.beginPath(); vg.rect(x - s * 0.38f, y - s * 0.05f, s * 0.76f, s * 0.5f); vg.fillColor(c); vg.fill();
    vg.beginPath(); vg.rect(x - s * 0.1f, y + s * 0.15f, s * 0.2f, s * 0.3f); vg.fillColor(kInk); vg.fill();
}
void iconPiano(NanoVG& vg, float x, float y, float s, const Color& c)
{
    vg.beginPath(); vg.roundedRect(x - s * 0.5f, y - s * 0.42f, s, s * 0.84f, s * 0.08f); stroke(vg, c, s * 0.09f);
    for (int i = 1; i < 4; ++i) { vg.beginPath(); vg.moveTo(x - s * 0.5f + i * s / 4, y - s * 0.42f); vg.lineTo(x - s * 0.5f + i * s / 4, y + s * 0.42f); stroke(vg, c, s * 0.06f); }
    for (int i = 0; i < 3; ++i) { vg.beginPath(); vg.rect(x - s * 0.5f + (i + 1) * s / 4 - s * 0.07f, y - s * 0.42f, s * 0.14f, s * 0.45f); vg.fillColor(c); vg.fill(); }
}
void iconWave(NanoVG& vg, float x, float y, float s, const Color& c)
{
    static const float hs[] = { 0.25f, 0.55f, 0.85f, 1.0f, 0.7f, 0.45f, 0.2f };
    for (int i = 0; i < 7; ++i) {
        const float bx = x - s * 0.54f + i * s * 0.18f, bh = s * hs[i];
        vg.beginPath(); vg.moveTo(bx, y - bh / 2); vg.lineTo(bx, y + bh / 2); stroke(vg, c, s * 0.09f);
    }
}
void iconSliders(NanoVG& vg, float x, float y, float s, const Color& c)
{
    const float knobs[] = { 0.15f, -0.2f, 0.05f };
    for (int i = 0; i < 3; ++i) {
        const float bx = x - s * 0.36f + i * s * 0.36f;
        vg.beginPath(); vg.moveTo(bx, y - s * 0.5f); vg.lineTo(bx, y + s * 0.5f); stroke(vg, c, s * 0.07f);
        vg.beginPath(); vg.roundedRect(bx - s * 0.12f, y + knobs[i] * s - s * 0.07f, s * 0.24f, s * 0.14f, s * 0.03f); vg.fillColor(c); vg.fill();
    }
}
void iconHall(NanoVG& vg, float x, float y, float s, const Color& c)
{
    vg.beginPath(); vg.moveTo(x - s * 0.6f, y - s * 0.22f); vg.lineTo(x, y - s * 0.5f); vg.lineTo(x + s * 0.6f, y - s * 0.22f); vg.closePath();
    vg.fillColor(c); vg.fill();
    for (int i = 0; i < 5; ++i) { vg.beginPath(); vg.rect(x - s * 0.5f + i * s * 0.23f, y - s * 0.14f, s * 0.08f, s * 0.48f); vg.fillColor(c); vg.fill(); }
    vg.beginPath(); vg.rect(x - s * 0.62f, y + s * 0.38f, s * 1.24f, s * 0.1f); vg.fillColor(c); vg.fill();
}
void iconGear(NanoVG& vg, float x, float y, float s, const Color& c)
{
    vg.beginPath();
    for (int i = 0; i < 16; ++i) {
        const float a = (float)i / 16 * 2 * (float)M_PI, r = (i % 2 == 0) ? s * 0.5f : s * 0.38f;
        const float a2 = a + 2 * (float)M_PI / 32;
        if (i == 0) vg.moveTo(x + std::cos(a) * r, y + std::sin(a) * r); else vg.lineTo(x + std::cos(a) * r, y + std::sin(a) * r);
        vg.lineTo(x + std::cos(a2) * r, y + std::sin(a2) * r);
    }
    vg.closePath(); vg.fillColor(c); vg.fill();
    vg.beginPath(); vg.circle(x, y, s * 0.17f); vg.fillColor(kInk); vg.fill();
}
void iconBigger(NanoVG& vg, float x, float y, float s, const Color& c, bool in)
{
    // four corners: pointing out (make larger) or in (back to the usual size)
    const float a = s * 0.5f, b = s * 0.24f;
    for (int i = 0; i < 4; ++i) {
        const float dx = (i & 1) ? 1.0f : -1.0f, dy = (i & 2) ? 1.0f : -1.0f;
        const float cx = in ? x + dx * s * 0.14f : x + dx * a, cy = in ? y + dy * s * 0.14f : y + dy * a;
        const float sx = in ? dx : -dx, sy = in ? dy : -dy; // the arms point back to the middle (out) or away (in)
        vg.beginPath(); vg.moveTo(cx, cy + sy * b); vg.lineTo(cx, cy); vg.lineTo(cx + sx * b, cy);
        stroke(vg, c, s * 0.11f);
    }
}
void iconSpeaker(NanoVG& vg, float x, float y, float s, const Color& c)
{
    vg.beginPath(); vg.moveTo(x - s * 0.5f, y - s * 0.15f); vg.lineTo(x - s * 0.25f, y - s * 0.15f); vg.lineTo(x + s * 0.05f, y - s * 0.42f);
    vg.lineTo(x + s * 0.05f, y + s * 0.42f); vg.lineTo(x - s * 0.25f, y + s * 0.15f); vg.lineTo(x - s * 0.5f, y + s * 0.15f); vg.closePath();
    vg.fillColor(c); vg.fill();
    for (int i = 1; i <= 2; ++i) {
        vg.beginPath(); vg.arc(x + s * 0.05f, y, s * 0.2f * i, -0.8f, 0.8f, NanoVG::CW); stroke(vg, c, s * 0.08f);
    }
}
void iconCrown(NanoVG& vg, float x, float y, float s, const Color& c)
{
    vg.beginPath(); vg.moveTo(x - s * 0.55f, y + s * 0.3f); vg.lineTo(x - s * 0.6f, y - s * 0.3f); vg.lineTo(x - s * 0.25f, y);
    vg.lineTo(x, y - s * 0.45f); vg.lineTo(x + s * 0.25f, y); vg.lineTo(x + s * 0.6f, y - s * 0.3f); vg.lineTo(x + s * 0.55f, y + s * 0.3f);
    vg.closePath(); vg.fillPaint(vg.linearGradient(0, y - s * 0.45f, 0, y + s * 0.3f, kGoldHi, kGoldDeep)); vg.fill();
    vg.beginPath(); vg.rect(x - s * 0.55f, y + s * 0.36f, s * 1.1f, s * 0.12f); vg.fillColor(c); vg.fill();
}
void iconNote(NanoVG& vg, float x, float y, float s, const Color& c)
{
    vg.beginPath(); vg.ellipse(x - s * 0.22f, y + s * 0.3f, s * 0.2f, s * 0.15f); vg.fillColor(c); vg.fill();
    vg.beginPath(); vg.ellipse(x + s * 0.32f, y + s * 0.2f, s * 0.2f, s * 0.15f); vg.fillColor(c); vg.fill();
    vg.beginPath(); vg.moveTo(x - s * 0.04f, y + s * 0.3f); vg.lineTo(x - s * 0.04f, y - s * 0.42f); vg.lineTo(x + s * 0.5f, y - s * 0.52f);
    vg.lineTo(x + s * 0.5f, y + s * 0.2f); stroke(vg, c, s * 0.09f);
}
void iconGrid(NanoVG& vg, float x, float y, float s, const Color& c)
{
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) { vg.beginPath(); vg.roundedRect(x - s * 0.48f + i * s * 0.34f, y - s * 0.48f + j * s * 0.34f, s * 0.26f, s * 0.26f, s * 0.04f); vg.fillColor(c); vg.fill(); }
}
void iconPlay(NanoVG& vg, float x, float y, float s, const Color& c)
{
    vg.beginPath(); vg.moveTo(x - s * 0.35f, y - s * 0.45f); vg.lineTo(x + s * 0.45f, y); vg.lineTo(x - s * 0.35f, y + s * 0.45f); vg.closePath();
    vg.fillPaint(vg.linearGradient(0, y - s * 0.45f, 0, y + s * 0.45f, kGoldHi, kGoldDeep)); vg.fill(); (void)c;
}
void iconBolt(NanoVG& vg, float x, float y, float s, const Color& c)
{
    vg.beginPath(); vg.moveTo(x + s * 0.12f, y - s * 0.55f); vg.lineTo(x - s * 0.32f, y + s * 0.08f); vg.lineTo(x - s * 0.02f, y + s * 0.08f);
    vg.lineTo(x - s * 0.14f, y + s * 0.55f); vg.lineTo(x + s * 0.32f, y - s * 0.1f); vg.lineTo(x + s * 0.02f, y - s * 0.1f); vg.closePath();
    vg.fillColor(c); vg.fill();
}
void iconKeys(NanoVG& vg, float x, float y, float s, const Color& c)
{
    vg.beginPath(); vg.roundedRect(x - s * 0.55f, y - s * 0.4f, s * 1.1f, s * 0.8f, s * 0.06f); vg.fillColor(c); vg.fill();
    for (int i = 0; i < 4; ++i) { vg.beginPath(); vg.rect(x - s * 0.36f + i * s * 0.24f, y - s * 0.4f, s * 0.12f, s * 0.42f); vg.fillColor(kInk); vg.fill(); }
}
/// a category's pictogram: a harpsichord's wing, a harp, mallets, … (generic line drawings)
void iconFamily(NanoVG& vg, const std::string& group, float x, float y, float s, const Color& c)
{
    if (group.find("Upright") != std::string::npos) {
        vg.beginPath(); vg.roundedRect(x - s * 0.5f, y - s * 0.45f, s, s * 0.7f, s * 0.05f); stroke(vg, c, s * 0.07f);
        vg.beginPath(); vg.rect(x - s * 0.5f, y + s * 0.05f, s, s * 0.08f); vg.fillColor(c); vg.fill();
        vg.beginPath(); vg.moveTo(x - s * 0.42f, y + s * 0.25f); vg.lineTo(x - s * 0.42f, y + s * 0.45f); vg.moveTo(x + s * 0.42f, y + s * 0.25f);
        vg.lineTo(x + s * 0.42f, y + s * 0.45f); stroke(vg, c, s * 0.07f);
    } else if (group.find("Harpsichord") != std::string::npos || group.find("Clavichord") != std::string::npos || group.find("Grand") != std::string::npos) {
        vg.beginPath(); vg.moveTo(x - s * 0.55f, y - s * 0.05f); vg.lineTo(x - s * 0.55f, y + s * 0.12f); vg.lineTo(x + s * 0.55f, y + s * 0.12f);
        vg.bezierTo(x + s * 0.55f, y - s * 0.2f, x + s * 0.2f, y - s * 0.1f, x + s * 0.05f, y - s * 0.3f); vg.lineTo(x - s * 0.55f, y - s * 0.05f);
        stroke(vg, c, s * 0.07f);
        vg.beginPath(); vg.moveTo(x - s * 0.45f, y + s * 0.12f); vg.lineTo(x - s * 0.45f, y + s * 0.45f); vg.moveTo(x + s * 0.45f, y + s * 0.12f);
        vg.lineTo(x + s * 0.45f, y + s * 0.45f); stroke(vg, c, s * 0.07f);
    } else if (group.find("Harp") != std::string::npos) {
        vg.beginPath(); vg.moveTo(x - s * 0.35f, y + s * 0.5f); vg.lineTo(x - s * 0.35f, y - s * 0.5f);
        vg.bezierTo(x + s * 0.1f, y - s * 0.2f, x + s * 0.35f, y - s * 0.35f, x + s * 0.4f, y - s * 0.05f); vg.lineTo(x - s * 0.35f, y + s * 0.5f);
        stroke(vg, c, s * 0.07f);
        for (int i = 1; i < 5; ++i) { const float sx = x - s * 0.35f + i * s * 0.14f; vg.beginPath(); vg.moveTo(sx, y - s * 0.38f + i * 0.02f * s); vg.lineTo(sx, y + s * 0.45f - i * s * 0.17f); stroke(vg, c, s * 0.03f); }
    } else if (group.find("Guitar") != std::string::npos) {
        vg.beginPath(); vg.circle(x - s * 0.15f, y + s * 0.2f, s * 0.25f); stroke(vg, c, s * 0.07f);
        vg.beginPath(); vg.moveTo(x + s * 0.02f, y + s * 0.02f); vg.lineTo(x + s * 0.45f, y - s * 0.45f); stroke(vg, c, s * 0.09f);
    } else { // mallets and the rest: two mallets over bars
        for (int i = 0; i < 4; ++i) { vg.beginPath(); vg.rect(x - s * 0.5f + i * s * 0.27f, y + s * 0.05f - i * s * 0.05f, s * 0.2f, s * 0.38f + i * s * 0.05f); vg.fillColor(c); vg.fill(); }
        vg.beginPath(); vg.moveTo(x - s * 0.3f, y - s * 0.05f); vg.lineTo(x + s * 0.1f, y - s * 0.5f); stroke(vg, c, s * 0.05f);
        vg.beginPath(); vg.circle(x + s * 0.12f, y - s * 0.52f, s * 0.09f); vg.fillColor(c); vg.fill();
    }
}

/// a knob: a dark metal cap in a gold ring lit as far as its value, a notch pointing at it
void knob(NanoVG& vg, float cx, float cy, float r, float v, bool hot)
{
    const float a0 = 0.75f * (float)M_PI, a1 = 2.25f * (float)M_PI, av = a0 + (a1 - a0) * std::clamp(v, 0.0f, 1.0f);
    vg.beginPath(); vg.circle(cx, cy + r * 0.08f, r * 1.25f);
    vg.fillPaint(vg.radialGradient(cx, cy, r * 0.9f, r * 1.3f, Color(0, 0, 0, 0.6f), Color(0, 0, 0, 0.0f))); vg.fill();
    vg.beginPath(); vg.arc(cx, cy, r * 1.08f, a0, a1, NanoVG::CW); stroke(vg, Color(40, 32, 22), r * 0.14f);
    vg.beginPath(); vg.arc(cx, cy, r * 1.08f, a0, av, NanoVG::CW);
    stroke(vg, hot ? kGoldHi : kGold, r * 0.14f);
    vg.beginPath(); vg.arc(cx, cy, r * 1.08f, a0, av, NanoVG::CW); stroke(vg, alpha(kGold, 0.25f), r * 0.45f); // its glow
    vg.beginPath(); vg.circle(cx, cy, r * 0.9f);
    vg.fillPaint(vg.radialGradient(cx - r * 0.3f, cy - r * 0.35f, r * 0.1f, r * 1.1f, Color(96, 92, 88), Color(14, 13, 12))); vg.fill();
    vg.strokeColor(Color(0, 0, 0, 0.8f)); vg.strokeWidth(1); vg.stroke();
    for (int i = 0; i < 3; ++i) { // brushed rings
        vg.beginPath(); vg.circle(cx, cy, r * (0.3f + 0.18f * i)); vg.strokeColor(Color(255, 255, 255, 0.035f)); vg.strokeWidth(1); vg.stroke();
    }
    vg.beginPath(); vg.moveTo(cx + std::cos(av) * r * 0.45f, cy + std::sin(av) * r * 0.45f); vg.lineTo(cx + std::cos(av) * r * 0.8f, cy + std::sin(av) * r * 0.8f);
    stroke(vg, kGoldHi, r * 0.1f);
}

/// a switch: a gold pill with its white knob right when on; dimmed when it is not available yet
void toggle(NanoVG& vg, const Rect& r, bool on, bool enabled, bool hot)
{
    vg.beginPath(); vg.roundedRect(r.x, r.y, r.w, r.h, r.h / 2);
    vg.fillPaint(on ? vg.linearGradient(0, r.y, 0, r.y + r.h, Color(255, 210, 120), Color(200, 136, 48))
                    : vg.linearGradient(0, r.y, 0, r.y + r.h, Color(20, 16, 12), Color(48, 40, 30)));
    vg.fill();
    vg.strokeColor(hot && enabled ? kGoldHi : alpha(kGold, enabled ? 0.6f : 0.25f)); vg.strokeWidth(1); vg.stroke();
    const float d = r.h - 4, kx = on ? r.x + r.w - 2 - d / 2 : r.x + 2 + d / 2;
    vg.beginPath(); vg.circle(kx, r.y + r.h / 2, d / 2);
    vg.fillPaint(vg.radialGradient(kx - d * 0.15f, r.y + r.h / 2 - d * 0.15f, 1, d * 0.6f, Color(255, 255, 255), Color(208, 200, 188)));
    vg.fill();
    if (!enabled) { vg.beginPath(); vg.roundedRect(r.x, r.y, r.w, r.h, r.h / 2); vg.fillColor(Color(0, 0, 0, 0.45f)); vg.fill(); }
}

void image(NanoVG& vg, const NanoImage* img, const Rect& r, float a = 1.0f)
{
    if (img == nullptr || !img->isValid()) return;
    vg.beginPath(); vg.rect(r.x, r.y, r.w, r.h);
    vg.fillPaint(vg.imagePattern(r.x, r.y, r.w, r.h, 0, *img, a)); vg.fill();
}

/// an image covering r (cropped to its aspect), its edges faded into the background
void cover(NanoVG& vg, const NanoImage* img, const Rect& r, float fade)
{
    if (img == nullptr || !img->isValid()) return;
    const auto sz = img->getSize();
    const float ia = (float)sz.getWidth() / (float)sz.getHeight();
    // the whole picture shown (a little larger than fits: its faded edges go under the panels)
    float w = r.w * 1.18f, h = w / ia;
    if (h > r.h * 1.05f) { h = r.h * 1.05f; w = h * ia; }
    const float x = r.x + (r.w - w) / 2, y = r.y + (r.h - h) / 2;
    vg.save(); vg.intersectScissor(r.x, r.y, r.w, r.h);
    vg.beginPath(); vg.rect(r.x, r.y, r.w, r.h); vg.fillPaint(vg.imagePattern(x, y, w, h, 0, *img, 1)); vg.fill();
    // the edges: into the window's ink
    const Color in(4, 3, 2, 1.0f), out(4, 3, 2, 0.0f);
    vg.beginPath(); vg.rect(r.x, r.y, fade, r.h); vg.fillPaint(vg.linearGradient(r.x, 0, r.x + fade, 0, in, out)); vg.fill();
    vg.beginPath(); vg.rect(r.x + r.w - fade, r.y, fade, r.h); vg.fillPaint(vg.linearGradient(r.x + r.w, 0, r.x + r.w - fade, 0, in, out)); vg.fill();
    vg.beginPath(); vg.rect(r.x, r.y, r.w, fade * 0.6f); vg.fillPaint(vg.linearGradient(0, r.y, 0, r.y + fade * 0.6f, in, out)); vg.fill();
    vg.beginPath(); vg.rect(r.x, r.y + r.h - fade * 0.5f, r.w, fade * 0.5f); vg.fillPaint(vg.linearGradient(0, r.y + r.h, 0, r.y + r.h - fade * 0.5f, in, out)); vg.fill();
    vg.restore();
}

std::string shortGroup(const std::string& g)
{
    if (g == "Grand pianos") return "Concert Grand";
    if (g == "Upright pianos") return "Studio Upright";
    if (g == "Harpsichords") return "Harpsichord";
    if (g == "Clavichords") return "Clavichord";
    if (g == "Harps") return "Harp";
    if (g == "Guitars") return "Guitar";
    if (g == "Thumb pianos") return "Thumb Piano";
    if (g == "Zithers") return "Zither";
    return g;
}

} // namespace

void HomeView::layout(const HomeModel& m)
{
    const float W = m.w, H = m.h;
    u = std::clamp(std::min(W / 1100.0f, H / 620.0f), 0.62f, 2.0f);
    const float pad = 14 * u, topH = 64 * u, barH = 52 * u;
    fTop = { 0, 0, W, topH + pad };
    // the logo (its picture's aspect), the navigation centered, the volume and the two buttons right
    const float logoH = topH * 1.02f;
    fLogo = { pad * 0.6f, pad * 0.35f, logoH * (396.0f / 128.0f), logoH };
    const float navW = std::min(W * 0.40f, 520 * u), navH = topH * 0.86f;
    fNav = { W / 2 - navW / 2, pad * 0.6f, navW, navH };
    for (int i = 0; i < 5; ++i)
        fRect[home::NavHome + i] = { fNav.x + 6 * u + i * (navW - 12 * u) / 5, fNav.y + 5 * u, (navW - 12 * u) / 5, navH - 10 * u };
    const float bs = navH * 0.78f, rightX = W - pad;
    fRect[home::Bigger] = { rightX - bs, fNav.y + (navH - bs) / 2, bs, bs };
    fRect[home::Settings] = { rightX - 2 * bs - 10 * u, fNav.y + (navH - bs) / 2, bs, bs };
    const float volW = std::min(250 * u, fRect[home::Settings].x - 12 * u - (fNav.x + navW + 12 * u));
    fRect[home::Volume] = { fRect[home::Settings].x - 12 * u - volW, fNav.y + (navH - bs) / 2, std::max(0.0f, volW), bs };
    // the panels and the stage between them
    const float y0 = topH + pad * 1.6f, y1 = H - barH - pad * 1.2f;
    const float leftW = std::min(W * 0.31f, 560 * u), rightW = std::min(W * 0.255f, 440 * u);
    fLeft = { pad, y0, leftW, y1 - y0 };
    fRight = { W - pad - rightW, y0, rightW, y1 - y0 };
    fStage = { fLeft.x + fLeft.w * 0.6f, topH * 0.75f, (fRight.x + fRight.w * 0.4f) - (fLeft.x + fLeft.w * 0.6f), y1 + barH * 0.5f - topH * 0.75f };
    // left: the header line, the featured card, the four category cards
    const float hh = 40 * u, inner = 12 * u;
    fRect[home::SeeAll] = { fLeft.x + fLeft.w - inner - 72 * u, fLeft.y + (hh - 24 * u) / 2 + 4 * u, 72 * u, 24 * u };
    const float cardsW = std::min(fLeft.w * 0.27f, 120 * u);
    fFeature = { fLeft.x + inner + 18 * u, fLeft.y + hh + 8 * u, fLeft.w - 3 * inner - cardsW - 36 * u, fLeft.h - hh - 8 * u - inner };
    const float ar = 15 * u;
    fRect[home::InstPrev] = { fLeft.x + inner + 2 * u, fFeature.y + fFeature.h * 0.45f - ar, 2 * ar, 2 * ar };
    fRect[home::InstNext] = { fFeature.x + fFeature.w - ar + 2 * u, fFeature.y + fFeature.h * 0.45f - ar, 2 * ar, 2 * ar };
    const float cx = fLeft.x + fLeft.w - inner - cardsW, gap = 8 * u, ch = (fFeature.h - 3 * gap) / 4;
    for (int i = 0; i < 4; ++i)
        fRect[home::Group0 + i] = { cx, fFeature.y + i * (ch + gap), cardsW, ch };
    // right: the audio controls above, the smart features below
    const float split = fRight.h * 0.47f;
    fAudio = { fRight.x, fRight.y, fRight.w, split };
    fSmart = { fRight.x + inner, fRight.y + split + 4 * u, fRight.w - 2 * inner, fRight.h - split - 4 * u - inner };
    const float kr = std::min(fAudio.w / 4 * 0.34f, fAudio.h * 0.2f);
    for (int i = 0; i < 4; ++i) {
        const float kx = fAudio.x + fAudio.w * (0.125f + 0.25f * i), ky = fAudio.y + hh + (fAudio.h - hh) * 0.42f;
        fRect[home::Knob0 + i] = { kx - kr * 1.3f, ky - kr * 1.3f, kr * 2.6f, kr * 2.6f + 26 * u };
    }
    const float rowH = (fSmart.h - hh) / 3;
    for (int i = 0; i < 3; ++i)
        fRect[home::SmartAuto + i] = { fSmart.x, fSmart.y + hh + i * rowH, fSmart.w, rowH };
    // the play bar
    fBar = { pad, H - barH - pad * 0.4f, W - 2 * pad, barH };
    const float bh = barH * 0.74f, by = fBar.y + (barH - bh) / 2, mw = std::min(140 * u, fBar.w * 0.12f);
    for (int i = 0; i < 3; ++i)
        fRect[home::ModeKeyboard + i] = { fBar.x + 8 * u + i * (mw + 10 * u), by, mw, bh };
    const float keyW = std::min(190 * u, fBar.w * 0.16f), trW = std::min(210 * u, fBar.w * 0.17f);
    const Rect key = { fBar.x + fBar.w - 8 * u - keyW, by, keyW, bh }, tr = { key.x - 12 * u - trW, by, trW, bh };
    fRect[home::KeyPrev] = { key.x + 4 * u, by, bh, bh };
    fRect[home::KeyNext] = { key.x + key.w - bh - 4 * u, by, bh, bh };
    fRect[home::TransDown] = { tr.x + tr.w - 2 * bh - 6 * u, by + 3 * u, bh - 6 * u, bh - 6 * u };
    fRect[home::TransUp] = { tr.x + tr.w - bh - 2 * u, by + 3 * u, bh - 6 * u, bh - 6 * u };
}

int HomeView::at(float x, float y) const
{
    for (int i = 0; i < home::Count; ++i)
        if (fRect[i].w > 0 && fRect[i].contains(x, y))
            return i;
    return home::None;
}

void HomeView::draw(NanoVG& vg, const HomeModel& m)
{
    if (!fImgTried) {
        fImgTried = true;
        auto load = [&](const unsigned char* d, size_t n) {
            std::unique_ptr<NanoImage> i(new NanoImage(vg.createImageFromMemory(d, (unsigned int)n, 0)));
            return i->isValid() ? std::move(i) : std::unique_ptr<NanoImage>();
        };
        fLogoImg = load(kHomeLogo, sizeof(kHomeLogo));
        fStageImg = load(kHomeStage, sizeof(kHomeStage));
        fGrandImg = load(kHomeGrand, sizeof(kHomeGrand));
    }
    layout(m);
    const float W = m.w, H = m.h;
    // the hall: deep ink, blue lights high in the corners, gold light low in the middle
    vg.beginPath(); vg.rect(0, 0, W, H); vg.fillColor(Color(4, 3, 2)); vg.fill();
    vg.beginPath(); vg.rect(0, 0, W, H);
    vg.fillPaint(vg.radialGradient(W * 0.9f, H * 0.05f, 10, W * 0.45f, Color(40, 70, 140, 0.35f), Color(0, 0, 0, 0.0f))); vg.fill();
    vg.beginPath(); vg.rect(0, 0, W, H);
    vg.fillPaint(vg.radialGradient(W * 0.5f, H * 0.75f, 10, W * 0.5f, Color(160, 100, 30, 0.22f), Color(0, 0, 0, 0.0f))); vg.fill();
    // the stage
    cover(vg, fStageImg.get(), fStage, 70 * u);
    // ---- the top bar
    image(vg, fLogoImg.get(), fLogo);
    glass(vg, fNav, fNav.h / 2, 0.8f);
    const char* navNames[5] = { "Home", "Piano", "Sounds", "Effects", "Concert Hall" };
    for (int i = 0; i < 5; ++i) {
        const Rect& r = fRect[home::NavHome + i];
        const bool lit = i == 0, hot = m.hot == home::NavHome + i;
        if (lit) pill(vg, r, false, true);
        if (lit) { vg.beginPath(); vg.roundedRect(r.x, r.y, r.w, r.h, r.h / 2); vg.strokeColor(kGoldHi); vg.strokeWidth(2); vg.stroke(); }
        else if (hot) { vg.beginPath(); vg.roundedRect(r.x, r.y, r.w, r.h, r.h / 2); vg.fillColor(Color(255, 210, 130, 0.08f)); vg.fill(); }
        if (i > 0 && i < 5) { vg.beginPath(); vg.moveTo(r.x, r.y + r.h * 0.2f); vg.lineTo(r.x, r.y + r.h * 0.8f); vg.strokeColor(alpha(kGold, 0.25f)); vg.strokeWidth(1); vg.stroke(); }
        const float ix = r.x + r.w / 2, iy = r.y + r.h * 0.38f, is = r.h * 0.38f;
        const Color ic = lit || hot ? kGoldHi : kGold;
        switch (i) {
        case 0: iconHome(vg, ix, iy, is, ic); break;
        case 1: iconPiano(vg, ix, iy, is, ic); break;
        case 2: iconWave(vg, ix, iy, is, ic); break;
        case 3: iconSliders(vg, ix, iy, is, ic); break;
        default: iconHall(vg, ix, iy, is, ic); break;
        }
        text(vg, ix, r.y + r.h * 0.8f, std::min(15 * u, r.w / 7.5f), navNames[i], lit ? Color(255, 250, 240) : kText, kCenter);
    }
    // the volume, the settings, the window's size
    if (fRect[home::Volume].w > 60 * u) {
        const Rect& v = fRect[home::Volume];
        glass(vg, v, v.h / 2, 0.5f);
        iconSpeaker(vg, v.x + v.h * 0.55f, v.y + v.h / 2, v.h * 0.42f, kGold);
        text(vg, v.x + v.h, v.y + v.h / 2, 14 * u, "Volume", kText, kLeft);
        const float tx = v.x + v.h + 62 * u, tw = v.w - (tx - v.x) - 62 * u, ty = v.y + v.h / 2;
        fVolTrack = { tx, v.y, std::max(0.0f, tw), v.h };
        if (tw > 20) {
            vg.beginPath(); vg.roundedRect(tx, ty - 3 * u, tw, 6 * u, 3 * u); vg.fillColor(Color(48, 40, 30)); vg.fill();
            vg.beginPath(); vg.roundedRect(tx, ty - 3 * u, tw * m.volumeNorm, 6 * u, 3 * u);
            vg.fillPaint(vg.linearGradient(tx, 0, tx + tw, 0, kGoldDeep, kGoldHi)); vg.fill();
            const float kx = tx + tw * m.volumeNorm;
            vg.beginPath(); vg.circle(kx, ty, 8 * u);
            vg.fillPaint(vg.radialGradient(kx - 2 * u, ty - 2 * u, 1, 9 * u, Color(255, 236, 190), Color(196, 140, 52))); vg.fill();
        }
        text(vg, v.x + v.w - 12 * u, v.y + v.h / 2, 13 * u, m.volumeText.c_str(), kText, kRight);
    }
    for (int b : { (int)home::Settings, (int)home::Bigger }) {
        const Rect& r = fRect[b];
        glass(vg, r, r.h * 0.3f, m.hot == b ? 1.2f : 0.5f);
        if (b == home::Settings) iconGear(vg, r.x + r.w / 2, r.y + r.h / 2, r.h * 0.5f, m.hot == b ? kGoldHi : kGold);
        else iconBigger(vg, r.x + r.w / 2, r.y + r.h / 2, r.h * 0.42f, m.big || m.hot == b ? kGoldHi : kGold, m.big);
    }
    // ---- left: the instruments
    glass(vg, fLeft, 18 * u);
    const float hh = 40 * u;
    iconCrown(vg, fLeft.x + 26 * u, fLeft.y + hh / 2 + 4 * u, 22 * u, kGold);
    text(vg, fLeft.x + 46 * u, fLeft.y + hh / 2 + 4 * u, 17 * u, "Premium Instruments", kGoldHi, kLeft);
    {
        const Rect& r = fRect[home::SeeAll];
        pill(vg, r, false, m.hot == home::SeeAll);
        text(vg, r.x + r.w / 2, r.y + r.h / 2, 12.5f * u, "See All  >", kText, kCenter);
    }
    vg.beginPath(); vg.moveTo(fLeft.x + 12 * u, fLeft.y + hh + 2 * u); vg.lineTo(fLeft.x + fLeft.w - 12 * u, fLeft.y + hh + 2 * u);
    vg.strokeColor(alpha(kGold, 0.2f)); vg.strokeWidth(1); vg.stroke();
    // the featured card: the instrument playing
    {
        const Rect& f = fFeature;
        vg.beginPath(); vg.roundedRect(f.x, f.y, f.w, f.h, 16 * u);
        vg.fillPaint(vg.linearGradient(0, f.y, 0, f.y + f.h, Color(26, 24, 30), Color(6, 5, 4))); vg.fill();
        vg.strokePaint(vg.linearGradient(0, f.y, 0, f.y + f.h, alpha(kGoldHi, 0.9f), alpha(kGold, 0.5f))); vg.strokeWidth(1.6f); vg.stroke();
        const Rect pic = { f.x + 8 * u, f.y + 8 * u, f.w - 16 * u, f.h * 0.68f };
        if (m.group == "Grand pianos" && fGrandImg) {
            vg.save(); vg.intersectScissor(pic.x, pic.y, pic.w, pic.h);
            cover(vg, fGrandImg.get(), pic, 18 * u);
            vg.restore();
        } else {
            vg.beginPath(); vg.ellipse(pic.x + pic.w / 2, pic.y + pic.h * 0.86f, pic.w * 0.42f, pic.h * 0.08f);
            vg.strokeColor(alpha(kGold, 0.8f)); vg.strokeWidth(2 * u); vg.stroke();
            vg.beginPath(); vg.ellipse(pic.x + pic.w / 2, pic.y + pic.h * 0.86f, pic.w * 0.42f, pic.h * 0.08f);
            vg.strokeColor(alpha(kGold, 0.2f)); vg.strokeWidth(9 * u); vg.stroke();
            iconFamily(vg, m.group, pic.x + pic.w / 2, pic.y + pic.h * 0.48f, std::min(pic.w, pic.h) * 0.62f, kGold);
        }
        text(vg, f.x + f.w / 2, f.y + f.h * 0.80f, std::min(22 * u, f.w / 9), m.instrument.c_str(), kGoldHi, kCenter);
        text(vg, f.x + f.w / 2, f.y + f.h * 0.90f, 13 * u, shortGroup(m.group).c_str(), kText, kCenter);
    }
    for (int b : { (int)home::InstPrev, (int)home::InstNext }) {
        const Rect& r = fRect[b];
        vg.beginPath(); vg.circle(r.x + r.w / 2, r.y + r.h / 2, r.w / 2);
        vg.fillPaint(vg.linearGradient(0, r.y, 0, r.y + r.h, Color(40, 34, 26), Color(8, 6, 4))); vg.fill();
        vg.strokeColor(m.hot == b ? kGoldHi : alpha(kGold, 0.7f)); vg.strokeWidth(1.4f); vg.stroke();
        const float cx = r.x + r.w / 2, cy = r.y + r.h / 2, a = r.w * 0.16f, d = b == home::InstPrev ? 1.0f : -1.0f;
        vg.beginPath(); vg.moveTo(cx + d * a * 0.6f, cy - a); vg.lineTo(cx - d * a * 0.6f, cy); vg.lineTo(cx + d * a * 0.6f, cy + a);
        stroke(vg, kGoldHi, 2.2f * u);
    }
    for (int i = 0; i < 4; ++i) {
        const Rect& r = fRect[home::Group0 + i];
        if (i >= (int)m.groups.size() || m.groups[i].empty()) continue;
        const bool on = i == m.groupIndex, hot = m.hot == home::Group0 + i;
        vg.beginPath(); vg.roundedRect(r.x, r.y, r.w, r.h, 10 * u);
        vg.fillPaint(vg.linearGradient(0, r.y, 0, r.y + r.h, Color(32, 28, 26), Color(8, 7, 6))); vg.fill();
        vg.strokeColor(on ? kGoldHi : hot ? alpha(kGold, 0.9f) : alpha(kGold, 0.3f)); vg.strokeWidth(on ? 1.8f : 1.0f); vg.stroke();
        const Rect pic = { r.x + 4 * u, r.y + 4 * u, r.w - 8 * u, r.h * 0.62f };
        if (m.groups[i] == "Grand pianos" && fGrandImg) {
            vg.save(); vg.intersectScissor(pic.x, pic.y, pic.w, pic.h); cover(vg, fGrandImg.get(), pic, 8 * u); vg.restore();
        } else {
            iconFamily(vg, m.groups[i], pic.x + pic.w / 2, pic.y + pic.h / 2, std::min(pic.w, pic.h) * 0.78f, on ? kGoldHi : kGold);
        }
        text(vg, r.x + r.w / 2, r.y + r.h * 0.84f, std::min(12.5f * u, r.w / 8.5f), shortGroup(m.groups[i]).c_str(), kText, kCenter);
    }
    // ---- right: the audio controls
    glass(vg, fRight, 18 * u);
    iconWave(vg, fAudio.x + 26 * u, fAudio.y + hh / 2 + 4 * u, 20 * u, kGold);
    text(vg, fAudio.x + 46 * u, fAudio.y + hh / 2 + 4 * u, 17 * u, "Audio Control", kGoldHi, kLeft);
    const char* knobNames[4] = { "Reverb", "Delay", "Chorus", "EQ" };
    for (int i = 0; i < 4; ++i) {
        const Rect& r = fRect[home::Knob0 + i];
        const float kr = r.w / 2.6f, cx = r.x + r.w / 2, cy = r.y + kr * 1.3f;
        const bool hot = m.hot == home::Knob0 + i;
        knob(vg, cx, cy, kr, m.knob[i], hot);
        text(vg, cx, cy + kr * 1.55f, 14 * u, knobNames[i], kText, kCenter);
        text(vg, cx, cy + kr * 1.55f + 16 * u, 11.5f * u, m.knobText[i].c_str(), hot ? kGoldHi : kDim, kCenter);
    }
    // the smart features
    {
        const Rect& s = fSmart;
        vg.beginPath(); vg.roundedRect(s.x, s.y, s.w, s.h, 14 * u);
        vg.fillPaint(vg.linearGradient(0, s.y, 0, s.y + s.h, Color(20, 17, 14, 0.9f), Color(8, 7, 6, 0.9f))); vg.fill();
        vg.strokeColor(alpha(kGold, 0.35f)); vg.strokeWidth(1); vg.stroke();
        iconBolt(vg, s.x + 22 * u, s.y + hh / 2 + 2 * u, 20 * u, kGold);
        text(vg, s.x + 40 * u, s.y + hh / 2 + 2 * u, 16 * u, "Smart Features", kGoldHi, kLeft);
        const char* names[3] = { "Auto Accompaniment", "Smart Chords", "One Touch Play" };
        const char* later[3] = { "coming in stage 6", nullptr, "coming in stage 6" };
        for (int i = 0; i < 3; ++i) {
            const Rect& r = fRect[home::SmartAuto + i];
            const bool enabled = later[i] == nullptr, on = i == 1 && m.mode == 1, hot = m.hot == home::SmartAuto + i;
            const float cy = r.y + r.h / 2, ix = r.x + 22 * u;
            if (i == 0) iconNote(vg, ix, cy, 20 * u, enabled ? kGold : alpha(kGold, 0.45f));
            else if (i == 1) iconGrid(vg, ix, cy, 18 * u, kGold);
            else iconPlay(vg, ix, cy, 18 * u, kGold);
            text(vg, r.x + 44 * u, later[i] ? cy - 6 * u : cy, 14 * u, names[i], enabled ? kText : kDim, kLeft);
            if (later[i]) text(vg, r.x + 44 * u, cy + 9 * u, 10.5f * u, later[i], alpha(kDim, 0.8f), kLeft);
            const float tw = 46 * u, th = 24 * u;
            toggle(vg, { r.x + r.w - tw - 10 * u, cy - th / 2, tw, th }, on, enabled, hot);
        }
    }
    // ---- the play bar
    {
        const Rect& b = fBar;
        glass(vg, b, b.h / 2, 0.9f);
        const char* modes[3] = { "Keyboard", "Chords", "Melody" };
        for (int i = 0; i < 3; ++i) {
            const Rect& r = fRect[home::ModeKeyboard + i];
            const bool lit = m.mode == i;
            pill(vg, r, lit, m.hot == home::ModeKeyboard + i);
            const Color ic = lit ? Color(40, 26, 8) : kGold;
            const float ix = r.x + r.h * 0.6f, cy = r.y + r.h / 2;
            if (i == 0) iconKeys(vg, ix, cy, r.h * 0.42f, ic);
            else if (i == 1) iconGrid(vg, ix, cy, r.h * 0.4f, ic);
            else iconNote(vg, ix, cy, r.h * 0.42f, ic);
            text(vg, ix + r.h * 0.45f, cy, std::min(16 * u, r.w / 6.2f), modes[i], lit ? Color(30, 20, 6) : kText, kLeft);
        }
        // the plaque in the middle: the crown and the instrument playing
        const float pw = std::min(300 * u, b.w * 0.26f), ph = b.h * 1.08f, px = b.x + b.w / 2 - pw / 2, py = b.y + b.h / 2 - ph / 2;
        vg.beginPath();
        vg.moveTo(px + 26 * u, py); vg.lineTo(px + pw - 26 * u, py); vg.lineTo(px + pw, py + ph / 2); vg.lineTo(px + pw - 26 * u, py + ph);
        vg.lineTo(px + 26 * u, py + ph); vg.lineTo(px, py + ph / 2); vg.closePath();
        vg.fillPaint(vg.linearGradient(0, py, 0, py + ph, Color(30, 24, 18), Color(6, 5, 4))); vg.fill();
        vg.strokePaint(vg.linearGradient(0, py, 0, py + ph, kGoldHi, kGoldDeep)); vg.strokeWidth(1.8f); vg.stroke();
        { // the gold bars out of it, in the room between the buttons
            const Rect& mel = fRect[home::ModeMelody];
            const float leftRoom = px - (mel.x + mel.w) - 16 * u;
            const float rightRoom = (fRect[home::TransDown].x - std::min(210 * u, b.w * 0.17f) + (fRect[home::TransDown].w) ) - (px + pw);
            for (int sgn : { -1, 1 }) {
                const float room = std::min(110 * u, (sgn < 0 ? leftRoom : rightRoom) - 16 * u);
                if (room < 20 * u) continue;
                const float x0 = sgn < 0 ? px - room - 6 * u : px + pw + 6 * u;
                vg.beginPath(); vg.roundedRect(x0, b.y + b.h / 2 - 3 * u, room, 6 * u, 3 * u);
                vg.fillPaint(vg.linearGradient(x0, 0, x0 + room, 0, sgn < 0 ? alpha(kGold, 0.0f) : kGold, sgn < 0 ? kGold : alpha(kGold, 0.0f))); vg.fill();
            }
        }
        iconCrown(vg, px + pw / 2, py - 2 * u, 18 * u, kGold);
        vg.fontSize(std::min(24 * u, pw / 9));
        vg.textAlign(NanoVG::Align(kCenter));
        vg.fillPaint(vg.linearGradient(0, py + ph * 0.3f, 0, py + ph * 0.75f, Color(255, 232, 170), Color(206, 150, 60)));
        vg.text(px + pw / 2, py + ph * 0.55f, m.instrument.c_str(), nullptr);
        // transpose, the key
        const float bh = fRect[home::KeyPrev].h, by = fRect[home::KeyPrev].y;
        const Rect key = { fRect[home::KeyPrev].x - 4 * u, by, fRect[home::KeyNext].x + fRect[home::KeyNext].w + 4 * u - (fRect[home::KeyPrev].x - 4 * u), bh };
        const float trW = std::min(210 * u, b.w * 0.17f);
        const Rect trBox = { key.x - 12 * u - trW, by, trW, bh };
        glass(vg, trBox, bh / 2, 0.3f);
        char t[32];
        std::snprintf(t, sizeof t, m.transpose == 0 ? "Transpose" : "Transpose %+d", m.transpose);
        text(vg, trBox.x + 14 * u, by + bh / 2, std::min(15 * u, trW / (m.transpose == 0 ? 9.0f : 11.5f)), t, kText, kLeft);
        for (int bt : { (int)home::TransDown, (int)home::TransUp }) {
            const Rect& r = fRect[bt];
            vg.beginPath(); vg.circle(r.x + r.w / 2, r.y + r.h / 2, r.w / 2);
            vg.strokeColor(m.hot == bt ? kGoldHi : kGold); vg.strokeWidth(1.6f); vg.stroke();
            const float cx = r.x + r.w / 2, cy = r.y + r.h / 2, a = r.w * 0.26f;
            vg.beginPath(); vg.moveTo(cx - a, cy); vg.lineTo(cx + a, cy);
            if (bt == home::TransUp) { vg.moveTo(cx, cy - a); vg.lineTo(cx, cy + a); }
            stroke(vg, kGoldHi, 2 * u);
        }
        glass(vg, key, bh / 2, 0.3f);
        static const char* const keys[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "G#", "A", "Bb", "B" };
        std::snprintf(t, sizeof t, "Key  %s", keys[((m.key % 12) + 12) % 12]);
        text(vg, key.x + key.w / 2, by + bh / 2, 15 * u, t, kText, kCenter);
        for (int bt : { (int)home::KeyPrev, (int)home::KeyNext }) {
            const Rect& r = fRect[bt];
            const float cx = r.x + r.w / 2, cy = r.y + r.h / 2, a = r.h * 0.16f, d = bt == home::KeyPrev ? 1.0f : -1.0f;
            vg.beginPath(); vg.moveTo(cx + d * a * 0.6f, cy - a); vg.lineTo(cx - d * a * 0.6f, cy); vg.lineTo(cx + d * a * 0.6f, cy + a);
            stroke(vg, m.hot == bt ? kGoldHi : kGold, 2 * u);
        }
    }
}

} // namespace pe
