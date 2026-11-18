#include "UiState.hpp"
#include "Layout.hpp"
#include "Params.hpp"
#include <algorithm>
#include "Numbers.hpp"
// std::to_chars for floating point: libstdc++ 11+, libc++ on macOS 13.3+
#include <charconv>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <cmath>
#include <cstdio>
#include <cstring>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace pe {
const int kZoomSteps[8] = { 100, 125, 150, 175, 200, 250, 300, 400 };

namespace {
/// the number after `key` ("lo=" …) in `text`; locale independent (pe::parseFloat reads a point), the
/// old text format stays readable: an optional leading '+', exponent notation. False if there is no number.
bool readFloat(const char* text, const char* key, float& out)
{
    const char* p = std::strstr(text, key);
    if (p == nullptr) return false;
    p += std::strlen(key);
    if (*p == '+') ++p; // sscanf's %f took it, parseFloat does not
    float v = 0;
    if (parseFloat(p, p + std::strlen(p), v) == nullptr) return false;
    out = v;
    return true;
}
}

UiState parseUiState(const char* s)
{
    UiState st;
    if (s == nullptr)
        return st;
    // a state saved before the tabs has "open=…" (the sections) instead: its tab is Sound
    unsigned tab = 0;
    if (const char* t = std::strstr(s, "tab="); t != nullptr && std::sscanf(t, "tab=%u", &tab) == 1 && tab < kTabCount)
        st.tab = tab;
    int zoom = 0;
    if (const char* z = std::strstr(s, "zoom="); z != nullptr && std::sscanf(z, "zoom=%d", &zoom) == 1)
        for (int k : kZoomSteps)
            if (k == zoom)
                st.zoom = zoom;
    int diag = 0;
    if (const char* d = std::strstr(s, ";diag=")) {
        std::sscanf(d, ";diag=%d", &diag);
        st.diag = diag >= 0 && diag <= 5 ? diag : 0; // StripInteraction.hpp: 4 and 5 a panel without the strip
    }
    int view = 0;
    if (const char* v = std::strstr(s, ";view=")) {
        std::sscanf(v, ";view=%d", &view);
        st.view = view == 1 ? 1 : 0;
    }
    float lo = 0, hi = 0, fl = 0;
    if (readFloat(s, ";lo=", lo) && readFloat(s, ";hi=", hi) && std::isfinite(lo) && std::isfinite(hi) && lo >= 20.0f &&
        hi <= 24000.0f && hi / lo >= 4.0f) {
        st.lo = lo;
        st.hi = hi;
    }
    if (readFloat(s, ";floor=", fl) && std::isfinite(fl) && fl >= -120.0f && fl <= -20.0f)
        st.floorDb = fl;
    return st;
}

std::string formatUiState(const UiState& s)
{
    // the floats through std::to_chars: shortest text that reads back exactly, always with a point whatever the
    // numeric locale (a printf would write "27,5" under a German host). Same text as the old %g for these values.
    auto num = [](float v) {
        char buf[32];
        const std::to_chars_result r = std::to_chars(buf, buf + sizeof buf, v);
        return std::string(buf, r.ptr);
    };
    char head[96];
    std::snprintf(head, sizeof head, "tab=%u;zoom=%d;diag=%d;view=%d;", s.tab, s.zoom, s.diag, s.view); // integers only
    return std::string(head) + "lo=" + num(s.lo) + ";hi=" + num(s.hi) + ";floor=" + num(s.floorDb);
}

void minimumWindowSize(double hostScale, uint32_t& width, uint32_t& height)
{
    width = (uint32_t)(kMinWidth * hostScale);
    height = (uint32_t)(kMinHeight * hostScale);
}

void minimumWindowSize(double hostScale, double scale, float diagLogical, uint32_t& width, uint32_t& height)
{
    minimumWindowSize(hostScale, width, height);
    constexpr float kControlsViewport = 240.0f; // the drawing's rows and a few of the tab's (ui_tests)
    const uint32_t diagHeight = (uint32_t)std::lround(diagLogical * scale);
    const uint32_t controlsHeight = (uint32_t)std::lround(kControlsViewport * hostScale);
    height = std::max(height, diagHeight + controlsHeight);
}

bool WindowResizeTracker::pending(uint64_t nowMs) const
{
    return fCount > 0 && nowMs - fAt < kExpiryMs;
}

void WindowResizeTracker::requested(uint32_t height, uint64_t nowMs)
{
    if (!pending(nowMs)) fCount = 0; // an expired request is forgotten
    if (fCount == kMax) {
        for (int i = 1; i < kMax; ++i) fReq[i - 1] = fReq[i];
        --fCount;
    }
    fReq[fCount++] = height;
    fAt = nowMs;
}

