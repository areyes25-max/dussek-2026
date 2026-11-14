#ifndef RTAUDIO_BRIDGE_HPP_INCLUDED
#define RTAUDIO_BRIDGE_HPP_INCLUDED

#include "src/jackbridge/NativeBridge.hpp"
#include "StandaloneAudio.hpp"

#if defined(DISTRHO_OS_WINDOWS)
# define __WINDOWS_WASAPI__
# define __WINDOWS_ASIO__
# define __WINDOWS_DS__
# define __WINDOWS_MM__
# define RTAUDIO_API_TYPE WINDOWS_WASAPI
# define RTMIDI_API_TYPE WINDOWS_MM
#elif defined(DISTRHO_OS_MAC)
# define __MACOSX_CORE__
# define RTAUDIO_API_TYPE MACOSX_CORE
# define RTMIDI_API_TYPE MACOSX_CORE
#elif defined(DISTRHO_OS_LINUX)
# if defined(HAVE_ALSA) && defined(HAVE_PULSEAUDIO)
#  define __LINUX_PULSE__
#  define __LINUX_ALSA__
#  define RTAUDIO_API_TYPE LINUX_PULSE
#  define RTMIDI_API_TYPE LINUX_ALSA
# elif defined(HAVE_ALSA)
#  define __LINUX_ALSA__
#  define RTAUDIO_API_TYPE LINUX_ALSA
#  define RTMIDI_API_TYPE LINUX_ALSA
# elif defined(HAVE_PULSEAUDIO)
#  define __LINUX_PULSE__
#  define RTAUDIO_API_TYPE LINUX_PULSE
# else
#  define __RTAUDIO_DUMMY__
#  define __RTMIDI_DUMMY__
# endif
#endif

#ifndef __RTMIDI_DUMMY__
# define DISTRHO_RTAUDIO_HAVE_MIDI 1
#else
# define DISTRHO_RTAUDIO_HAVE_MIDI 0
#endif

#define RTMIDI_DO_NOT_ENSURE_UNIQUE_PORTNAMES

#include "rtaudio/RtAudio.h"
#include "rtmidi/RtMidi.h"

#if defined(DISTRHO_OS_WINDOWS)
# include "asiodrivers.h"
#endif

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

void notifyStandaloneSampleRateChanged(void* arg, jack_nframes_t sampleRate);

struct RtAudioBridge : NativeBridge {
    // Framestorage provided by RtAudio
    float* frameBuffer = nullptr;

    RtAudio* handle = nullptr;
    RtAudio::Api currentApi = RtAudio::UNSPECIFIED;
    std::string currentDevice;
    std::string lastError;
    bool captureEnabled = false;
    uint streamInChannels = 0;
    uint streamOutChannels = 0;

    using SampleRateChangedCallback = void (*)(void* arg, jack_nframes_t sampleRate);
    SampleRateChangedCallback sampleRateChangedCallback = notifyStandaloneSampleRateChanged;

#if DISTRHO_RTAUDIO_HAVE_MIDI
# if DISTRHO_PLUGIN_WANT_MIDI_INPUT
    static constexpr const uint kMaxMIDIInputPorts = 64;
    uint numMIDIInPorts = 0;
    struct MIDIInPorts {
        RtMidiIn* handle = nullptr;
        RtMidiIn** handlePtr = nullptr;
    } midiInPorts[kMaxMIDIInputPorts];
    std::vector<std::string> midiInPortNames;
# endif
# if DISTRHO_PLUGIN_WANT_MIDI_OUTPUT
    RtMidiOut* midiOut = nullptr;
# endif
#endif

    RtAudioBridge()
    {
#if DISTRHO_RTAUDIO_HAVE_MIDI && (DISTRHO_PLUGIN_WANT_MIDI_INPUT || DISTRHO_PLUGIN_WANT_MIDI_OUTPUT)
        midiAvailable = true;
#endif
#if DISTRHO_RTAUDIO_HAVE_MIDI && DISTRHO_PLUGIN_WANT_MIDI_INPUT
        for (uint i = 0; i < kMaxMIDIInputPorts; ++i)
            midiInPorts[i].handlePtr = &midiInPorts[i].handle;
#endif
    }

