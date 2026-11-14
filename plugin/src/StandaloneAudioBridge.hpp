#ifndef RTAUDIO_BRIDGE_HPP_INCLUDED
#define RTAUDIO_BRIDGE_HPP_INCLUDED

#include "src/jackbridge/NativeBridge.hpp"
#include "StandaloneAudio.hpp"

#if defined(DISTRHO_OS_WINDOWS)
# define __WINDOWS_WASAPI__
# define __WINDOWS_ASIO__
# define __WINDOWS_DS__
# define __WINDOWS_MM__
#elif defined(DISTRHO_OS_MAC)
# define __MACOSX_CORE__
#elif defined(DISTRHO_OS_LINUX)
# if defined(HAVE_ALSA) && defined(HAVE_PULSEAUDIO)
#  define __LINUX_PULSE__
#  define __LINUX_ALSA__
# elif defined(HAVE_ALSA)
#  define __LINUX_ALSA__
# elif defined(HAVE_PULSEAUDIO)
#  define __LINUX_PULSE__
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
# endif
# if DISTRHO_PLUGIN_WANT_MIDI_OUTPUT
    RtMidiOut* midiOut = nullptr;
# endif
#endif

    RtAudioBridge()
    {
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
        if (api != RtAudio::WINDOWS_ASIO)
            names.push_back("Default");

        // Important: RtApiAsio's constructor calls drivers.removeCurrentDriver() on
        // a file-static AsioDrivers singleton in RtAudio.cpp. Never construct a second
        // RtAudio(WINDOWS_ASIO) while an ASIO handle is already open.
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
        pe::setStandaloneAudioBackend(std::move(backend));
    }

    bool open(const char* const clientName) override
    {
        const pe::StandaloneAudioConfig saved = pe::loadStandaloneAudioConfig();

        uint32_t initialBuffer = saved.bufferSize;
        if (initialBuffer == 0)
        {
#ifdef __WINDOWS_WASAPI__
            initialBuffer = 0;
#else
            initialBuffer = 512;
#endif
        }

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
        RtMidiIn* midiIn;
        try {
            midiIn = new RtMidiIn(RtMidi::UNSPECIFIED, clientName);
        } catch (const RtMidiError& err) {
            d_stderr2("Failed to initialize MIDI in, reason: %s", err.what());
            return 0;
        }

        const uint portCount = std::min(kMaxMIDIInputPorts, midiIn->getPortCount());

        if (portCount == 0)
        {
            delete midiIn;
            return 0;
        }

        std::string portName = midiIn->getPortName(0);
        d_stdout("Connecting to MIDI in port '%s'", portName.c_str());

        midiIn->openPort(0, "events-in");
        midiIn->ignoreTypes(true, true, true);
        midiIn->setCallback(RtMidiCallback, this);
        midiInPorts[0].handle = midiIn;

        for (uint i = 1; i < portCount; ++i)
        {
            midiIn = new RtMidiIn(RtMidi::UNSPECIFIED, clientName);
            portName = midiIn->getPortName(i);
            d_stdout("Connecting to MIDI in port '%s'", portName.c_str());

            midiIn->openPort(i, "events-in");
            midiIn->ignoreTypes(true, true, true);
            midiIn->setCallback(RtMidiCallback, this);
            midiInPorts[i].handle = midiIn;
        }

        return (numMIDIInPorts = portCount);
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
#endif
#if DISTRHO_RTAUDIO_HAVE_MIDI && DISTRHO_PLUGIN_WANT_MIDI_OUTPUT
        if (midiOut != nullptr)
        {
            midiOut->closePort();
            delete midiOut;
            midiOut = nullptr;
        }
#endif

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
        return numAudioIns != 0;
    }

    bool supportsBufferSizeChanges() const override
    {
        if (handle == nullptr)
            return false;

        switch (handle->getCurrentApi())
        {
        // WASAPI queues buffer fragments inside RtAudio rather than resizing the hardware period.
        case RtAudio::WINDOWS_WASAPI:
            return false;
        default:
            return true;
        }
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
        const bool ok = reinit(numAudioIns != 0, newBufferSize);
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
        const bool audioIn = numAudioIns != 0;

        char clientName[32];
        std::snprintf(clientName, sizeof(clientName), "%s", DISTRHO_PLUGIN_NAME);
        clientName[sizeof(clientName) - 1] = '\0';

        if (handle != nullptr)
            closeAudioStreamOnly();

        uint32_t requestedBuffer = prevBufferSize;
        if (targetApi == RtAudio::WINDOWS_WASAPI)
            requestedBuffer = 0;
        else if (requestedBuffer == 0)
            requestedBuffer = 512;

        if (!_open(clientName, requestedBuffer, targetApi, targetDevice, audioIn) || !activate())
        {
            const std::string failedError = lastError;
            if (handle != nullptr)
                closeAudioStreamOnly();

            // Restore previous working engine and device
            if (prevApi != RtAudio::UNSPECIFIED)
            {
                const uint32_t restoreBuf = (prevApi == RtAudio::WINDOWS_WASAPI) ? 0 : prevBufferSize;
                if (_open(clientName, restoreBuf, prevApi, prevDevice, audioIn))
                    activate();
            }
            lastError = failedError;
            return false;
        }

        if (sampleRate != prevSampleRate && sampleRateChangedCallback != nullptr)
            sampleRateChangedCallback(jackProcessArg, sampleRate);

        if (bufferSize != prevBufferSize && bufferSizeCallback != nullptr)
            bufferSizeCallback(bufferSize, jackBufferSizeArg);

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

        closeAudioStreamOnly();

        char clientName[32];
        std::snprintf(clientName, sizeof(clientName), "%s", DISTRHO_PLUGIN_NAME);
        clientName[sizeof(clientName) - 1] = '\0';

        if (!_open(clientName, newBufferSize, activeApi, activeDevice, audioIn))
            return false;

        if (sampleRate != oldSampleRate && sampleRateChangedCallback != nullptr)
            sampleRateChangedCallback(jackProcessArg, sampleRate);

        if (bufferSize != oldBufferSize && bufferSizeCallback != nullptr)
            bufferSizeCallback(bufferSize, jackBufferSizeArg);

        return activate();
    }

    bool _open(const char* const clientName,
               uint32_t nextBufferSize,
               const RtAudio::Api api,
               const std::string& wantedDevice,
               const bool audioIn = DISTRHO_PLUGIN_NUM_INPUTS != 0)
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

        int selectedOutDev = -1;
        std::string resolvedDeviceName;

        if (!wantedDevice.empty())
        {
            for (uint i = 0; i < devCount; ++i)
            {
                try {
                    const RtAudio::DeviceInfo info = rtaudio->getDeviceInfo(i);
                    if (info.name == wantedDevice)
                    {
                        selectedOutDev = static_cast<int>(i);
                        resolvedDeviceName = info.name;
                        break;
                    }
                } catch (...) {}
            }
            if (selectedOutDev < 0)
            {
                lastError = "Audio device not found: " + wantedDevice;
                delete rtaudio;
                return false;
            }
        }
        else
        {
            selectedOutDev = static_cast<int>(rtaudio->getDefaultOutputDevice());
            if (selectedOutDev >= 0 && static_cast<uint>(selectedOutDev) < devCount)
            {
                try {
                    const RtAudio::DeviceInfo info = rtaudio->getDeviceInfo(static_cast<uint>(selectedOutDev));
                    if (rtaudio->getCurrentApi() == RtAudio::WINDOWS_ASIO)
                        resolvedDeviceName = info.name;
                } catch (...) {}
            }
        }

        RtAudio::StreamParameters inParams;
        RtAudio::StreamParameters* inParamsPtr = nullptr;

        if (audioIn)
        {
            inParams.deviceId = rtaudio->getDefaultInputDevice();
            inParams.nChannels = DISTRHO_PLUGIN_NUM_INPUTS;
            if (inParams.nChannels != 0)
                inParamsPtr = &inParams;
        }

        RtAudio::StreamParameters outParams;
        RtAudio::StreamParameters* outParamsPtr = nullptr;
        outParams.deviceId = static_cast<uint>(selectedOutDev >= 0 ? selectedOutDev : 0);
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

        try {
            rtaudio->openStream(outParamsPtr, inParamsPtr, RTAUDIO_FLOAT32, targetSampleRate, &nextBufferSize, RtAudioCallback, this, &opts);
        } catch (const RtAudioError& err) {
            lastError = err.what();
            d_stderr2("Failed to open Audio stream, reason: %s", err.what());
            delete rtaudio;
            return false;
        }

        handle = rtaudio;
        currentApi = rtaudio->getCurrentApi();
        currentDevice = resolvedDeviceName;
        numAudioIns = inParamsPtr != nullptr ? inParams.nChannels : 0;
        numAudioOuts = outParamsPtr != nullptr ? outParams.nChannels : 0;
        bufferSize = nextBufferSize;
        sampleRate = rtaudio->getStreamSampleRate();
        lastError.clear();

        allocBuffers(inParamsPtr != nullptr, outParamsPtr != nullptr);
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
            ulong i = 0;

            if (float* const ins = static_cast<float*>(inputBuffer))
            {
                for (; i < self->numAudioIns; ++i)
                    self->audioBuffers[i] = ins + (i * numFrames);
            }

            if (float* const outs = static_cast<float*>(outputBuffer))
            {
                for (ulong j = 0; i < self->numAudioIns + self->numAudioOuts; ++i, ++j)
                    self->audioBuffers[i] = outs + (j * numFrames);
            }

            self->jackProcessCallback(numFrames, self->jackProcessArg);
        }
        else if (outputBuffer != nullptr)
        {
            std::memset(outputBuffer, 0, sizeof(float) * numFrames * self->numAudioOuts);
        }

        return 0;
        (void)streamTime;
        (void)status;
    }

#if DISTRHO_RTAUDIO_HAVE_MIDI && DISTRHO_PLUGIN_WANT_MIDI_INPUT
    static void RtMidiCallback(double timeStamp, std::vector<uchar>* message, void* userData)
    {
        const size_t numBytes = message->size();
        DISTRHO_SAFE_ASSERT_RETURN(numBytes > 0,);

        if (numBytes > kMaxMIDIInputMessageSize)
            return;

        RtAudioBridge* const self = static_cast<RtAudioBridge*>(userData);

        const uint8_t* const bytes = message->data();

        self->midiInBufferPending.writeByte(static_cast<uint8_t>(numBytes));
        for (size_t i = 0; i < numBytes; ++i)
            self->midiInBufferPending.writeByte(bytes[i]);
        self->midiInBufferPending.commitWrite();

        (void)timeStamp;
    }
#endif
};

#endif // RTAUDIO_BRIDGE_HPP_INCLUDED
