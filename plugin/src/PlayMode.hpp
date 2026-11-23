// NA_Magicos: the play bar's note mapping (pure; tested in ui_tests). Each key played becomes the notes it
// sounds: transposed, and in Chords mode its chord in the key (Smart Chords: the scale's triad on it, a major
// triad off the scale), in Melody mode with its harmony a third below in the key. What a key started is kept
// until it is released, so changing the mode or the key while holding never leaves a note hanging; a note two
// keys share sounds until both are up.
#pragma once
#include <array>
#include <cstdint>

namespace pe {

class PlayMapper {
public:
    static constexpr int kMax = 3;
    struct Notes { int count = 0; uint8_t note[kMax] {}; uint8_t velocity[kMax] {}; };

    /// a key down: the notes to start (those already sounding from another key are restarted)
    Notes on(uint8_t key, uint8_t velocity, int transpose, int keyRoot, int mode)
    {
        Notes n;
        const int root = key + transpose;
        auto add = [&](int note, int vel) {
            if (note < 0 || note > 127 || n.count >= kMax) return;
            for (int i = 0; i < n.count; ++i) if (n.note[i] == note) return;
            n.note[n.count] = (uint8_t)note;
            n.velocity[n.count++] = (uint8_t)(vel < 1 ? 1 : vel > 127 ? 127 : vel);
        };
        add(root, velocity);
        if (root >= 0 && root <= 127 && mode != 0) {
            static const int scale[7] = { 0, 2, 4, 5, 7, 9, 11 };
            const int pc = ((root - keyRoot) % 12 + 12) % 12;
            int degree = -1;
            for (int d = 0; d < 7; ++d) if (scale[d] == pc) degree = d;
            auto up = [&](int steps) { // the scale's note `steps` degrees above the played one, in semitones
                const int to = degree + steps;
                return scale[to % 7] + 12 * (to / 7) - scale[degree];
            };
            if (mode == 1) { // Chords
                const int third = degree >= 0 ? up(2) : 4, fifth = degree >= 0 ? up(4) : 7;
                add(root + third, velocity * 85 / 100);
                add(root + fifth, velocity * 85 / 100);
            } else if (mode == 2 && degree >= 0) { // Melody: a third below, in the key
                add(root - (12 - up(5)), velocity * 75 / 100);
            }
        }
        held[key] = n;
        for (int i = 0; i < n.count; ++i) ++count[n.note[i]];
        return n;
    }

    /// a key up: the notes to stop now (the ones no other held key still sounds)
    Notes off(uint8_t key)
    {
        Notes out;
        Notes& h = held[key];
        for (int i = 0; i < h.count; ++i) {
            const uint8_t note = h.note[i];
            if (count[note] > 0 && --count[note] == 0)
                out.note[out.count++] = note;
        }
        h = Notes {};
        return out;
    }

    void reset() { held.fill(Notes {}); count.fill(0); }

private:
    std::array<Notes, 128> held {};
    std::array<uint8_t, 128> count {};
};

} // namespace pe
