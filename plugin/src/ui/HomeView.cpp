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


std::string shortGroup(const std::string& g)
{
    if (g == "Grand pianos") return "Concert Grand";
    if (g == "Upright pianos") return "Studio Piano";
    if (g == "Harpsichords") return "Harpsichord";
    if (g == "Clavichords") return "Clavichord";
    if (g == "Harps") return "Harp";
    if (g == "Guitars") return "Guitar";
    if (g == "Thumb pianos") return "Thumb Piano";
    if (g == "Zithers") return "Zither";
    return g;
}

std::unique_ptr<NanoImage> loadImage(NanoVG& vg, const unsigned char* d, size_t n)
{
    std::unique_ptr<NanoImage> i(new NanoImage(vg.createImageFromMemory(d, (unsigned int)n, 0)));
    return i->isValid() ? std::move(i) : std::unique_ptr<NanoImage>();
}

void blit(NanoVG& vg, const NanoImage* img, float x, float y, float w, float h, float a = 1.0f)
{
    if (img == nullptr) return;
    vg.beginPath(); vg.rect(x, y, w, h);
    vg.fillPaint(vg.imagePattern(x, y, w, h, 0, *img, a)); vg.fill();
}

} // namespace

// the design's own coordinates (its picture is 1672 x 736 above the keys); the view scales them
namespace {
const Rect kNav[5] = { { 530, 30, 130, 95 }, { 662, 30, 120, 95 }, { 784, 30, 120, 95 }, { 905, 30, 117, 95 }, { 1022, 30, 140, 95 } };
const Rect kVolumePill { 1218, 38, 294, 65 }, kVolumeTrack { 1350, 68, 84, 6 };
const Rect kSettings { 1527, 40, 52, 60 }, kBigger { 1590, 40, 56, 60 };
const Rect kSeeAll { 460, 195, 75, 27 };
const Rect kCards[4] = { { 421, 250, 112, 80 }, { 421, 338, 112, 78 }, { 421, 425, 112, 78 }, { 421, 513, 112, 78 } };
const Rect kFeaturePic { 100, 252, 270, 250 };
const float kKnobX[4] = { 1291, 1391, 1491, 1591 }, kKnobY = 309;
const Rect kToggles[3] = { { 1566, 468, 52, 27 }, { 1566, 511, 52, 26 }, { 1566, 555, 52, 26 } };
const Rect kModes[3] = { { 38, 659, 160, 54 }, { 217, 659, 141, 54 }, { 377, 659, 141, 54 } };
}

