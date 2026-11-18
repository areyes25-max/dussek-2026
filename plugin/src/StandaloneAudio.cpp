#include "StandaloneAudio.hpp"

#include "Paths.hpp"

#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>

namespace pe {
namespace {

std::mutex& backendMutex() {
    static std::mutex m;
    return m;
}

StandaloneAudioBackend& backendInstance() {
    static StandaloneAudioBackend b;
    return b;
}

std::string trim(const std::string& s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

}  // namespace

StandaloneAudioConfig parseStandaloneAudioConfig(const std::string& text) {
    StandaloneAudioConfig cfg;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const auto trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#')
            continue;
        const auto eq = trimmed.find('=');
        if (eq == std::string::npos)
            continue;
        const std::string key = trim(trimmed.substr(0, eq));
        const std::string val = trim(trimmed.substr(eq + 1));
        if (key == "engine") {
            cfg.engine = val;
        } else if (key == "device") {
            cfg.device = val;
        } else if (key == "buffer") {
            char* end = nullptr;
            const unsigned long parsed = std::strtoul(val.c_str(), &end, 10);
            if (end != val.c_str() && parsed > 0 && parsed <= 65536)
                cfg.bufferSize = static_cast<uint32_t>(parsed);
        }
    }
    return cfg;
}

std::string formatStandaloneAudioConfig(const StandaloneAudioConfig& cfg) {
    std::ostringstream out;
    out << "engine=" << cfg.engine << "\n";
    out << "device=" << cfg.device << "\n";
    if (cfg.bufferSize > 0)
        out << "buffer=" << cfg.bufferSize << "\n";
    return out.str();
}

std::filesystem::path standaloneAudioConfigPath() {
    const auto dir = dataDirs().config;
    if (dir.empty())
        return {};
    return std::filesystem::path(dir) / "audio.txt";
}

StandaloneAudioConfig loadStandaloneAudioConfig() {
    const auto path = standaloneAudioConfigPath();
    if (path.empty())
        return {};
    std::ifstream in(path);
    if (!in)
        return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return parseStandaloneAudioConfig(ss.str());
}

void saveStandaloneAudioConfig(const StandaloneAudioConfig& cfg) {
    const auto path = standaloneAudioConfigPath();
    if (path.empty())
        return;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (!out)
        return;
    out << formatStandaloneAudioConfig(cfg);
}

void saveStandaloneBufferSize(uint32_t frames) {
    if (frames == 0)
        return;
    auto cfg = loadStandaloneAudioConfig();
    if (cfg.engine.empty())
        cfg.engine = currentStandaloneAudioEngine();
    if (cfg.device.empty())
        cfg.device = currentStandaloneAudioDevice();
    cfg.bufferSize = frames;
    saveStandaloneAudioConfig(cfg);
}

void setStandaloneAudioBackend(StandaloneAudioBackend backend) {
    std::lock_guard<std::mutex> lock(backendMutex());
    backendInstance() = std::move(backend);
}

bool hasStandaloneAudioBackend() {
    std::lock_guard<std::mutex> lock(backendMutex());
    return static_cast<bool>(backendInstance().getEngines);
}

std::vector<std::string> standaloneAudioEngines() {
    std::function<std::vector<std::string>()> fn;
    {
        std::lock_guard<std::mutex> lock(backendMutex());
        fn = backendInstance().getEngines;
    }
    if (fn)
        return fn();
    return {};
}

std::string currentStandaloneAudioEngine() {
    std::function<std::string()> fn;
    {
        std::lock_guard<std::mutex> lock(backendMutex());
        fn = backendInstance().getCurrentEngine;
    }
    if (fn)
        return fn();
    return {};
}

bool selectStandaloneAudioEngine(const std::string& engine, std::string* errorOut) {
    std::function<bool(const std::string&, std::string*)> fn;
    {
        std::lock_guard<std::mutex> lock(backendMutex());
        fn = backendInstance().selectEngine;
    }
    if (!fn) {
        if (errorOut)
            *errorOut = "Audio backend is not available";
        return false;
    }
    return fn(engine, errorOut);
}

std::vector<std::string> standaloneAudioDevices() {
    std::function<std::vector<std::string>()> fn;
    {
        std::lock_guard<std::mutex> lock(backendMutex());
        fn = backendInstance().getDevices;
    }
    if (fn)
        return fn();
    return {};
}

std::string currentStandaloneAudioDevice() {
    std::function<std::string()> fn;
    {
        std::lock_guard<std::mutex> lock(backendMutex());
        fn = backendInstance().getCurrentDevice;
    }
    if (fn)
        return fn();
    return {};
}

bool selectStandaloneAudioDevice(const std::string& device, std::string* errorOut) {
    std::function<bool(const std::string&, std::string*)> fn;
    {
        std::lock_guard<std::mutex> lock(backendMutex());
        fn = backendInstance().selectDevice;
    }
    if (!fn) {
        if (errorOut)
            *errorOut = "Audio backend is not available";
        return false;
    }
    return fn(device, errorOut);
}

bool hasStandaloneAudioControlPanel() {
    std::function<bool()> fn;
    {
        std::lock_guard<std::mutex> lock(backendMutex());
        fn = backendInstance().hasControlPanel;
    }
    if (fn)
        return fn();
    return false;
}

bool openStandaloneAudioControlPanel() {
    std::function<bool()> fn;
    {
        std::lock_guard<std::mutex> lock(backendMutex());
        fn = backendInstance().openControlPanel;
    }
    if (fn)
        return fn();
    return false;
}

std::vector<std::string> standaloneMidiInputs() {
    std::function<std::vector<std::string>()> fn;
    {
        std::lock_guard<std::mutex> lock(backendMutex());
        fn = backendInstance().getMidiInputs;
    }
    if (fn)
        return fn();
    return {};
}

}  // namespace pe
