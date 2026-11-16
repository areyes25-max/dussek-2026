#include "Paths.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif defined(__APPLE__)
#include <climits>
#include <mach-o/dyld.h>
#else
#include <climits>
#include <unistd.h>
#endif
#include <filesystem>

namespace pe {

Platform hostPlatform()
{
#if defined(_WIN32)
    return Platform::Windows;
#elif defined(__APPLE__)
    return Platform::MacOS;
#else
    return Platform::Linux;
#endif
}

DataDirs dataDirs(const std::function<const char*(const char*)>& env, Platform platform)
{
    const auto get = [&](const char* name) {
        const char* v = env(name);
        return std::string(v != nullptr ? v : "");
    };
    DataDirs d;
    switch (platform) {
    case Platform::Windows:
        if (const std::string app = get("APPDATA"); !app.empty())
            d.data = d.config = app + "/Dussek";
        break;
    case Platform::MacOS:
        if (const std::string home = get("HOME"); !home.empty())
            d.data = d.config = home + "/Library/Application Support/Dussek";
        break;
    case Platform::Linux: {
        // the XDG spec: a relative path is invalid and ignored
        const auto xdg = [&](const char* name, const char* fallback) {
            if (const std::string v = get(name); !v.empty() && v[0] == '/')
                return v + "/dussek";
            if (const std::string home = get("HOME"); !home.empty())
                return home + fallback;
            return std::string();
        };
        d.data = xdg("XDG_DATA_HOME", "/.local/share/dussek");
        d.config = xdg("XDG_CONFIG_HOME", "/.config/dussek");
        break;
    }
    }
    return d;
}

std::string standaloneExecutable(PluginKind k, const std::string& self, bool selfIsStandalone, const char* path,
    const std::vector<std::string>& installed)
{
    namespace fs = std::filesystem;
    if (self.empty())
        return std::string();
    if (selfIsStandalone)
        return self;
    const std::string exe = identity(k).executable, app = identity(k).name;
#ifdef _WIN32
    const std::string names[] = { exe + ".exe" };
    const char sep = ';';
#else
    const std::string names[] = { exe, app + ".app/Contents/MacOS/" + exe, exe + ".app/Contents/MacOS/" + exe };
    const char sep = ':';
#endif
    std::error_code ec;
    const fs::path me = fs::weakly_canonical(fs::path(self), ec);
    const auto standalone = [&](const fs::path& dir) -> std::string {
        for (const std::string& n : names) {
            const fs::path c = dir / n;
            std::error_code e;
            // not the plugin itself (a macOS VST3's or CLAP's binary is named dussek too)
            if (fs::is_regular_file(c, e) && !fs::equivalent(c, me, e))
                return c.string();
        }
        return std::string();
    };
    fs::path dir = me.parent_path();
    for (int up = 0; up < 5 && !dir.empty(); ++up, dir = dir.parent_path()) {
        if (std::string s = standalone(dir); !s.empty())
            return s;
        if (dir == dir.root_path())
            break;
    }
    for (const std::string& d : installed)
        if (std::string s = standalone(d); !s.empty())
            return s;
    for (std::string rest = path != nullptr ? path : ""; !rest.empty();) {
        const size_t end = rest.find(sep);
        const std::string d = rest.substr(0, end);
        rest = end == std::string::npos ? std::string() : rest.substr(end + 1);
        if (!d.empty())
            if (std::string s = standalone(d); !s.empty())
                return s;
    }
    return std::string();
}

std::string executablePath()
{
#ifdef _WIN32
    char buf[MAX_PATH];
    const DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return n > 0 && n < MAX_PATH ? std::string(buf, n) : std::string();
#elif defined(__APPLE__)
    char buf[PATH_MAX];
    uint32_t size = sizeof(buf);
    return _NSGetExecutablePath(buf, &size) == 0 ? std::string(buf) : std::string();
#else
    char buf[PATH_MAX];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf));
    return n > 0 && (size_t)n < sizeof(buf) ? std::string(buf, (size_t)n) : std::string();
#endif
}

std::string portableFolder(const std::string& exe)
{
    const size_t cut = exe.find_last_of("/\\");
    if (cut == std::string::npos)
        return std::string();
    std::string name = exe.substr(cut + 1);
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    bool ours = false;
    for (PluginKind k : { PluginKind::Keys, PluginKind::Winds })
        ours = ours || name == identity(k).executable || name == std::string(identity(k).executable) + ".exe";
    if (!ours)
        return std::string();
    std::string dir = exe.substr(0, cut);
    // macOS: <folder>/Dussek.app/Contents/MacOS/dussek
    static const std::string inApp = "/Contents/MacOS";
    if (dir.size() > inApp.size() && dir.compare(dir.size() - inApp.size(), inApp.size(), inApp) == 0) {
        const std::string app = dir.substr(0, dir.size() - inApp.size());
        if (app.size() > 4 && app.compare(app.size() - 4, 4, ".app") == 0)
            if (const size_t up = app.find_last_of('/'); up != std::string::npos)
                return app.substr(0, up);
    }
    return dir;
}

std::vector<std::string> installedProgramDirs(PluginKind k)
{
    switch (hostPlatform()) {
    case Platform::Windows:
        if (const char* pf = std::getenv("PROGRAMFILES"); pf != nullptr && *pf)
            return { std::string(pf) + "/" + identity(k).name };
        return {};
    case Platform::MacOS:
        return { "/Applications" };
    case Platform::Linux:
        break;
    }
    return {};
}