void WindowResizeTracker::hostResized(uint32_t reportedHeight, uint64_t nowMs)
{
    if (!pending(nowMs)) { fCount = 0; return; }
    for (int i = 0; i < fCount; ++i)
        if (fReq[i] == reportedHeight) {
            for (int j = i + 1; j < fCount; ++j) fReq[j - i - 1] = fReq[j];
            fCount -= i + 1;
            return;
        }
    fCount = 0; // the host clamped a zoom request or the user resized the window
}

uint32_t WindowResizeTracker::zoomed(uint32_t actualHeight, double oldScale, double newScale, uint32_t minH, uint64_t nowMs)
{
    const uint32_t base = pending(nowMs) ? fReq[fCount - 1] : actualHeight;
    const uint32_t h = std::max<uint32_t>(minH, (uint32_t)std::lround(base / oldScale * newScale));
    requested(h, nowMs);
    return h;
}

std::vector<uint32_t> resetParams()
{
    std::vector<uint32_t> r;
    for (uint32_t i = 0; i < kParamCount; ++i)
        r.push_back(i);
    return r;
}

int parseSavedZoom(const std::string& text)
{
    int zoom = 0;
    if (const char* z = std::strstr(text.c_str(), "zoom="); z != nullptr && std::sscanf(z, "zoom=%d", &zoom) == 1)
        for (int k : kZoomSteps)
            if (k == zoom)
                return zoom;
    return 100;
}

WindowPlacementState parseWindowPlacement(const std::string& text)
{
    WindowPlacementState s;
    s.zoom = parseSavedZoom(text);
    bool hasX = false, hasY = false, hasW = false, hasH = false;
    int x = 0, y = 0, w = 0, h = 0, max = 0;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (std::sscanf(line.c_str(), "x=%d", &x) == 1)
            hasX = true;
        else if (std::sscanf(line.c_str(), "y=%d", &y) == 1)
            hasY = true;
        else if (std::sscanf(line.c_str(), "width=%d", &w) == 1)
            hasW = w > 0;
        else if (std::sscanf(line.c_str(), "height=%d", &h) == 1)
            hasH = h > 0;
        else if (std::sscanf(line.c_str(), "maximized=%d", &max) == 1)
            s.maximized = max != 0;
    }
    if (hasX && hasY && hasW && hasH) {
        s.hasBounds = true;
        s.x = x;
        s.y = y;
        s.width = (uint32_t)w;
        s.height = (uint32_t)h;
    }
    return s;
}

std::string formatWindowPlacement(const WindowPlacementState& s)
{
    std::ostringstream out;
    out << "zoom=" << parseSavedZoom("zoom=" + std::to_string(s.zoom)) << "\n";
    if (s.hasBounds && s.width > 0 && s.height > 0) {
        out << "x=" << s.x << "\n"
            << "y=" << s.y << "\n"
            << "width=" << s.width << "\n"
            << "height=" << s.height << "\n"
            << "maximized=" << (s.maximized ? 1 : 0) << "\n";
    }
    return out.str();
}

WindowPlacementState loadWindowPlacement(const std::string& config)
{
    if (config.empty())
        return {};
    std::ifstream f(config + "/window.txt");
    if (!f)
        return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return parseWindowPlacement(ss.str());
}

bool saveWindowPlacement(const std::string& config, const WindowPlacementState& s)
{
    if (config.empty())
        return false;
    std::error_code ec;
    std::filesystem::create_directories(config, ec);
    const std::string path = config + "/window.txt", tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        f << formatWindowPlacement(s);
        if (!f)
            return false;
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::remove(tmp.c_str());
        std::ofstream f(path, std::ios::trunc);
        f << formatWindowPlacement(s);
        return (bool)f;
    }
    return true;
}

void centerWindowRect(int workLeft, int workTop, int workRight, int workBottom, int winW, int winH, int& outX, int& outY)
{
    const int workW = std::max(1, workRight - workLeft);
    const int workH = std::max(1, workBottom - workTop);
    outX = workLeft + std::max(0, (workW - winW) / 2);
    outY = workTop + std::max(0, (workH - winH) / 2);
}

void clampWindowRectToWorkArea(int workLeft, int workTop, int workRight, int workBottom, int& x, int& y, uint32_t& w, uint32_t& h)
{
    const int workW = std::max(1, workRight - workLeft);
    const int workH = std::max(1, workBottom - workTop);
    w = std::min<uint32_t>(w, (uint32_t)workW);
    h = std::min<uint32_t>(h, (uint32_t)workH);
    if (x + (int)w > workRight)
        x = workRight - (int)w;
    if (y + (int)h > workBottom)
        y = workBottom - (int)h;
    if (x < workLeft)
        x = workLeft;
    if (y < workTop)
        y = workTop;
}

