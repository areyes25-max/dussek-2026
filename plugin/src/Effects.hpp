// NA_Magicos: the effects rack on the plugin's output, after the instrument and its room:
// EQ (3 bands) -> Chorus -> Delay -> Reverb. Real DSP, no allocation on the audio thread
// (prepare() sizes every buffer for the sample rate; process() only reads and writes them).
// Each effect costs nothing while it is off (its mix at 0, the EQ flat); the delay's and reverb's
// tails keep ringing out when their mix is lowered, the way a send does.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace pe::fx {

struct Settings {
    float reverb = 0, reverbSize = 60;           // %, %
    float delay = 0, delayMs = 375, feedback = 35; // %, ms, %
    float chorus = 0, chorusHz = 0.8f, chorusDepth = 40; // %, Hz, %
    float low = 0, mid = 0, high = 0;             // dB
};

/// RBJ biquad (transposed direct form II)
struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    float run(float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void set(int type, double sr, double f, double q, double db) // 0 low shelf, 1 peak, 2 high shelf
    {
        const double A = std::pow(10.0, db / 40.0), w = 2 * M_PI * f / sr, c = std::cos(w), s = std::sin(w);
        double B0, B1, B2, A0, A1, A2;
        if (type == 1) {
            const double al = s / (2 * q);
            B0 = 1 + al * A; B1 = -2 * c; B2 = 1 - al * A; A0 = 1 + al / A; A1 = -2 * c; A2 = 1 - al / A;
        } else {
            const double al = s / 2 * std::sqrt(2.0), sq = 2 * std::sqrt(A) * al, sg = type == 0 ? 1 : -1;
            B0 = A * ((A + 1) - sg * (A - 1) * c + sq);
            B1 = sg * 2 * A * ((A - 1) - sg * (A + 1) * c);
            B2 = A * ((A + 1) - sg * (A - 1) * c - sq);
            A0 = (A + 1) + sg * (A - 1) * c + sq;
            A1 = -sg * 2 * ((A - 1) + sg * (A + 1) * c);
            A2 = (A + 1) + sg * (A - 1) * c - sq;
        }
        b0 = (float)(B0 / A0); b1 = (float)(B1 / A0); b2 = (float)(B2 / A0); a1 = (float)(A1 / A0); a2 = (float)(A2 / A0);
    }
};

/// Freeverb's comb (damped feedback) and all-pass
struct Comb {
    std::vector<float> buf; size_t i = 0; float store = 0;
    float run(float x, float fb, float damp)
    {
        const float y = buf[i];
        store = y * (1 - damp) + store * damp;
        buf[i] = x + store * fb;
        if (++i >= buf.size()) i = 0;
        return y;
    }
};
struct Allpass {
    std::vector<float> buf; size_t i = 0;
    float run(float x)
    {
        const float b = buf[i], y = -x + b;
        buf[i] = x + b * 0.5f;
        if (++i >= buf.size()) i = 0;
        return y;
    }
};

