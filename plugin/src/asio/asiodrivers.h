#pragma once

#include "asiosys.h"
#include "asio.h"

#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32) || defined(_WIN64)
#include <objbase.h>

interface IASIO : public IUnknown {
    virtual ASIOBool init(void* sysHandle) = 0;
    virtual void getDriverName(char* name) = 0;
    virtual long getDriverVersion() = 0;
    virtual void getErrorMessage(char* string) = 0;
    virtual ASIOError start() = 0;
    virtual ASIOError stop() = 0;
    virtual ASIOError getChannels(long* numInputChannels, long* numOutputChannels) = 0;
    virtual ASIOError getLatencies(long* inputLatency, long* outputLatency) = 0;
    virtual ASIOError getBufferSize(long* minSize, long* maxSize, long* preferredSize, long* granularity) = 0;
    virtual ASIOError canSampleRate(ASIOSampleRate sampleRate) = 0;
    virtual ASIOError getSampleRate(ASIOSampleRate* sampleRate) = 0;
    virtual ASIOError setSampleRate(ASIOSampleRate sampleRate) = 0;
    virtual ASIOError getClockSources(ASIOClockSource* clocks, long* numSources) = 0;
    virtual ASIOError setClockSource(long reference) = 0;
    virtual ASIOError getSamplePosition(ASIOSamples* sPos, ASIOTimeStamp* tStamp) = 0;
    virtual ASIOError getChannelInfo(ASIOChannelInfo* info) = 0;
    virtual ASIOError createBuffers(ASIOBufferInfo* bufferInfos, long numChannels, long bufferSize, ASIOCallbacks* callbacks) = 0;
    virtual ASIOError disposeBuffers() = 0;
    virtual ASIOError controlPanel() = 0;
    virtual ASIOError future(long selector, void* opt) = 0;
    virtual ASIOError outputReady() = 0;
};
#endif

class AsioDrivers {
public:
    struct DriverEntry {
        std::string name;
        std::string clsidStr;
#if defined(_WIN32) || defined(_WIN64)
        CLSID clsid{};
#endif
    };

    AsioDrivers() {
        refresh();
    }

    ~AsioDrivers() = default;

    void refresh() {
        entries_.clear();
#if defined(_WIN32) || defined(_WIN64)
        HKEY asioKey = nullptr;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\ASIO", 0, KEY_READ, &asioKey) != ERROR_SUCCESS)
            return;

        DWORD index = 0;
        char subKeyName[256] = {};
        while (RegEnumKeyA(asioKey, index++, subKeyName, sizeof(subKeyName)) == ERROR_SUCCESS) {
            HKEY drvKey = nullptr;
            if (RegOpenKeyExA(asioKey, subKeyName, 0, KEY_READ, &drvKey) == ERROR_SUCCESS) {
                char clsidBuf[128] = {};
                DWORD clsidSize = sizeof(clsidBuf);
                DWORD type = 0;
                if (RegQueryValueExA(drvKey, "CLSID", nullptr, &type, reinterpret_cast<LPBYTE>(clsidBuf), &clsidSize) == ERROR_SUCCESS) {
                    char descBuf[256] = {};
                    DWORD descSize = sizeof(descBuf);
                    std::string driverName = subKeyName;
                    if (RegQueryValueExA(drvKey, "Description", nullptr, &type, reinterpret_cast<LPBYTE>(descBuf), &descSize) == ERROR_SUCCESS
                        && descBuf[0] != '\0') {
                        driverName = descBuf;
                    }

                    wchar_t wClsid[128] = {};
                    MultiByteToWideChar(CP_ACP, 0, clsidBuf, -1, wClsid, 128);
                    CLSID clsid{};
                    if (SUCCEEDED(CLSIDFromString(wClsid, &clsid))) {
                        DriverEntry entry;
                        entry.name = driverName;
                        entry.clsidStr = clsidBuf;
                        entry.clsid = clsid;
                        entries_.push_back(std::move(entry));
                    }
                }
                RegCloseKey(drvKey);
            }
        }
        RegCloseKey(asioKey);
#endif
    }

    long asioGetNumDev() {
        refresh();
        return static_cast<long>(entries_.size());
    }

    ASIOError asioGetDriverName(int index, char* name, int nameSize) {
        if (!name || nameSize <= 0)
            return ASE_InvalidParameter;
        if (index < 0 || static_cast<size_t>(index) >= entries_.size())
            refresh();
        if (index < 0 || static_cast<size_t>(index) >= entries_.size()) {
            name[0] = '\0';
            return ASE_NotPresent;
        }
        std::strncpy(name, entries_[static_cast<size_t>(index)].name.c_str(), static_cast<size_t>(nameSize - 1));
        name[nameSize - 1] = '\0';
        return ASE_OK;
    }

    bool loadDriver(char* name) {
        if (!name || name[0] == '\0')
            return false;
        removeCurrentDriver();
        if (entries_.empty())
            refresh();
#if defined(_WIN32) || defined(_WIN64)
        for (const auto& entry : entries_) {
            // RtAudio truncates driver names to 31 characters in getDeviceInfo().
            if (entry.name == name || std::strncmp(entry.name.c_str(), name, 31) == 0) {
                CoInitialize(nullptr);
                IASIO* drv = nullptr;
                HRESULT hr = CoCreateInstance(entry.clsid, nullptr, CLSCTX_INPROC_SERVER, entry.clsid, reinterpret_cast<void**>(&drv));
                if (SUCCEEDED(hr) && drv != nullptr) {
                    currentDriver() = drv;
                    return true;
                }
            }
        }
#endif
        return false;
    }