DataDirs dataDirs()
{
    return dataDirs([](const char* name) { return static_cast<const char*>(std::getenv(name)); }, hostPlatform());
}

std::vector<std::string> systemDataDirs(const std::function<const char*(const char*)>& env, Platform platform)
{
    const auto get = [&](const char* name) {
        const char* v = env(name);
        return std::string(v != nullptr ? v : "");
    };
    std::vector<std::string> dirs;
    switch (platform) {
    case Platform::Windows:
        if (const std::string all = get("PROGRAMDATA"); !all.empty())
            dirs.push_back(all + "/Dussek");
        break;
    case Platform::MacOS:
        dirs.push_back("/Library/Application Support/Dussek");
        break;
    case Platform::Linux: {
        // the XDG spec: a colon-separated list, relative entries ignored, unset or empty the default
        const std::string list = get("XDG_DATA_DIRS");
        size_t start = 0;
        while (start <= list.size()) {
            const size_t end = std::min(list.find(':', start), list.size());
            std::string entry = list.substr(start, end - start);
            while (entry.size() > 1 && entry.back() == '/')
                entry.pop_back();
            if (!entry.empty() && entry[0] == '/')
                dirs.push_back(entry + "/dussek");
            start = end + 1;
        }
        // install.sh --system's folder and the distribution's, when the list leaves them out
        for (const char* fallback : { "/usr/local/share/dussek", "/usr/share/dussek" })
            if (std::find(dirs.begin(), dirs.end(), fallback) == dirs.end())
                dirs.push_back(fallback);
        break;
    }
    }
    return dirs;
}

std::vector<std::string> systemDataDirs()
{
    return systemDataDirs([](const char* name) { return static_cast<const char*>(std::getenv(name)); }, hostPlatform());
}

std::vector<std::string> bundleDataDirs(const std::string& bundle, Platform platform)
{
    std::vector<std::string> dirs;
    if (platform != Platform::Linux || bundle.empty() || bundle[0] != '/')
        return dirs;
    const auto add = [&](const std::string& d) {
        if (std::find(dirs.begin(), dirs.end(), d) == dirs.end())
            dirs.push_back(d);
    };
    // each folder from the bundle up: the format's folder, then what holds it
    std::string dir = bundle;
    while (dir.size() > 1 && dir.back() == '/')
        dir.pop_back();
    for (int up = 0; up < 8 && dir.size() > 1; ++up) {
        const size_t cut = dir.find_last_of('/');
        const std::string name = dir.substr(cut + 1), parent = cut == 0 ? std::string("/") : dir.substr(0, cut);
        if (name == ".lv2" || name == ".vst3" || name == ".clap") {
            add((parent == "/" ? std::string() : parent) + "/.local/share/dussek");
        } else if ((name == "lv2" || name == "vst3" || name == "clap") && parent != "/") {
            // <prefix>/lib (lib64, lib32) or a Debian multiarch <prefix>/lib/<triplet>
            const auto split = [](const std::string& d, std::string& base) {
                const size_t c = d.find_last_of('/');
                base = d.substr(c + 1);
                return c == 0 || c == std::string::npos ? std::string() : d.substr(0, c);
            };
            std::string lib, above;
            std::string prefix = split(parent, lib);
            if (lib.rfind("lib", 0) != 0 && !prefix.empty()) {
                const std::string up2 = split(prefix, above);
                prefix = above == "lib" ? up2 : std::string();
                lib = above;
            }
            if (lib.rfind("lib", 0) == 0 && !prefix.empty()) {
                add(prefix + "/share/dussek");
                if (prefix.size() >= 4 && prefix.compare(prefix.size() - 4, 4, "/usr") == 0)
                    add(prefix + "/local/share/dussek");
            }
        }
        dir = parent;
    }
    return dirs;
}

std::vector<std::string> sandboxDataDirs(const std::function<const char*(const char*)>& env, Platform platform)
{
    std::vector<std::string> dirs;
    if (platform != Platform::Linux)
        return dirs;
    const auto get = [&](const char* name) {
        const char* v = env(name);
        return std::string(v != nullptr ? v : "");
    };
    if (!get("FLATPAK_ID").empty()) {
        if (const std::string home = get("HOME"); !home.empty() && home[0] == '/')
            dirs.push_back(home + "/.local/share/dussek");
        dirs.push_back("/run/host/usr/local/share/dussek");
        dirs.push_back("/run/host/usr/share/dussek");
    }
    if (const std::string real = get("SNAP_REAL_HOME"); !real.empty() && real[0] == '/')
        dirs.push_back(real + "/.local/share/dussek");
    return dirs;
}

std::vector<std::string> sandboxDataDirs()
{
    return sandboxDataDirs([](const char* name) { return static_cast<const char*>(std::getenv(name)); }, hostPlatform());
}

std::string folderOf(const std::string& file)
{
#ifdef _WIN32
    const size_t cut = file.find_last_of("/\\");
#else
    const size_t cut = file.find_last_of('/');
#endif
    if (cut == std::string::npos)
        return std::string();
    if (cut == 0)
        return file.substr(0, 1); // "/"
#ifdef _WIN32
    if (cut == 2 && file[1] == ':')
        return file.substr(0, 3); // "C:\"
#endif
    return file.substr(0, cut);
}

std::string fileNameOf(const std::string& file)
{
#ifdef _WIN32
    const size_t cut = file.find_last_of("/\\");
#else
    const size_t cut = file.find_last_of('/');
#endif
    return cut == std::string::npos ? file : file.substr(cut + 1);
}

} // namespace pe