    ~RtAudioBridge() override
    {
        pe::setStandaloneAudioBackend({});
    }

    static std::string apiToDisplayName(const RtAudio::Api api)
    {
        switch (api)
        {
        case RtAudio::WINDOWS_WASAPI: return "WASAPI";
        case RtAudio::WINDOWS_ASIO:   return "ASIO";
        case RtAudio::WINDOWS_DS:     return "DirectSound";
        case RtAudio::LINUX_PULSE:    return "PulseAudio";
        case RtAudio::LINUX_ALSA:     return "ALSA";
        case RtAudio::UNIX_JACK:      return "JACK";
        case RtAudio::MACOSX_CORE:    return "CoreAudio";
        case RtAudio::RTAUDIO_DUMMY:  return "Dummy";
        default:                      return "Default";
        }
    }

    static RtAudio::Api displayNameToApi(const std::string& name)
    {
        std::vector<RtAudio::Api> apis;
        RtAudio::getCompiledApi(apis);
        for (const auto api : apis)
        {
            if (apiToDisplayName(api) == name)
                return api;
        }
        return RtAudio::UNSPECIFIED;
    }

    static std::vector<RtAudio::Api> compiledApis()
    {
        std::vector<RtAudio::Api> apis;
        RtAudio::getCompiledApi(apis);
        const auto it = std::find(apis.begin(), apis.end(), RtAudio::WINDOWS_WASAPI);
        if (it != apis.end() && it != apis.begin())
        {
            apis.erase(it);
            apis.insert(apis.begin(), RtAudio::WINDOWS_WASAPI);
        }
        return apis;
    }

    std::vector<std::string> getEngineNames() const
    {
        std::vector<std::string> names;
        for (const auto api : compiledApis())
            names.push_back(apiToDisplayName(api));
        return names;
    }

    std::string getCurrentEngineName() const
    {
        if (handle != nullptr)
            return apiToDisplayName(handle->getCurrentApi());
        if (currentApi != RtAudio::UNSPECIFIED)
            return apiToDisplayName(currentApi);
        const auto apis = compiledApis();
        return apis.empty() ? std::string("Default") : apiToDisplayName(apis.front());
    }

    std::string getCurrentDeviceName() const
    {
        return currentDevice.empty() ? std::string("Default") : currentDevice;
    }

    std::vector<std::string> getDeviceNamesForApi(const RtAudio::Api api)
    {
        std::vector<std::string> names;
#if defined(DISTRHO_OS_WINDOWS)
        if (api == RtAudio::WINDOWS_ASIO)
        {
            AsioDrivers asio;
            const long count = asio.asioGetNumDev();
            for (long i = 0; i < count; ++i)
            {
                char drvName[64] = {};
                if (asio.asioGetDriverName(static_cast<int>(i), drvName, sizeof(drvName)) == ASE_OK && drvName[0] != '\0')
                {
                    if (std::find(names.begin(), names.end(), drvName) == names.end())
                        names.push_back(drvName);
                }
            }
            return names;
        }
#endif
        if (api != RtAudio::WINDOWS_ASIO)
            names.push_back("Default");

        if (handle != nullptr && handle->getCurrentApi() == api)
        {
            const uint count = handle->getDeviceCount();
            for (uint i = 0; i < count; ++i)
            {
                try {
                    const RtAudio::DeviceInfo info = handle->getDeviceInfo(i);
                    if (!info.name.empty() && (api == RtAudio::WINDOWS_ASIO || info.outputChannels > 0 || info.probed))
                    {
                        if (std::find(names.begin(), names.end(), info.name) == names.end())
                            names.push_back(info.name);
                    }
                } catch (...) {}
            }
            return names;
        }

        try {
            RtAudio temp(api);
            const uint count = temp.getDeviceCount();
            for (uint i = 0; i < count; ++i)
            {
                try {
                    const RtAudio::DeviceInfo info = temp.getDeviceInfo(i);
                    if (!info.name.empty() && (api == RtAudio::WINDOWS_ASIO || info.outputChannels > 0 || info.probed))
                    {
                        if (std::find(names.begin(), names.end(), info.name) == names.end())
                            names.push_back(info.name);
                    }
                } catch (...) {}
            }
        } catch (...) {}

        return names;
    }