void applyWindowPlacementToNativeWindow(uintptr_t nativeWindowHandle, const WindowPlacementState& wp)
{
#ifdef _WIN32
    HWND hwnd = reinterpret_cast<HWND>(nativeWindowHandle);
    if (hwnd == nullptr || !IsWindow(hwnd))
        return;
    if (wp.hasBounds && wp.width > 0 && wp.height > 0) {
        RECT rc { (LONG)wp.x, (LONG)wp.y, (LONG)(wp.x + (int)wp.width), (LONG)(wp.y + (int)wp.height) };
        MONITORINFO mInfo {};
        mInfo.cbSize = sizeof(mInfo);
        int x = wp.x, y = wp.y;
        uint32_t w = wp.width, h = wp.height;
        if (GetMonitorInfo(MonitorFromRect(&rc, MONITOR_DEFAULTTONEAREST), &mInfo))
            clampWindowRectToWorkArea(mInfo.rcWork.left, mInfo.rcWork.top, mInfo.rcWork.right, mInfo.rcWork.bottom, x, y, w, h);
        if (IsZoomed(hwnd) || IsIconic(hwnd))
            ShowWindow(hwnd, SW_RESTORE);
        SetWindowPos(hwnd, nullptr, x, y, (int)w, (int)h, SWP_NOZORDER | SWP_NOACTIVATE);
        if (wp.maximized)
            ShowWindow(hwnd, SW_MAXIMIZE);
        return;
    }
    MONITORINFO mInfo {};
    mInfo.cbSize = sizeof(mInfo);
    RECT winRect {};
    if (GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mInfo) && GetWindowRect(hwnd, &winRect)) {
        int cx = 0, cy = 0;
        centerWindowRect(mInfo.rcWork.left, mInfo.rcWork.top, mInfo.rcWork.right, mInfo.rcWork.bottom,
                         winRect.right - winRect.left, winRect.bottom - winRect.top, cx, cy);
        SetWindowPos(hwnd, nullptr, cx, cy, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (wp.maximized)
        ShowWindow(hwnd, SW_MAXIMIZE);
#else
    (void)nativeWindowHandle;
    (void)wp;
#endif
}

bool queryWindowPlacementFromNativeWindow(uintptr_t nativeWindowHandle, WindowPlacementState& wp)
{
#ifdef _WIN32
    HWND hwnd = reinterpret_cast<HWND>(nativeWindowHandle);
    if (hwnd == nullptr || !IsWindow(hwnd) || IsIconic(hwnd))
        return false;
    const bool max = IsZoomed(hwnd) != FALSE;
    wp.maximized = max;
    if (!max) {
        RECT rc {};
        if (GetWindowRect(hwnd, &rc) && rc.right > rc.left && rc.bottom > rc.top) {
            wp.hasBounds = true;
            wp.x = rc.left;
            wp.y = rc.top;
            wp.width = (uint32_t)(rc.right - rc.left);
            wp.height = (uint32_t)(rc.bottom - rc.top);
        }
    } else if (!wp.hasBounds) {
        WINDOWPLACEMENT wpl {};
        wpl.length = sizeof(WINDOWPLACEMENT);
        if (GetWindowPlacement(hwnd, &wpl) && wpl.rcNormalPosition.right > wpl.rcNormalPosition.left
            && wpl.rcNormalPosition.bottom > wpl.rcNormalPosition.top) {
            wp.hasBounds = true;
            wp.x = wpl.rcNormalPosition.left;
            wp.y = wpl.rcNormalPosition.top;
            wp.width = (uint32_t)(wpl.rcNormalPosition.right - wpl.rcNormalPosition.left);
            wp.height = (uint32_t)(wpl.rcNormalPosition.bottom - wpl.rcNormalPosition.top);
        }
    }
    return wp.hasBounds;
#else
    (void)nativeWindowHandle;
    (void)wp;
    return false;
#endif
}

int loadSavedZoom(const std::string& config)
{
    return loadWindowPlacement(config).zoom;
}

bool saveZoom(const std::string& config, int zoom)
{
    WindowPlacementState s = loadWindowPlacement(config);
    s.zoom = zoom;
    return saveWindowPlacement(config, s);
}

UiState initialUiState(const std::string& config)
{
    UiState s;
    s.zoom = loadSavedZoom(config);
    return s;
}
}