    void removeCurrentDriver() {
#if defined(_WIN32) || defined(_WIN64)
        if (currentDriver() != nullptr) {
            currentDriver()->Release();
            currentDriver() = nullptr;
        }
#endif
    }

#if defined(_WIN32) || defined(_WIN64)
    static IASIO*& currentDriver() {
        static IASIO* drv = nullptr;
        return drv;
    }
#endif

private:
    std::vector<DriverEntry> entries_;
};

#if defined(_WIN32) || defined(_WIN64)

inline ASIOError ASIOInit(ASIODriverInfo* info) {
    IASIO* drv = AsioDrivers::currentDriver();
    if (!drv)
        return ASE_NotPresent;
    if (!info)
        return ASE_InvalidParameter;

    info->driverVersion = 0;
    info->name[0] = '\0';
    info->errorMessage[0] = '\0';
    void* sysRef = info->sysRef ? info->sysRef : GetDesktopWindow();
    if (!drv->init(sysRef)) {
        drv->getErrorMessage(info->errorMessage);
        drv->Release();
        AsioDrivers::currentDriver() = nullptr;
        return ASE_NotPresent;
    }
    info->driverVersion = drv->getDriverVersion();
    drv->getDriverName(info->name);
    return ASE_OK;
}

inline ASIOError ASIOExit(void) {
    IASIO* drv = AsioDrivers::currentDriver();
    if (drv) {
        drv->Release();
        AsioDrivers::currentDriver() = nullptr;
    }
    return ASE_OK;
}

inline ASIOError ASIOStart(void) {
    IASIO* drv = AsioDrivers::currentDriver();
    return drv ? drv->start() : ASE_NotPresent;
}

inline ASIOError ASIOStop(void) {
    IASIO* drv = AsioDrivers::currentDriver();
    return drv ? drv->stop() : ASE_NotPresent;
}

inline ASIOError ASIOGetChannels(long* numInputChannels, long* numOutputChannels) {
    IASIO* drv = AsioDrivers::currentDriver();
    return drv ? drv->getChannels(numInputChannels, numOutputChannels) : ASE_NotPresent;
}

inline ASIOError ASIOGetLatencies(long* inputLatency, long* outputLatency) {
    IASIO* drv = AsioDrivers::currentDriver();
    return drv ? drv->getLatencies(inputLatency, outputLatency) : ASE_NotPresent;
}

inline ASIOError ASIOGetBufferSize(long* minSize, long* maxSize, long* preferredSize, long* granularity) {
    IASIO* drv = AsioDrivers::currentDriver();
    return drv ? drv->getBufferSize(minSize, maxSize, preferredSize, granularity) : ASE_NotPresent;
}

inline ASIOError ASIOCanSampleRate(ASIOSampleRate sampleRate) {
    IASIO* drv = AsioDrivers::currentDriver();
    return drv ? drv->canSampleRate(sampleRate) : ASE_NotPresent;
}

inline ASIOError ASIOGetSampleRate(ASIOSampleRate* currentRate) {
    IASIO* drv = AsioDrivers::currentDriver();
    return drv ? drv->getSampleRate(currentRate) : ASE_NotPresent;
}

inline ASIOError ASIOSetSampleRate(ASIOSampleRate sampleRate) {
    IASIO* drv = AsioDrivers::currentDriver();
    return drv ? drv->setSampleRate(sampleRate) : ASE_NotPresent;
}

inline ASIOError ASIOGetClockSources(ASIOClockSource* clocks, long* numSources) {
    IASIO* drv = AsioDrivers::currentDriver();
    return drv ? drv->getClockSources(clocks, numSources) : ASE_NotPresent;
}

inline ASIOError ASIOSetClockSource(long reference) {
    IASIO* drv = AsioDrivers::currentDriver();
    return drv ? drv->setClockSource(reference) : ASE_NotPresent;
}

inline ASIOError ASIOGetSamplePosition(ASIOSamples* sPos, ASIOTimeStamp* tStamp) {
    IASIO* drv = AsioDrivers::currentDriver();
    return drv ? drv->getSamplePosition(sPos, tStamp) : ASE_NotPresent;
}

inline ASIOError ASIOGetChannelInfo(ASIOChannelInfo* info) {
    IASIO* drv = AsioDrivers::currentDriver();
    return drv ? drv->getChannelInfo(info) : ASE_NotPresent;
}

inline ASIOError ASIOCreateBuffers(ASIOBufferInfo* bufferInfos, long numChannels, long bufferSize, ASIOCallbacks* callbacks) {
    IASIO* drv = AsioDrivers::currentDriver();
    return drv ? drv->createBuffers(bufferInfos, numChannels, bufferSize, callbacks) : ASE_NotPresent;
}

inline ASIOError ASIODisposeBuffers(void) {
    IASIO* drv = AsioDrivers::currentDriver();
    return drv ? drv->disposeBuffers() : ASE_NotPresent;
}

inline ASIOError ASIOControlPanel(void) {
    IASIO* drv = AsioDrivers::currentDriver();
    return drv ? drv->controlPanel() : ASE_NotPresent;
}

inline ASIOError ASIOFuture(long selector, void* opt) {
    IASIO* drv = AsioDrivers::currentDriver();
    return drv ? drv->future(selector, opt) : ASE_NotPresent;
}

inline ASIOError ASIOOutputReady(void) {
    IASIO* drv = AsioDrivers::currentDriver();
    return drv ? drv->outputReady() : ASE_NotPresent;
}

#endif