class Rack {
public:
    void prepare(double sampleRate)
    {
        sr = sampleRate > 0 ? sampleRate : 48000.0;
        const double k = sr / 44100.0;
        static const int combs[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
        static const int aps[4] = { 556, 441, 341, 225 };
        for (int c = 0; c < 2; ++c) {
            for (int j = 0; j < 8; ++j) { comb[c][j].buf.assign((size_t)((combs[j] + 23 * c) * k) + 1, 0.0f); comb[c][j].i = 0; comb[c][j].store = 0; }
            for (int j = 0; j < 4; ++j) { ap[c][j].buf.assign((size_t)((aps[j] + 23 * c) * k) + 1, 0.0f); ap[c][j].i = 0; }
            dl[c].assign((size_t)(sr * 2.0) + 4, 0.0f);
            ch[c].assign((size_t)(sr * 0.05) + 4, 0.0f);
            dlLp[c] = 0;
        }
        preDelay.assign((size_t)(sr * 0.02) + 2, 0.0f);
        dlPos = chPos = pdPos = 0;
        phase = 0;
        lastLow = lastMid = lastHigh = 1e9f; // the EQ's filters are set on the first block
        smDelay = -1;
    }

    /// in place, on the audio thread
    void process(float* left, float* right, uint32_t n, const Settings& s)
    {
        if (dl[0].empty()) return;
        float* io[2] = { left, right };
        // EQ
        const bool eq = std::fabs(s.low) > 0.05f || std::fabs(s.mid) > 0.05f || std::fabs(s.high) > 0.05f;
        if (s.low != lastLow || s.mid != lastMid || s.high != lastHigh) {
            for (int c = 0; c < 2; ++c) {
                band[c][0].set(0, sr, 120, 0.707, s.low);
                band[c][1].set(1, sr, 1000, 0.8, s.mid);
                band[c][2].set(2, sr, 8000, 0.707, s.high);
            }
            lastLow = s.low; lastMid = s.mid; lastHigh = s.high;
        }
        if (eq)
            for (int c = 0; c < 2; ++c)
                for (uint32_t i = 0; i < n; ++i)
                    io[c][i] = band[c][2].run(band[c][1].run(band[c][0].run(io[c][i])));
        // Chorus: a modulated short delay per side, the LFOs a quarter turn apart
        const float chMix = std::clamp(s.chorus, 0.0f, 100.0f) / 100.0f;
        if (chMix > 0) {
            const size_t L = ch[0].size();
            const double base = 0.012 * sr, depth = 0.006 * sr * std::clamp(s.chorusDepth, 0.0f, 100.0f) / 100.0;
            const double inc = 2 * M_PI * std::clamp(s.chorusHz, 0.05f, 8.0f) / sr;
            for (uint32_t i = 0; i < n; ++i) {
                for (int c = 0; c < 2; ++c) {
                    ch[c][chPos] = io[c][i];
                    const double d = base + depth * 0.5 * (1 + std::sin(phase + c * M_PI / 2));
                    double rp = (double)chPos - d;
                    while (rp < 0) rp += (double)L;
                    const size_t a = (size_t)rp % L, b = (a + 1) % L;
                    const float fr = (float)(rp - std::floor(rp));
                    const float w = ch[c][a] * (1 - fr) + ch[c][b] * fr;
                    io[c][i] = io[c][i] * (1 - 0.5f * chMix) + w * 0.7f * chMix;
                }
                phase += inc;
                if (phase > 2 * M_PI) phase -= 2 * M_PI;
                if (++chPos >= L) chPos = 0;
            }
        }
        // Delay: stereo, its echo crossing sides (ping-pong), darkened a little at each repeat
        const float dMix = std::clamp(s.delay, 0.0f, 100.0f) / 100.0f;
        const float fb = std::clamp(s.feedback, 0.0f, 90.0f) / 100.0f;
        const double want = std::clamp((double)s.delayMs, 20.0, 1900.0) * sr / 1000.0;
        if (smDelay < 0) smDelay = want;
        if (dMix > 0 || tail(dTail)) {
            const size_t L = dl[0].size();
            for (uint32_t i = 0; i < n; ++i) {
                smDelay += (want - smDelay) * 0.0005; // a time change glides, it does not click
                double rp = (double)dlPos - smDelay;
                while (rp < 0) rp += (double)L;
                const size_t a = (size_t)rp % L, b = (a + 1) % L;
                const float fr = (float)(rp - std::floor(rp));
                const float eL = dl[0][a] * (1 - fr) + dl[0][b] * fr, eR = dl[1][a] * (1 - fr) + dl[1][b] * fr;
                dlLp[0] += 0.35f * (eR - dlLp[0]);
                dlLp[1] += 0.35f * (eL - dlLp[1]);
                dl[0][dlPos] = (io[0][i] + io[1][i]) * 0.5f * dMix + dlLp[0] * fb;
                dl[1][dlPos] = dlLp[1] * fb; // the first echo left, then across (ping-pong)
                io[0][i] += eL;
                io[1][i] += eR;
                if (++dlPos >= L) dlPos = 0;
            }
            dTail = dMix > 0 ? (uint32_t)(sr * 12) : (dTail > n ? dTail - n : 0);
        }
        // Reverb: Freeverb (8 combs, 4 all-passes per side), a short pre-delay, the size its decay
        const float rMix = std::clamp(s.reverb, 0.0f, 100.0f) / 100.0f;
        if (rMix > 0 || tail(rTail)) {
            const float size = std::clamp(s.reverbSize, 0.0f, 100.0f) / 100.0f;
            const float roomFb = 0.70f + 0.28f * size, damp = 0.45f - 0.25f * size;
            const size_t P = preDelay.size();
            for (uint32_t i = 0; i < n; ++i) {
                const float in = preDelay[pdPos];
                preDelay[pdPos] = (io[0][i] + io[1][i]) * 0.015f * rMix;
                if (++pdPos >= P) pdPos = 0;
                for (int c = 0; c < 2; ++c) {
                    float acc = 0;
                    for (Comb& cb : comb[c]) acc += cb.run(in, roomFb, damp);
                    for (Allpass& a : ap[c]) acc = a.run(acc);
                    wet[c] = acc;
                }
                io[0][i] += wet[0] * 3.0f;
                io[1][i] += wet[1] * 3.0f;
            }
            rTail = rMix > 0 ? (uint32_t)(sr * 15) : (rTail > n ? rTail - n : 0);
        }
    }

private:
    static bool tail(uint32_t t) { return t > 0; }
    double sr = 48000;
    Biquad band[2][3];
    float lastLow = 1e9f, lastMid = 1e9f, lastHigh = 1e9f;
    std::vector<float> ch[2]; size_t chPos = 0; double phase = 0;
    std::vector<float> dl[2]; size_t dlPos = 0; float dlLp[2] = { 0, 0 }; double smDelay = -1; uint32_t dTail = 0;
    Comb comb[2][8]; Allpass ap[2][4]; std::vector<float> preDelay; size_t pdPos = 0; float wet[2] = { 0, 0 }; uint32_t rTail = 0;
};

} // namespace pe::fx
