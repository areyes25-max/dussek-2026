#pragma once

#include <cstdint>

using ASIOSampleRate = double;

using ASIOBool = long;
enum {
    ASIOFalse = 0,
    ASIOTrue = 1
};

using ASIOSampleType = long;
enum {
    ASIOSTInt16MSB   = 0,
    ASIOSTInt24MSB   = 1,
    ASIOSTInt32MSB   = 2,
    ASIOSTFloat32MSB = 3,
    ASIOSTFloat64MSB = 4,
    ASIOSTInt32MSB16 = 8,
    ASIOSTInt32MSB18 = 9,
    ASIOSTInt32MSB20 = 10,
    ASIOSTInt32MSB24 = 11,

    ASIOSTInt16LSB   = 16,
    ASIOSTInt24LSB   = 17,
    ASIOSTInt32LSB   = 18,
    ASIOSTFloat32LSB = 19,
    ASIOSTFloat64LSB = 20,
    ASIOSTInt32LSB16 = 24,
    ASIOSTInt32LSB18 = 25,
    ASIOSTInt32LSB20 = 26,
    ASIOSTInt32LSB24 = 27,

    ASIOSTDSDInt8LSB1 = 32,
    ASIOSTDSDInt8MSB1 = 33,
    ASIOSTDSDInt8NER8 = 40,

    ASIOSTLastEntry
};

using ASIOError = long;
enum {
    ASE_OK               = 0,
    ASE_SUCCESS          = 0x3f4847a0,
    ASE_NotPresent       = -1000,
    ASE_HWMalfunction    = ASE_NotPresent + 1,
    ASE_InvalidParameter = ASE_NotPresent + 2,
    ASE_InvalidMode      = ASE_NotPresent + 3,
    ASE_SPNotAdvancing   = ASE_NotPresent + 4,
    ASE_NoClock          = ASE_NotPresent + 5,
    ASE_NoMemory         = ASE_NotPresent + 6
};

struct ASIO64Bit {
    unsigned long hi;
    unsigned long lo;
};
using ASIOSamples = ASIO64Bit;
using ASIOTimeStamp = ASIO64Bit;

struct ASIODriverInfo {
    long asioVersion;
    long driverVersion;
    char name[32];
    char errorMessage[124];
    void* sysRef;
};

struct ASIOClockSource {
    long index;
    long associatedChannel;
    long associatedGroup;
    ASIOBool isCurrentSource;
    char name[32];
};

struct ASIOChannelInfo {
    long channel;
    ASIOBool isInput;
    ASIOBool isActive;
    long channelGroup;
    ASIOSampleType type;
    char name[32];
};

struct ASIOBufferInfo {
    ASIOBool isInput;
    long channelNum;
    void* buffers[2];
};

struct ASIOTimeCode {
    double speed;
    ASIOSamples timeCodeSamples;
    unsigned long flags;
    char future[64];
};

struct AsioTimeInfo {
    double speed;
    ASIOTimeStamp systemTime;
    ASIOSamples samplePosition;
    ASIOSampleRate sampleRate;
    unsigned long flags;
    char reserved[12];
};

struct ASIOTime {
    long reserved[4];
    AsioTimeInfo timeInfo;
    ASIOTimeCode timeCode;
};

enum {
    kAsioSelectorSupported    = 1,
    kAsioEngineVersion        = 2,
    kAsioResetRequest         = 3,
    kAsioBufferSizeChange     = 4,
    kAsioResyncRequest        = 5,
    kAsioLatenciesChanged     = 6,
    kAsioSupportsTimeInfo     = 7,
    kAsioSupportsTimeCode     = 8,
    kAsioMMCCommand           = 9,
    kAsioSupportsInputMonitor = 10,
    kAsioSupportsInputGain    = 11,
    kAsioSupportsInputMeter   = 12,
    kAsioSupportsOutputGain   = 13,
    kAsioSupportsOutputMeter  = 14,
    kAsioOverload             = 15,
    kAsioNumMessageSelectors
};

struct ASIOCallbacks {
    void (*bufferSwitch)(long doubleBufferIndex, ASIOBool directProcess);
    void (*sampleRateDidChange)(ASIOSampleRate sRate);
    long (*asioMessage)(long selector, long value, void* message, double* opt);
    ASIOTime* (*bufferSwitchTimeInfo)(ASIOTime* params, long doubleBufferIndex, ASIOBool directProcess);
};

ASIOError ASIOInit(ASIODriverInfo* info);
ASIOError ASIOExit(void);
ASIOError ASIOStart(void);
ASIOError ASIOStop(void);
ASIOError ASIOGetChannels(long* numInputChannels, long* numOutputChannels);
ASIOError ASIOGetLatencies(long* inputLatency, long* outputLatency);
ASIOError ASIOGetBufferSize(long* minSize, long* maxSize, long* preferredSize, long* granularity);
ASIOError ASIOCanSampleRate(ASIOSampleRate sampleRate);
ASIOError ASIOGetSampleRate(ASIOSampleRate* currentRate);
ASIOError ASIOSetSampleRate(ASIOSampleRate sampleRate);
ASIOError ASIOGetClockSources(ASIOClockSource* clocks, long* numSources);
ASIOError ASIOSetClockSource(long reference);
ASIOError ASIOGetSamplePosition(ASIOSamples* sPos, ASIOTimeStamp* tStamp);
ASIOError ASIOGetChannelInfo(ASIOChannelInfo* info);
ASIOError ASIOCreateBuffers(ASIOBufferInfo* bufferInfos, long numChannels, long bufferSize, ASIOCallbacks* callbacks);
ASIOError ASIODisposeBuffers(void);
ASIOError ASIOControlPanel(void);
ASIOError ASIOFuture(long selector, void* opt);
ASIOError ASIOOutputReady(void);