    std::vector<std::string> getDeviceNames()
    {
        const RtAudio::Api api = (handle != nullptr) ? handle->getCurrentApi() : currentApi;
        return getDeviceNamesForApi(api);
    }

    void registerStandaloneBackend()
    {
        pe::StandaloneAudioBackend backend;
        backend.getEngines = [this]() {
            return getEngineNames();
        };
        backend.getCurrentEngine = [this]() {
            return getCurrentEngineName();
        };
        backend.selectEngine = [this](const std::string& engineName, std::string* errorOut) {
            const RtAudio::Api targetApi = displayNameToApi(engineName);
            if (targetApi == RtAudio::UNSPECIFIED)
            {
                if (errorOut != nullptr)
                    *errorOut = "Unknown audio engine: " + engineName;
                return false;
            }
            const bool ok = switchAudio(targetApi, std::string());
            if (!ok && errorOut != nullptr)
                *errorOut = lastError.empty() ? ("Could not initialize " + engineName) : lastError;
            return ok;
        };
        backend.getDevices = [this]() {
            return getDeviceNames();
        };
        backend.getCurrentDevice = [this]() {
            return getCurrentDeviceName();
        };
        backend.selectDevice = [this](const std::string& deviceName, std::string* errorOut) {
            const RtAudio::Api api = (handle != nullptr) ? handle->getCurrentApi() : currentApi;
            const std::string wanted = (deviceName == "Default") ? std::string() : deviceName;
            const bool ok = switchAudio(api, wanted);
            if (!ok && errorOut != nullptr)
                *errorOut = lastError.empty() ? ("Could not open " + deviceName) : lastError;
            return ok;
        };
        backend.hasControlPanel = [this]() {
#if defined(DISTRHO_OS_WINDOWS)
            return handle != nullptr
                && handle->getCurrentApi() == RtAudio::WINDOWS_ASIO
                && AsioDrivers::currentDriver() != nullptr;
#else
            return false;
#endif
        };
        backend.openControlPanel = [this]() {
#if defined(DISTRHO_OS_WINDOWS)
            if (handle != nullptr
                && handle->getCurrentApi() == RtAudio::WINDOWS_ASIO
                && AsioDrivers::currentDriver() != nullptr)
            {
                return ASIOControlPanel() == ASE_OK;
            }
#endif
            return false;
        };
        backend.getMidiInputs = [this]() {
#if DISTRHO_RTAUDIO_HAVE_MIDI && DISTRHO_PLUGIN_WANT_MIDI_INPUT
            return midiInPortNames;
#else
            return std::vector<std::string>{};
#endif
        };
        pe::setStandaloneAudioBackend(std::move(backend));
    }

