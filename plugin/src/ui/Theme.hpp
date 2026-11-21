// One dark theme: colors and sizes (logical units; the UI scales them).
#pragma once
#include "Color.hpp"

namespace pe::theme {
// NA_Magicos: black and walnut, gold accents, ivory text; the panels let the cover art show through a little
inline const DGL_NAMESPACE::Color bg { 10, 8, 6 }, panel { 24, 18, 12, 0.86f }, line { 70, 54, 30 };
inline const DGL_NAMESPACE::Color text { 238, 228, 206 }, dim { 190, 170, 132 }, faint { 132, 114, 86 };
inline const DGL_NAMESPACE::Color accent { 214, 172, 84 }, accentText { 244, 212, 140 };
inline const DGL_NAMESPACE::Color wood { 70, 46, 24 }, woodEdge { 168, 128, 62 }, keys { 96, 80, 60 }, marker { 120, 98, 64 };
inline const DGL_NAMESPACE::Color gold { 212, 175, 90 }, ivory { 242, 234, 214 }, ebony { 16, 12, 9 };
constexpr float fontSize = 13, smallFont = 11, radius = 4, track = 4, knob = 7;
}
