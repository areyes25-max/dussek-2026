/*
 * Standalone entry point for Dussek & Weidinger.
 *
 * Overrides DPF's default RtAudioBridge.hpp with StandaloneAudioBridge.hpp
 * before compiling DistrhoPluginJACK.cpp so that Windows standalone builds
 * include WASAPI, ASIO, and DirectSound support and allow runtime selection
 * of both the audio engine and audio device without modifying the dpf submodule.
 */

#include "src/DistrhoPlugin.cpp"

#include "src/DistrhoPluginInternal.hpp"
#include "DistrhoPluginUtils.hpp"

#if DISTRHO_PLUGIN_HAS_UI
# include "src/DistrhoUIInternal.hpp"
#endif
#if DISTRHO_PLUGIN_WANT_MIDI_INPUT
# include "extra/RingBuffer.hpp"
#endif

#include "src/jackbridge/JackBridge.hpp"
#include "DistrhoStandaloneUtils.hpp"
#ifdef HAVE_JACK
# include "extra/LibraryUtils.hpp"
#endif
#include "src/jackbridge/NativeBridge.hpp"

#include "StandaloneAudioBridge.hpp"

#include "src/DistrhoPluginJACK.cpp"

namespace {

struct PluginJackSampleRateAccess : DISTRHO_NAMESPACE::PluginJack {
    static void notifySampleRate(void* const arg, const jack_nframes_t sampleRate)
    {
        if (arg == nullptr)
            return;
        auto* const self = static_cast<DISTRHO_NAMESPACE::PluginJack*>(arg);
        (self->*&PluginJackSampleRateAccess::jackSampleRate)(sampleRate);
    }
};

} // namespace

void notifyStandaloneSampleRateChanged(void* const arg, const jack_nframes_t sampleRate)
{
    PluginJackSampleRateAccess::notifySampleRate(arg, sampleRate);
}

#define DISTRHO_IS_STANDALONE 1
#include "src/DistrhoUtils.cpp"