    bool open(const char* const clientName) override
    {
        const pe::StandaloneAudioConfig saved = pe::loadStandaloneAudioConfig();

        uint32_t initialBuffer = saved.bufferSize;
        if (initialBuffer == 0)
            initialBuffer = 512;

        const auto apis = compiledApis();
        RtAudio::Api preferredApi = apis.empty() ? RtAudio::UNSPECIFIED : apis.front();
        if (!saved.engine.empty())
        {
            const RtAudio::Api parsed = displayNameToApi(saved.engine);
            if (parsed != RtAudio::UNSPECIFIED)
                preferredApi = parsed;
        }

        const std::string wantedDevice = (saved.device == "Default") ? std::string() : saved.device;

        bool opened = _open(clientName, initialBuffer, preferredApi, wantedDevice);
        if (!opened && !wantedDevice.empty())
            opened = _open(clientName, initialBuffer, preferredApi, std::string());

        if (!opened)
        {
            for (const auto fallbackApi : apis)
            {
                if (fallbackApi == preferredApi)
                    continue;
                if (_open(clientName, initialBuffer, fallbackApi, std::string()))
                {
                    opened = true;
                    break;
                }
            }
        }

        if (!opened)
            return false;

        registerStandaloneBackend();

#if DISTRHO_RTAUDIO_HAVE_MIDI && DISTRHO_PLUGIN_WANT_MIDI_INPUT
        openMIDIInPorts(clientName);
#endif
#if DISTRHO_RTAUDIO_HAVE_MIDI && DISTRHO_PLUGIN_WANT_MIDI_OUTPUT
        try {
            midiOut = new RtMidiOut(RtMidi::UNSPECIFIED, clientName);
        } catch (const RtMidiError& err) {
            d_stderr2("Failed to initialize MIDI out, reason: %s", err.what());
            return true;
        }
        midiOut->openPort(0, "events-out");
#endif

        return true;
    }

#if DISTRHO_RTAUDIO_HAVE_MIDI && DISTRHO_PLUGIN_WANT_MIDI_INPUT
    uint openMIDIInPorts(const char* const clientName)
    {
        midiInPortNames.clear();
        RtMidiIn* probe = nullptr;
        try {
            probe = new RtMidiIn(RtMidi::UNSPECIFIED, clientName);
        } catch (const RtMidiError& err) {
            d_stderr2("Failed to initialize MIDI in, reason: %s", err.what());
            return 0;
        }

        const uint portCount = std::min(kMaxMIDIInputPorts, probe->getPortCount());
        delete probe;

        if (portCount == 0)
            return 0;

        uint opened = 0;
        for (uint i = 0; i < portCount; ++i)
        {
            RtMidiIn* midiIn = nullptr;
            try {
                midiIn = new RtMidiIn(RtMidi::UNSPECIFIED, clientName);
                const std::string portName = midiIn->getPortName(i);
                d_stdout("Connecting to MIDI in port '%s'", portName.c_str());

                midiIn->ignoreTypes(true, true, true);
                midiIn->setCallback(RtMidiCallback, this);
                midiIn->openPort(i, "events-in");
                midiInPorts[opened].handle = midiIn;
                midiInPortNames.push_back(portName);
                ++opened;
            } catch (const RtMidiError& err) {
                d_stderr2("Failed to open MIDI in port %u, reason: %s", i, err.what());
                delete midiIn;
            }
        }

        return (numMIDIInPorts = opened);
    }
#endif

    void closeAudioStreamOnly()
    {
        DISTRHO_SAFE_ASSERT_RETURN(handle != nullptr,);

        if (handle->isStreamRunning())
        {
            try {
                handle->stopStream();
            } catch (...) {}
        }
        if (handle->isStreamOpen())
        {
            try {
                handle->closeStream();
            } catch (...) {}
        }

#if DISTRHO_PLUGIN_NUM_INPUTS + DISTRHO_PLUGIN_NUM_OUTPUTS > 0
        delete[] audioBufferStorage;
        audioBufferStorage = nullptr;
        std::memset(audioBuffers, 0, sizeof(audioBuffers));
#endif

        delete handle;
        handle = nullptr;
    }

    bool close() override
    {
        DISTRHO_SAFE_ASSERT_RETURN(handle != nullptr, false);

        closeAudioStreamOnly();

#if DISTRHO_RTAUDIO_HAVE_MIDI && DISTRHO_PLUGIN_WANT_MIDI_INPUT
        for (uint i = 0; i < numMIDIInPorts; ++i)
        {
            midiInPorts[i].handle->closePort();
            delete midiInPorts[i].handle;
            midiInPorts[i].handle = nullptr;
        }
        numMIDIInPorts = 0;
        midiInPortNames.clear();
#endif
#if DISTRHO_RTAUDIO_HAVE_MIDI && DISTRHO_PLUGIN_WANT_MIDI_OUTPUT
        if (midiOut != nullptr)
        {
            midiOut->closePort();
            delete midiOut;
            midiOut = nullptr;
        }
#endif
        freeBuffers();

        return true;
    }

    bool activate() override
    {
        DISTRHO_SAFE_ASSERT_RETURN(handle != nullptr, false);

        try {
            handle->startStream();
        } catch (const RtAudioError& err) {
            lastError = err.what();
            d_stderr2("Failed to start Audio stream, reason: %s", err.what());
            return false;
        }
        return true;
    }

