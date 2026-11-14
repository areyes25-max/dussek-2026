#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace pe {

// Persisted standalone audio settings stored in <config>/audio.txt.
struct StandaloneAudioConfig {
    std::string engine;
    std::string device;
    uint32_t bufferSize = 0;
};

StandaloneAudioConfig parseStandaloneAudioConfig(const std::string& text);
std::string formatStandaloneAudioConfig(const StandaloneAudioConfig& cfg);

std::filesystem::path standaloneAudioConfigPath();
StandaloneAudioConfig loadStandaloneAudioConfig();
void saveStandaloneAudioConfig(const StandaloneAudioConfig& cfg);
void saveStandaloneBufferSize(uint32_t frames);

// Runtime callbacks registered by the standalone RtAudio bridge.
// In plugin builds (CLAP/VST3/LV2) and unit tests without a registered bridge,
// these return sensible defaults without touching audio hardware.
struct StandaloneAudioBackend {
    std::function<std::vector<std::string>()> getEngines;
    std::function<std::string()> getCurrentEngine;
    std::function<bool(const std::string& engine, std::string* errorOut)> selectEngine;

    std::function<std::vector<std::string>()> getDevices;
    std::function<std::string()> getCurrentDevice;
    std::function<bool(const std::string& device, std::string* errorOut)> selectDevice;

    std::function<bool()> hasControlPanel;
    std::function<bool()> openControlPanel;
};

void setStandaloneAudioBackend(StandaloneAudioBackend backend);
bool hasStandaloneAudioBackend();

std::vector<std::string> standaloneAudioEngines();
std::string currentStandaloneAudioEngine();
bool selectStandaloneAudioEngine(const std::string& engine, std::string* errorOut = nullptr);

std::vector<std::string> standaloneAudioDevices();
std::string currentStandaloneAudioDevice();
bool selectStandaloneAudioDevice(const std::string& device, std::string* errorOut = nullptr);

bool hasStandaloneAudioControlPanel();
bool openStandaloneAudioControlPanel();

}  // namespace pe
