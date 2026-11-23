#include "Widgets.hpp"
#include "Theme.hpp"
#include "Bevel.hpp"

#include <algorithm>

namespace pe {
using DGL_NAMESPACE::NanoVG;

namespace {
void label(NanoVG& vg, float x, float y, const char* s, const DGL_NAMESPACE::Color& c, NanoVG::Align a)
{
    if (s == nullptr || s[0] == '\0')
        return; // nothing to draw (NanoVG asserts on an empty string): the About tab's blank lines
    vg.fillColor(c);
    vg.textAlign(a);
    vg.text(x, y, s, nullptr);
}
}

void drawSlider(NanoVG& vg, const Rect& r, const ControlLook& c)
{
    const auto& tc = c.active ? theme::text : theme::faint;
    vg.fontSize(theme::fontSize);
    label(vg, r.x, r.y + r.h * 0.32f, c.label, tc, NanoVG::Align(NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE));

    // The value is also an editable field. Reveal its boundary on hover so that
    // the click-to-type behavior is visible without turning every row into a box.
    const float valueW = std::min(r.w * 0.34f, 132.0f);
    if (c.hot || c.editing) {
        bevel::sunken(vg, r.x + r.w - valueW, r.y + 1, valueW, r.h * 0.56f, theme::radius);
        if (c.editing) {
            vg.beginPath(); vg.roundedRect(r.x + r.w - valueW, r.y + 1, valueW, r.h * 0.56f, theme::radius);
            vg.strokeColor(theme::accent); vg.strokeWidth(1); vg.stroke();
        }
    }
    const std::string& v = c.editing && c.editText ? *c.editText : c.value;
    const auto& valueColor = c.editing ? theme::accentText : c.active ? (c.hot ? theme::accentText : theme::text) : theme::faint;
    label(vg, r.x + r.w - 8, r.y + r.h * 0.32f, (c.editing ? v + "|" : v).c_str(), valueColor,
        NanoVG::Align(NanoVG::ALIGN_RIGHT | NanoVG::ALIGN_MIDDLE));

    const float ty = r.y + r.h * 0.78f;
    const float trackH = theme::track + 2.0f;
    bevel::track(vg, r.x, ty - trackH / 2, r.w, trackH, c.norm, c.active);
    bevel::knob(vg, r.x + r.w * c.norm, ty, c.hot ? theme::knob + 1.5f : theme::knob, c.active, c.hot);
}

void drawToggle(NanoVG& vg, const Rect& r, const ControlLook& c)
{
    const bool on = c.norm >= 0.5f;
    bevel::raised(vg, r.x, r.y, r.w, r.h, theme::radius + 1, on, c.hot);
    vg.fontSize(theme::fontSize);
    label(vg, r.x + r.w / 2, r.y + r.h / 2 + 0.5f, c.label, on ? DGL_NAMESPACE::Color(26, 18, 8) : (c.hot ? theme::text : theme::dim),
        NanoVG::Align(NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE));
}

void drawDropdown(NanoVG& vg, const Rect& r, const ControlLook& c)
{
    bevel::raised(vg, r.x, r.y, r.w, r.h, theme::radius + 1, false, c.hot);
    vg.fontSize(theme::fontSize);
    const std::string s = c.value + "  ▾";
    label(vg, r.x + 10, r.y + r.h / 2, s.c_str(), c.active ? theme::text : theme::faint, NanoVG::Align(NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE));
}

void drawDropdownList(NanoVG& vg, const Rect& l, const std::vector<std::string>& items, int selected, int hot, float scroll)
{
    vg.beginPath(); vg.rect(l.x - 4, l.y, l.w + 8, l.h + 8); // the open list floats over the window
    vg.fillPaint(vg.boxGradient(l.x, l.y + 3, l.w, l.h, theme::radius, 8, DGL_NAMESPACE::Color(0, 0, 0, 0.6f), DGL_NAMESPACE::Color(0, 0, 0, 0.0f)));
    vg.fill();
    vg.beginPath();
    vg.roundedRect(l.x, l.y, l.w, l.h, theme::radius);
    vg.fillColor(DGL_NAMESPACE::Color(20, 15, 10));
    vg.fill();
    vg.strokeColor(DGL_NAMESPACE::Color(theme::gold, DGL_NAMESPACE::Color(0, 0, 0), 0.4f));
    vg.stroke();
    vg.save();
    vg.intersectScissor(l.x, l.y, l.w, l.h);
    vg.fontSize(theme::fontSize);
    const size_t first = (size_t)std::max(0.0f, scroll / kRowH);
    for (size_t i = first; i < items.size(); ++i) {
        const float y = l.y + i * kRowH - scroll;
        if (y >= l.y + l.h)
            break;
        if ((int)i == hot) {
            vg.beginPath();
            vg.rect(l.x + 1, y, l.w - 2, kRowH);
            vg.fillPaint(vg.linearGradient(0, y, 0, y + kRowH, DGL_NAMESPACE::Color(92, 70, 34), DGL_NAMESPACE::Color(60, 45, 22)));
            vg.fill();
        }
        label(vg, l.x + 10, y + kRowH / 2, items[i].c_str(), (int)i == selected ? theme::accentText : theme::text,
            NanoVG::Align(NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE));
    }
    const ListBar b = listBar(l, (uint32_t)items.size(), scroll);
    if (b.track.w > 0) {
        vg.beginPath();
        vg.rect(b.track.x, b.track.y, b.track.w, b.track.h);
        vg.fillColor(theme::panel);
        vg.fill();
        vg.beginPath();
        vg.roundedRect(b.thumb.x + 2, b.thumb.y + 2, b.thumb.w - 4, b.thumb.h - 4, (b.thumb.w - 4) / 2);
        vg.fillColor(theme::marker);
        vg.fill();
    }
    vg.restore();
}
}