    bool deactivate() override
    {
        DISTRHO_SAFE_ASSERT_RETURN(handle != nullptr, false);

        try {
            handle->stopStream();
        } catch (const RtAudioError& err) {
            lastError = err.what();
            d_stderr2("Failed to stop Audio stream, reason: %s", err.what());
            return false;
        }
        return true;
    }

    bool supportsAudioInput() const override
    {
        return true;
    }

    bool isAudioInputEnabled() const override
    {
        return captureEnabled;
    }

    bool supportsBufferSizeChanges() const override
    {
        return handle != nullptr;
    }

    bool isMIDIEnabled() const override
    {
#if DISTRHO_RTAUDIO_HAVE_MIDI && DISTRHO_PLUGIN_WANT_MIDI_INPUT
        if (numMIDIInPorts != 0)
            return true;
#endif
#if DISTRHO_RTAUDIO_HAVE_MIDI && DISTRHO_PLUGIN_WANT_MIDI_OUTPUT
        if (midiOut != nullptr)
            return true;
#endif
        return false;
    }

    bool requestAudioInput() override
    {
        return reinit(true, bufferSize);
    }

    bool requestBufferSizeChange(const uint32_t newBufferSize) override
    {
        const bool ok = reinit(captureEnabled, newBufferSize);
        if (ok && handle != nullptr)
            pe::saveStandaloneBufferSize(bufferSize);
        return ok;
    }

    bool requestMIDI() override
    {
#if DISTRHO_RTAUDIO_HAVE_MIDI && (DISTRHO_PLUGIN_WANT_MIDI_INPUT || DISTRHO_PLUGIN_WANT_MIDI_OUTPUT)
        char clientName[32];
        std::snprintf(clientName, sizeof(clientName), "%s", DISTRHO_PLUGIN_NAME);
        clientName[sizeof(clientName) - 1] = '\0';

# if DISTRHO_PLUGIN_WANT_MIDI_INPUT
        for (uint i = 0; i < numMIDIInPorts; ++i)
        {
            midiInPorts[i].handle->closePort();
            delete midiInPorts[i].handle;
            midiInPorts[i].handle = nullptr;
        }
        numMIDIInPorts = 0;
        openMIDIInPorts(clientName);
# endif

# if DISTRHO_PLUGIN_WANT_MIDI_OUTPUT
        if (midiOut == nullptr)
        {
            try {
                midiOut = new RtMidiOut(RtMidi::UNSPECIFIED, clientName);
            } catch (const RtMidiError& err) {
                d_stderr2("Failed to initialize MIDI out, reason: %s", err.what());
                return false;
            }
            midiOut->openPort(0, "events-out");
        }
# endif

        return isMIDIEnabled();
#else
        return false;
#endif
    }

    bool switchAudio(const RtAudio::Api targetApi, const std::string& targetDevice)
    {
        const RtAudio::Api prevApi = (handle != nullptr) ? handle->getCurrentApi() : currentApi;
        const std::string prevDevice = currentDevice;
        const uint32_t prevBufferSize = bufferSize;
        const uint32_t prevSampleRate = sampleRate;
        const bool audioIn = captureEnabled;

        char clientName[32];
        std::snprintf(clientName, sizeof(clientName), "%s", DISTRHO_PLUGIN_NAME);
        clientName[sizeof(clientName) - 1] = '\0';

        if (handle != nullptr)
            closeAudioStreamOnly();

        const uint32_t requestedBuffer = prevBufferSize != 0 ? prevBufferSize : 512;

        bool opened = _open(clientName, requestedBuffer, targetApi, targetDevice, audioIn);
        if (opened)
        {
            if (sampleRate != prevSampleRate && sampleRateChangedCallback != nullptr)
                sampleRateChangedCallback(jackProcessArg, sampleRate);

            if (bufferSize != prevBufferSize && bufferSizeCallback != nullptr)
                bufferSizeCallback(bufferSize, jackBufferSizeArg);

            if (!activate())
                opened = false;
        }

        if (!opened)
        {
            const std::string failedError = lastError;
            if (handle != nullptr)
                closeAudioStreamOnly();

            // Restore previous working engine and device
            if (prevApi != RtAudio::UNSPECIFIED)
            {
                const uint32_t restoreBuf = prevBufferSize != 0 ? prevBufferSize : 512;
                if (_open(clientName, restoreBuf, prevApi, prevDevice, audioIn))
                {
                    if (sampleRateChangedCallback != nullptr)
                        sampleRateChangedCallback(jackProcessArg, sampleRate);
                    if (bufferSizeCallback != nullptr)
                        bufferSizeCallback(bufferSize, jackBufferSizeArg);
                    activate();
                }
            }
            lastError = failedError;
            return false;
        }

        pe::StandaloneAudioConfig cfg = pe::loadStandaloneAudioConfig();
        cfg.engine = getCurrentEngineName();
        cfg.device = getCurrentDeviceName();
        if (supportsBufferSizeChanges())
            cfg.bufferSize = bufferSize;
        pe::saveStandaloneAudioConfig(cfg);

        return true;
    }