void HomeView::layout(const HomeModel& m)
{
    // the picture as large as fits, its foot on the keys; the room left over is the picture's own ink
    k = std::min(m.w / 1672.0f, m.h / 736.0f);
    ox = (m.w - 1672.0f * k) / 2;
    oy = m.h - 736.0f * k;
    u = k;
    auto R = [&](const Rect& r) { return Rect { ox + r.x * k, oy + r.y * k, r.w * k, r.h * k }; };
    auto C = [&](float cx, float cy, float r) { return Rect { ox + (cx - r) * k, oy + (cy - r) * k, 2 * r * k, 2 * r * k }; };
    for (int i = 0; i < 5; ++i) fRect[home::NavHome + i] = R(kNav[i]);
    fRect[home::Volume] = R(kVolumePill);
    fVolTrack = R(kVolumeTrack);
    fRect[home::Settings] = R(kSettings);
    fRect[home::Bigger] = R(kBigger);
    fRect[home::SeeAll] = R(kSeeAll);
    fRect[home::InstPrev] = C(62, 375, 20);
    fRect[home::InstNext] = C(405, 375, 20);
    for (int i = 0; i < 4; ++i) fRect[home::Group0 + i] = R(kCards[i]);
    for (int i = 0; i < 4; ++i) fRect[home::Knob0 + i] = C(kKnobX[i], kKnobY + 10, 46);
    for (int i = 0; i < 3; ++i) fRect[home::SmartAuto + i] = R({ 1250, kToggles[i].y - 14, 380, kToggles[i].h + 28 });
    for (int i = 0; i < 3; ++i) fRect[home::ModeKeyboard + i] = R(kModes[i]);
    fRect[home::TransDown] = C(1331, 687, 20);
    fRect[home::TransUp] = C(1380, 687, 20);
    fRect[home::KeyPrev] = C(1458, 687, 18);
    fRect[home::KeyNext] = C(1594, 687, 18);
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
        fPlate = loadImage(vg, kSkinPlate, sizeof(kSkinPlate));
        fCap = loadImage(vg, kSkinKnobCap, sizeof(kSkinKnobCap));
        fMode[0][1] = loadImage(vg, kSkinMode_kLit, sizeof(kSkinMode_kLit));
        fMode[0][0] = loadImage(vg, kSkinMode_kUnlit, sizeof(kSkinMode_kUnlit));
        fMode[1][1] = loadImage(vg, kSkinMode_cLit, sizeof(kSkinMode_cLit));
        fMode[1][0] = loadImage(vg, kSkinMode_cUnlit, sizeof(kSkinMode_cUnlit));
        fMode[2][1] = loadImage(vg, kSkinMode_mLit, sizeof(kSkinMode_mLit));
        fMode[2][0] = loadImage(vg, kSkinMode_mUnlit, sizeof(kSkinMode_mUnlit));
    }
    layout(m);
    const float W = m.w, H = m.h, pw = 1672 * k, ph = 736 * k;
    auto X = [&](float x) { return ox + x * k; };
    auto Y = [&](float y) { return oy + y * k; };
    vg.beginPath(); vg.rect(0, 0, W, H); vg.fillColor(Color(3, 2, 2)); vg.fill();
    if (fPlate) {
        // the room above and beside the picture: its own edges drawn out (the hall's ceiling, its dark walls)
        if (oy > 0.5f) {
            vg.beginPath(); vg.rect(ox, 0, pw, oy + 1);
            vg.fillPaint(vg.imagePattern(ox, 0, pw, ph * 40.0f, 0, *fPlate, 1)); vg.fill(); // its first rows, stretched tall
            vg.beginPath(); vg.rect(ox, 0, pw, oy + 1);
            vg.fillPaint(vg.linearGradient(0, 0, 0, oy + 1, Color(3, 2, 2, 0.85f), Color(3, 2, 2, 0.0f))); vg.fill();
        }
        if (ox > 0.5f) {
            vg.beginPath(); vg.rect(0, oy, ox + 1, ph);
            vg.fillPaint(vg.imagePattern(0, oy, pw * 60.0f, ph, 0, *fPlate, 1)); vg.fill();
            vg.beginPath(); vg.rect(ox + pw - 1, oy, W - ox - pw + 1, ph);
            vg.fillPaint(vg.imagePattern(ox + pw - pw * 60.0f, oy, pw * 60.0f, ph, 0, *fPlate, 1)); vg.fill();
        }
        blit(vg, fPlate.get(), ox, oy, pw, ph);
    }
    // ---- the navigation: Home lit (this screen); the one under the pointer glows
    for (int i = 1; i < 5; ++i)
        if (m.hot == home::NavHome + i) {
            const Rect& r = fRect[home::NavHome + i];
            vg.beginPath(); vg.roundedRect(r.x + 4 * k, r.y + 4 * k, r.w - 8 * k, r.h - 8 * k, 30 * k);
            vg.fillPaint(vg.radialGradient(r.x + r.w / 2, r.y + r.h / 2, 4, r.w * 0.6f, Color(255, 200, 110, 0.20f), Color(255, 200, 110, 0.0f)));
            vg.fill();
            vg.strokeColor(alpha(kGold, 0.55f)); vg.strokeWidth(1.2f); vg.stroke();
        }
    for (int b : { (int)home::Settings, (int)home::Bigger, (int)home::SeeAll })
        if (m.hot == b) {
            const Rect& r = fRect[b];
            vg.beginPath(); vg.roundedRect(r.x, r.y, r.w, r.h, std::min(r.h / 2, 16 * k));
            vg.strokeColor(kGoldHi); vg.strokeWidth(1.5f); vg.stroke();
        }
    // ---- the volume: its slider and its value
    {
        const Rect& t = fVolTrack;
        const float cy = t.y + t.h / 2, kx = t.x + t.w * std::clamp(m.volumeNorm, 0.0f, 1.0f);
        vg.beginPath(); vg.roundedRect(t.x, t.y, t.w, t.h, t.h / 2); vg.fillColor(Color(70, 56, 36)); vg.fill();
        vg.beginPath(); vg.roundedRect(t.x, t.y, kx - t.x, t.h, t.h / 2);
        vg.fillPaint(vg.linearGradient(t.x, 0, t.x + t.w, 0, Color(196, 128, 40), Color(255, 214, 130))); vg.fill();
        const float r = 12 * k;
        vg.beginPath(); vg.circle(kx, cy, r * 1.9f);
        vg.fillPaint(vg.radialGradient(kx, cy, r * 0.8f, r * 1.9f, Color(255, 190, 90, 0.45f), Color(255, 190, 90, 0.0f))); vg.fill();
        vg.beginPath(); vg.circle(kx, cy, r);
        vg.fillPaint(vg.radialGradient(kx - r * 0.35f, cy - r * 0.35f, 1, r * 1.1f, Color(255, 246, 220), Color(214, 150, 54))); vg.fill();
        vg.strokeColor(Color(120, 70, 20)); vg.strokeWidth(1); vg.stroke();
        text(vg, X(1503), Y(72), 18 * k, m.volumeText.c_str(), Color(236, 232, 226), kRight);
    }
    // ---- the featured card: the instrument playing (the grand's picture is the design's own)
    {
        if (m.group != "Grand pianos") {
            const Rect p = { X(kFeaturePic.x), Y(kFeaturePic.y), kFeaturePic.w * k, kFeaturePic.h * k };
            vg.beginPath(); vg.roundedRect(p.x, p.y, p.w, p.h, 14 * k);
            vg.fillPaint(vg.linearGradient(0, p.y, 0, p.y + p.h, Color(30, 30, 36), Color(6, 5, 5))); vg.fill();
            vg.beginPath(); vg.ellipse(p.x + p.w / 2, p.y + p.h * 0.85f, p.w * 0.4f, p.h * 0.07f);
            vg.strokeColor(alpha(kGold, 0.85f)); vg.strokeWidth(2.5f * k); vg.stroke();
            vg.beginPath(); vg.ellipse(p.x + p.w / 2, p.y + p.h * 0.85f, p.w * 0.4f, p.h * 0.07f);
            vg.strokeColor(alpha(kGold, 0.22f)); vg.strokeWidth(12 * k); vg.stroke();
            iconFamily(vg, m.group, p.x + p.w / 2, p.y + p.h * 0.45f, std::min(p.w, p.h) * 0.6f, kGoldHi);
        }
        vg.fontSize(25 * k);
        vg.textAlign(NanoVG::Align(kCenter));
        vg.fillPaint(vg.linearGradient(0, Y(514), 0, Y(542), Color(255, 226, 150), Color(222, 160, 64)));
        vg.text(X(234), Y(528), shortGroup(m.group).c_str(), nullptr);
        text(vg, X(234), Y(556), 16 * k, m.instrument.c_str(), Color(236, 230, 220), kCenter);
    }
    for (int b : { (int)home::InstPrev, (int)home::InstNext })
        if (m.hot == b) {
            const Rect& r = fRect[b];
            vg.beginPath(); vg.circle(r.x + r.w / 2, r.y + r.h / 2, r.w / 2);
            vg.strokeColor(kGoldHi); vg.strokeWidth(1.6f); vg.stroke();
        }
    // the category cards: Studio Piano plays the uprights; the electric piano, the strings and the organ come later
    for (int i = 0; i < 4; ++i) {
        const Rect& r = fRect[home::Group0 + i];
        if (i > 0) {
            vg.beginPath(); vg.roundedRect(r.x, r.y, r.w, r.h, 10 * k); vg.fillColor(Color(0, 0, 0, 0.5f)); vg.fill();
            const Rect b = { r.x + r.w - 46 * k, r.y + 6 * k, 40 * k, 16 * k };
            vg.beginPath(); vg.roundedRect(b.x, b.y, b.w, b.h, b.h / 2); vg.fillColor(Color(20, 14, 8, 0.9f)); vg.fill();
            vg.strokeColor(alpha(kGold, 0.7f)); vg.strokeWidth(1); vg.stroke();
            text(vg, b.x + b.w / 2, b.y + b.h / 2, 10.5f * k, "SOON", kGold, kCenter);
        } else if (m.group == "Upright pianos" || m.hot == home::Group0) {
            vg.beginPath(); vg.roundedRect(r.x, r.y, r.w, r.h, 10 * k);
            vg.strokeColor(kGoldHi); vg.strokeWidth(2); vg.stroke();
        }
    }
    // ---- the knobs: the design's cap turned to the value, its ring lit as far
    for (int i = 0; i < 4; ++i) {
        const float cx = X(kKnobX[i]), cy = Y(kKnobY), v = std::clamp(m.knob[i], 0.0f, 1.0f);
        const bool hot = m.hot == home::Knob0 + i;
        const float a0 = 0.75f * (float)M_PI, a1 = 2.25f * (float)M_PI, av = a0 + (a1 - a0) * v, rr = 35.5f * k;
        vg.beginPath(); vg.arc(cx, cy, rr, a0, a1, NanoVG::CW); stroke(vg, Color(58, 46, 32), 6.5f * k);
        if (v > 0.002f) {
            vg.beginPath(); vg.arc(cx, cy, rr, a0, av, NanoVG::CW); stroke(vg, Color(255, 170, 60, 0.28f), 16 * k); // its glow
            vg.beginPath(); vg.arc(cx, cy, rr, a0, av, NanoVG::CW);
            vg.strokePaint(vg.linearGradient(cx - rr, cy + rr, cx, cy - rr, Color(214, 128, 30), hot ? Color(255, 240, 200) : Color(255, 214, 130)));
            vg.strokeWidth(6.5f * k); vg.lineCap(NanoVG::ROUND); vg.stroke();
        }
        if (fCap) {
            const float r = 27 * k;
            vg.save();
            vg.translate(cx, cy);
            vg.rotate(av - 1.5f * (float)M_PI); // the design's notch points up: the middle of the range
            vg.beginPath(); vg.circle(0, 0, r);
            vg.fillPaint(vg.imagePattern(-r, -r, 2 * r, 2 * r, 0, *fCap, 1)); vg.fill();
            vg.restore();
        }
        text(vg, cx, Y(383), 13 * k, m.knobText[i].c_str(), hot ? kGoldHi : kDim, kCenter);
    }
    // ---- the smart features: Smart Chords works; the other two say they come later
    for (int i = 0; i < 3; ++i) {
        const Rect t = { X(kToggles[i].x), Y(kToggles[i].y), kToggles[i].w * k, kToggles[i].h * k };
        const bool enabled = i == 1, on = enabled && m.mode == 1, hot = m.hot == home::SmartAuto + i && enabled;
        if (on) {
            vg.beginPath(); vg.rect(t.x - 12 * k, t.y - 12 * k, t.w + 24 * k, t.h + 24 * k);
            vg.fillPaint(vg.boxGradient(t.x, t.y, t.w, t.h, t.h / 2, 12 * k, Color(255, 170, 60, 0.4f), Color(255, 170, 60, 0.0f))); vg.fill();
        }
        vg.beginPath(); vg.roundedRect(t.x, t.y, t.w, t.h, t.h / 2);
        vg.fillPaint(on ? vg.linearGradient(0, t.y, 0, t.y + t.h, Color(255, 206, 120), Color(206, 128, 36))
                        : vg.linearGradient(0, t.y, 0, t.y + t.h, Color(16, 13, 10), Color(44, 36, 26)));
        vg.fill();
        vg.strokeColor(on ? Color(255, 226, 160) : hot ? kGoldHi : alpha(kGold, enabled ? 0.55f : 0.3f)); vg.strokeWidth(1.2f); vg.stroke();
        const float d = t.h - 5 * k, kx = on ? t.x + t.w - 2.5f * k - d / 2 : t.x + 2.5f * k + d / 2, ky = t.y + t.h / 2;
        vg.beginPath(); vg.circle(kx, ky, d / 2);
        vg.fillPaint(vg.radialGradient(kx - d * 0.15f, ky - d * 0.15f, 1, d * 0.6f, Color(255, 255, 255), on ? Color(236, 230, 222) : Color(150, 142, 132)));
        vg.fill();
        if (!enabled)
            text(vg, X(i == 0 ? 1474 : 1432), Y(kToggles[i].y + kToggles[i].h / 2 + 1), 12 * k, "· soon", alpha(kDim, 0.9f), kLeft);
    }
    // ---- the play bar: the three modes (the design's buttons, lit and unlit), transpose, the key
    for (int i = 0; i < 3; ++i) {
        const Rect r = fRect[home::ModeKeyboard + i];
        const bool lit = m.mode == i;
        if (i == 0 && !lit) { // the design lights Keyboard: its glow put out
            vg.beginPath(); vg.roundedRect(r.x - 10 * k, r.y - 9 * k, r.w + 20 * k, r.h + 18 * k, r.h / 2 + 9 * k);
            vg.fillColor(Color(16, 8, 2)); vg.fill();
        }
        if (i > 0 && lit) {
            vg.beginPath(); vg.rect(r.x - 16 * k, r.y - 16 * k, r.w + 32 * k, r.h + 32 * k);
            vg.fillPaint(vg.boxGradient(r.x, r.y, r.w, r.h, r.h / 2, 16 * k, Color(255, 170, 60, 0.5f), Color(255, 170, 60, 0.0f))); vg.fill();
        }
        if (fMode[i][lit ? 1 : 0]) {
            vg.beginPath(); vg.roundedRect(r.x, r.y, r.w, r.h, r.h / 2);
            vg.fillPaint(vg.imagePattern(r.x, r.y, r.w, r.h, 0, *fMode[i][lit ? 1 : 0], 1)); vg.fill();
        }
        if (m.hot == home::ModeKeyboard + i && !lit) {
            vg.beginPath(); vg.roundedRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, r.h / 2); vg.strokeColor(kGoldHi); vg.strokeWidth(1.5f); vg.stroke();
        }
    }
    {
        char t[32];
        if (m.transpose == 0) std::snprintf(t, sizeof t, "Transpose");
        else std::snprintf(t, sizeof t, "Transp. %+d", m.transpose);
        text(vg, X(1252), Y(687), 18 * k, t, Color(236, 232, 226), kCenter);
        static const char* const keys[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "G#", "A", "Bb", "B" };
        std::snprintf(t, sizeof t, "Key   %s", keys[((m.key % 12) + 12) % 12]);
        text(vg, X(1526), Y(687), 18 * k, t, Color(236, 232, 226), kCenter);
        for (int b : { (int)home::TransDown, (int)home::TransUp, (int)home::KeyPrev, (int)home::KeyNext })
            if (m.hot == b) {
                const Rect& r = fRect[b];
                vg.beginPath(); vg.circle(r.x + r.w / 2, r.y + r.h / 2, r.w / 2);
                vg.fillPaint(vg.radialGradient(r.x + r.w / 2, r.y + r.h / 2, 2, r.w / 2, Color(255, 200, 110, 0.35f), Color(255, 200, 110, 0.0f))); vg.fill();
            }
    }
    (void)H;
}

} // namespace pe