    bool reinit(const bool audioIn, const uint32_t newBufferSize)
    {
        DISTRHO_SAFE_ASSERT_RETURN(handle != nullptr, false);

        const RtAudio::Api activeApi = handle->getCurrentApi();
        const std::string activeDevice = currentDevice;
        const uint32_t oldBufferSize = bufferSize;
        const uint32_t oldSampleRate = sampleRate;

        const bool oldAudioIn = captureEnabled;

        closeAudioStreamOnly();

        char clientName[32];
        std::snprintf(clientName, sizeof(clientName), "%s", DISTRHO_PLUGIN_NAME);
        clientName[sizeof(clientName) - 1] = '\0';

        bool ok = _open(clientName, newBufferSize, activeApi, activeDevice, audioIn);
        if (!ok)
        {
            if (!_open(clientName, oldBufferSize, activeApi, activeDevice, oldAudioIn))
                return false;
        }

        if (sampleRate != oldSampleRate && sampleRateChangedCallback != nullptr)
            sampleRateChangedCallback(jackProcessArg, sampleRate);

        if (bufferSize != oldBufferSize && bufferSizeCallback != nullptr)
            bufferSizeCallback(bufferSize, jackBufferSizeArg);

        if (!activate())
            return false;

        return ok;
    }

    bool _open(const char* const clientName,
               uint32_t nextBufferSize,
               const RtAudio::Api api,
               const std::string& wantedDevice,
               const bool audioIn = false)
    {
        RtAudio* rtaudio = nullptr;
        try {
            rtaudio = new RtAudio(api);
        } catch (const RtAudioError& err) {
            lastError = err.what();
            d_stderr2("Failed to initialize Audio, reason: %s", err.what());
            return false;
        }

        const uint devCount = rtaudio->getDeviceCount();
        if (devCount == 0)
        {
            lastError = "No audio devices found for " + apiToDisplayName(rtaudio->getCurrentApi());
            d_stderr2("%s", lastError.c_str());
            delete rtaudio;
            return false;
        }

        std::vector<uint> candidates;
        std::vector<std::string> candidateNames;

        if (!wantedDevice.empty())
        {
#if defined(DISTRHO_OS_WINDOWS)
            if (rtaudio->getCurrentApi() == RtAudio::WINDOWS_ASIO)
            {
                AsioDrivers asio;
                const long n = asio.asioGetNumDev();
                for (long i = 0; i < n && static_cast<uint>(i) < devCount; ++i)
                {
                    char drvName[64] = {};
                    if (asio.asioGetDriverName(static_cast<int>(i), drvName, sizeof(drvName)) == ASE_OK)
                    {
                        if (wantedDevice == drvName || std::strncmp(wantedDevice.c_str(), drvName, 31) == 0)
                        {
                            candidates.push_back(static_cast<uint>(i));
                            candidateNames.push_back(drvName);
                            break;
                        }
                    }
                }
            }
#endif
            if (candidates.empty())
            {
                for (uint i = 0; i < devCount; ++i)
                {
                    try {
                        const RtAudio::DeviceInfo info = rtaudio->getDeviceInfo(i);
                        if (info.name == wantedDevice || std::strncmp(wantedDevice.c_str(), info.name.c_str(), 31) == 0)
                        {
                            candidates.push_back(i);
                            candidateNames.push_back(wantedDevice);
                            break;
                        }
                    } catch (...) {}
                }
            }
            if (candidates.empty())
            {
                lastError = "Audio device not found: " + wantedDevice;
                delete rtaudio;
                return false;
            }
        }
        else
        {
            const uint defOut = rtaudio->getDefaultOutputDevice();
            if (defOut < devCount)
                candidates.push_back(defOut);
            for (uint i = 0; i < devCount; ++i)
            {
                if (std::find(candidates.begin(), candidates.end(), i) == candidates.end())
                    candidates.push_back(i);
            }
#if defined(DISTRHO_OS_WINDOWS)
            AsioDrivers asio;
            for (const uint idx : candidates)
            {
                if (rtaudio->getCurrentApi() == RtAudio::WINDOWS_ASIO)
                {
                    char drvName[64] = {};
                    if (asio.asioGetDriverName(static_cast<int>(idx), drvName, sizeof(drvName)) == ASE_OK)
                        candidateNames.push_back(drvName);
                    else
                        candidateNames.push_back(std::string());
                }
                else
                {
                    candidateNames.push_back(std::string());
                }
            }
#else
            candidateNames.resize(candidates.size());
#endif
        }

        if (nextBufferSize == 0)
            nextBufferSize = 512;

        bool streamOpened = false;
        std::string resolvedDeviceName;
        uint openedInChannels = 0;
        uint openedOutChannels = 0;
        uint32_t chosenBuffer = nextBufferSize;

        for (size_t cIdx = 0; cIdx < candidates.size(); ++cIdx)
        {
            if (rtaudio == nullptr)
            {
                try {
                    rtaudio = new RtAudio(api);
                } catch (const RtAudioError& err) {
                    lastError = err.what();
                    return false;
                }
            }

            const uint outDevId = candidates[cIdx];

            RtAudio::StreamParameters inParams;
            RtAudio::StreamParameters* inParamsPtr = nullptr;

            if (audioIn)
            {
                inParams.deviceId = (rtaudio->getCurrentApi() == RtAudio::WINDOWS_ASIO)
                    ? outDevId
                    : rtaudio->getDefaultInputDevice();
                inParams.nChannels = DISTRHO_PLUGIN_NUM_INPUTS;
                if (inParams.nChannels != 0)
                    inParamsPtr = &inParams;
            }

            RtAudio::StreamParameters outParams;
            RtAudio::StreamParameters* outParamsPtr = nullptr;
            outParams.deviceId = outDevId;
            outParams.nChannels = DISTRHO_PLUGIN_NUM_OUTPUTS;
            if (outParams.nChannels != 0)
                outParamsPtr = &outParams;

            if (inParamsPtr == nullptr && outParamsPtr == nullptr)
            {
                outParams.nChannels = 2;
                outParamsPtr = &outParams;
            }

            uint targetSampleRate = 48000;
            try {
                const RtAudio::DeviceInfo devInfo = rtaudio->getDeviceInfo(outParams.deviceId);
                if (!devInfo.sampleRates.empty())
                {
                    const auto& rates = devInfo.sampleRates;
                    if (std::find(rates.begin(), rates.end(), 48000u) != rates.end())
                        targetSampleRate = 48000;
                    else if (devInfo.preferredSampleRate != 0
                             && std::find(rates.begin(), rates.end(), devInfo.preferredSampleRate) != rates.end())
                        targetSampleRate = devInfo.preferredSampleRate;
                    else if (std::find(rates.begin(), rates.end(), 44100u) != rates.end())
                        targetSampleRate = 44100;
                    else
                        targetSampleRate = rates.front();
                }
                if (outParams.nChannels > devInfo.outputChannels && devInfo.outputChannels > 0)
                    outParams.nChannels = devInfo.outputChannels;
            } catch (...) {}

            RtAudio::StreamOptions opts;
            opts.flags = RTAUDIO_NONINTERLEAVED | RTAUDIO_MINIMIZE_LATENCY | RTAUDIO_ALSA_USE_DEFAULT;
            opts.streamName = clientName;

            chosenBuffer = nextBufferSize;
            try {
                rtaudio->openStream(outParamsPtr, inParamsPtr, RTAUDIO_FLOAT32, targetSampleRate, &chosenBuffer, RtAudioCallback, this, &opts);
                streamOpened = true;
                resolvedDeviceName = candidateNames[cIdx];
                openedInChannels = inParamsPtr != nullptr ? inParams.nChannels : 0;
                openedOutChannels = outParamsPtr != nullptr ? outParams.nChannels : 0;
                break;
            } catch (const RtAudioError& err) {
                lastError = err.what();
                d_stderr2("Failed to open Audio stream on device %u, reason: %s", outDevId, err.what());
                delete rtaudio;
                rtaudio = nullptr;
            }
        }

        if (!streamOpened || rtaudio == nullptr)
            return false;

        if (chosenBuffer == 0)
            chosenBuffer = nextBufferSize;

        handle = rtaudio;
        currentApi = rtaudio->getCurrentApi();
        currentDevice = resolvedDeviceName;
        captureEnabled = (openedInChannels != 0);
        streamInChannels = openedInChannels;
        streamOutChannels = openedOutChannels;
        bufferSize = chosenBuffer;
        sampleRate = rtaudio->getStreamSampleRate();
        lastError.clear();

        allocBuffers(true, !midiUsed);
        return true;
    }

    static int RtAudioCallback(void* const outputBuffer,
                               void* const inputBuffer,
                               const uint numFrames,
                               const double streamTime,
                               const RtAudioStreamStatus status,
                               void* const userData)
    {
        RtAudioBridge* const self = static_cast<RtAudioBridge*>(userData);

        if (outputBuffer == nullptr && inputBuffer == nullptr)
            return 0;

        if (self->jackProcessCallback != nullptr)
        {
#if DISTRHO_PLUGIN_NUM_INPUTS > 0
            if (float* const ins = static_cast<float*>(inputBuffer))
            {
                for (uint i = 0; i < self->streamInChannels && i < DISTRHO_PLUGIN_NUM_INPUTS; ++i)
                    self->audioBuffers[i] = ins + (i * numFrames);
            }
#endif

#if DISTRHO_PLUGIN_NUM_OUTPUTS > 0
            if (float* const outs = static_cast<float*>(outputBuffer))
            {
                for (uint i = 0; i < self->streamOutChannels && i < DISTRHO_PLUGIN_NUM_OUTPUTS; ++i)
                    self->audioBuffers[DISTRHO_PLUGIN_NUM_INPUTS + i] = outs + (i * numFrames);
            }
#endif

            self->jackProcessCallback(numFrames, self->jackProcessArg);
        }
        else if (outputBuffer != nullptr)
        {
            std::memset(outputBuffer, 0, sizeof(float) * numFrames * self->streamOutChannels);
        }

        return 0;
        (void)streamTime;
        (void)status;
    }

#if DISTRHO_RTAUDIO_HAVE_MIDI && DISTRHO_PLUGIN_WANT_MIDI_INPUT
    static void RtMidiCallback(double timeStamp, std::vector<uchar>* message, void* userData)
    {
        const size_t len = message->size();
        DISTRHO_SAFE_ASSERT_RETURN(len > 0 && len <= kMaxMIDIInputMessageSize,);

        RtAudioBridge* const self = static_cast<RtAudioBridge*>(userData);

        const RecursiveMutexLocker rml(self->midiInLock);

        self->midiInBufferPending.writeByte(static_cast<uint8_t>(len));
        self->midiInBufferPending.writeCustomData(message->data(), len);
        for (uint8_t i = static_cast<uint8_t>(len); i < kMaxMIDIInputMessageSize; ++i)
            self->midiInBufferPending.writeByte(0);
        self->midiInBufferPending.commitWrite("RtMidiCallback");

        (void)timeStamp;
    }
#endif
};

#endif // RTAUDIO_BRIDGE_HPP_INCLUDED
