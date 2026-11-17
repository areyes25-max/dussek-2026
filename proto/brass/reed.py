"""A reed instrument's offline renders and measures: a held note through rt.c's reed path at constant controls,
radiated by the note's own filters, and what the fits judge it by (harmonics, pitch, A-weighted level, the breath's
floor, the spectrum's top per octave, a start's rise, a line through the keys).

The measures are pure. The render needs the reed's bore, which lives in module state (BRASS_PROJECT): its modules
(rt_tables.py's filter helpers, live.py's library and rates) load on first use, so the measures import anywhere."""
import os

import numpy as np
from scipy.signal import bilinear, butter, firwin, lfilter, sosfilt

SR = 44100
NH = 16
SEED = 6.0  # the tongue's push, m/s at its peak: a note starts from it
RAMP_S = 0.04  # the breath's rise, s
NOISE_STD = 0.015  # the noise table's rms (live.streams'): the flow's fraction at a burst of 1
# the flow-to-air filter (Z H, rt_filters_flow), taps and the grid it is cut from: the clarion's h1-h4 within 1 dB of
# the full Z H on every key at 4096 (0.5 at most; 2048: 9 of 14, 2.5); the chalumeau's lowest keys 2 dB off at 4096,
# 8192 would close them, over rt.c's HMAX
ZH_LEN, ZH_NFFT = 4096, 32768
BANDS = ((1000, 2000), (2000, 4000), (4000, 8000), (8000, 16000))


def f_of(m):
    return 440.0 * 2 ** ((m - 69) / 12)


# ---- the measures (the spike's measure.py, even.py, bands.py, check.py's rise, render.py's line_fit, period_f0) ----

def pitch(x, sr, near):
    """The period's frequency near `near` Hz, from the autocorrelation's peak."""
    s = x - x.mean()
    ac = np.fft.irfft(np.abs(np.fft.rfft(s, 2 * len(s))) ** 2)[: len(s)]
    lo, hi = int(sr / (near * 1.2)), int(sr / (near / 1.2))
    i = lo + int(np.argmax(ac[lo:hi]))
    a, b, c = ac[i - 1:i + 2]
    return sr / (i + 0.5 * (a - c) / (a - 2 * b + c))


def period_f0(s, sr, lo=60.0, hi=2000.0):
    """The pitch as the first strong autocorrelation peak (the period, not the loudest harmonic: a note a register up
    reads as that); nan if none."""
    s = s - s.mean()
    ac = np.fft.irfft(np.abs(np.fft.rfft(s, 2 * len(s))) ** 2)[: len(s)]
    ac /= ac[0] + 1e-30
    a, b = int(sr / hi), int(sr / lo)
    seg = ac[a:b]
    top = seg.max()
    for i in range(1, len(seg) - 1):
        if seg[i] > 0.85 * top and seg[i] >= seg[i - 1] and seg[i] >= seg[i + 1]:
            j = a + i
            y0, y1, y2 = ac[j - 1:j + 2]
            return float(sr / (j + 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2)))
    return float("nan")


def harmonics(x, sr, m):
    """dB of each of the first NH harmonics' peak in a Hann window, re a full-scale sine; None above the band.
    The note's frequency is measured near key m's."""
    f0 = pitch(x, sr, f_of(m))
    n = len(x)
    w = np.hanning(n)
    X = np.abs(np.fft.rfft(x * w)) * 2 / w.sum()
    fr = np.fft.rfftfreq(n, 1 / sr)
    out = []
    for k in range(1, NH + 1):
        f = k * f0
        if f > sr / 2 - 500:
            out.append(None)
            continue
        band = (fr > f - 0.3 * f0) & (fr < f + 0.3 * f0)
        out.append(float(20 * np.log10(X[band].max() + 1e-12)))
    return out


def a_weight(x, sr):
    f1, f2, f3, f4 = 20.598997, 107.65265, 737.86223, 12194.217
    nums = [(2 * np.pi * f4) ** 2 * 10 ** (1.9997 / 20), 0, 0, 0, 0]
    dens = np.polymul([1, 4 * np.pi * f4, (2 * np.pi * f4) ** 2], [1, 4 * np.pi * f1, (2 * np.pi * f1) ** 2])
    dens = np.polymul(np.polymul(dens, [1, 2 * np.pi * f3]), [1, 2 * np.pi * f2])
    b, a = bilinear(nums, dens, sr)
    return lfilter(b, a, x)


def level_a(x, sr):
    """The A-weighted level, dB (the filter's first 10 ms left out)."""
    return float(10 * np.log10(np.mean(a_weight(x, sr)[int(0.01 * sr):] ** 2) + 1e-20))


def floor(x, sr, m, lo=2000, hi=10000):
    """The floor between the harmonics, lo-hi Hz, as power per Hz times the band, dB re the note's power."""
    n = 4096
    w = np.hanning(n)
    frames = [x[i:i + n] * w for i in range(0, len(x) - n, n // 2)]
    if not frames:
        return float("nan")
    P = np.mean(np.abs(np.fft.rfft(frames, axis=1)) ** 2, axis=0)
    fr = np.fft.rfftfreq(n, 1 / sr)
    f0 = f_of(m)
    off = np.abs(fr / f0 - np.round(fr / f0))
    band = (fr > lo) & (fr < hi) & (off > 0.3)
    return float(10 * np.log10(np.median(P[band]) * ((fr > lo) & (fr < hi)).sum() / P[fr > 50].sum()))


def bands(x, sr, m):
    """Per octave band (1-2, 2-4, 4-8, 8-16 kHz) the harmonics' median peak and the floor between them, dB re the
    note's power; (nan, nan) where the band holds no harmonic."""
    n = 8192
    w = np.hanning(n)
    P = np.mean([np.abs(np.fft.rfft(x[i:i + n] * w)) ** 2 for i in range(0, len(x) - n, n // 2)], axis=0)
    fr = np.fft.rfftfreq(n, 1 / sr)
    tot = P[fr > 50].sum()
    f0 = pitch(x, sr, f_of(m))
    out = []
    for lo, hi in BANDS:
        pk, fl = [], []
        for k in range(1, int(hi / f0) + 1):
            f = k * f0
            if f < lo or f > hi:
                continue
            near = np.abs(fr - f) < 0.15 * f0
            between = (np.abs(fr - f) > 0.3 * f0) & (np.abs(fr - f) < 0.5 * f0)
            pk.append(P[near].max())
            fl.append(np.median(P[between]))
        out.append((float(10 * np.log10(np.median(pk) / tot)), float(10 * np.log10(np.median(fl) / tot))) if pk
                   else (float("nan"), float("nan")))
    return out


def line_fit(keys, values, cut=4.0):
    """A straight line over the keys through the values, the points more than `cut` dB off it left out of a second fit;
    a value not finite left out of both. Under three points: their mean (a run limited to a few keys)."""
    k, v = np.asarray(keys, float), np.asarray(values, float)
    ok = np.isfinite(v)
    if ok.sum() < 3:
        return np.full(len(k), float(np.mean(v[ok])) if ok.any() else np.nan)
    c = np.polyfit(k[ok], v[ok], 1)
    near = ok & (np.abs(v - np.polyval(c, k)) < cut)
    return np.polyval(np.polyfit(k[near], v[near], 1) if near.sum() >= 2 else c, k)


def sustained(x, sr):
    """A recorded note's steady middle: from 0.25 s after the start to 0.25 s before the end, at most 1 s."""
    a = int(0.25 * sr)
    b = min(len(x) - int(0.25 * sr), a + sr)
    return x[a:b] if b - a > sr // 5 else x[len(x) // 4: 3 * len(x) // 4]


def start_of(x, sr):
    """Where a recorded note starts, s: 10 ms before it first reaches a tenth of its peak (Iowa's notes start tongued)."""
    return max(0.0, int(np.argmax(np.abs(x) > 0.1 * np.abs(x).max())) / sr - 0.01)


def rise(x, sr, start):
    """The time (ms) from `start` (s) until the level (5 ms frames) first reaches 3 dB under the sustained part's (the
    median of 250-600 ms after the start); nan if it never does."""
    n = int(0.005 * sr)
    k = len(x) // n
    e = np.sqrt(np.mean(x[:k * n].reshape(k, n) ** 2, axis=1))
    a = int(start * sr) // n
    steady = np.median(e[a + 50:a + 120])
    hit = np.nonzero(e[a:] > steady / np.sqrt(2))[0]
    return 5.0 * hit[0] if len(hit) else float("nan")


# ---- the render ----

_NOTES = {}


def _on_harmonic(fq, f, tol=0.06):
    k = max(1, round(fq / f))
    return abs(fq / (k * f) - 1) < tol


def note(m, extra=None, fitted=True):
    """The key's bore as the reed plays it, once per process (`extra`: the barrel's length, m; None: the fit's barrel
    for the key (`tuning`, with a project at hand and `fitted`), else tuned on the note's played peak).
    In the clarion the player keeps the reed to the note: the modes under it and the peaks between its harmonics are
    left out (a mode a sixth off a harmonic took a note over)."""
    if extra is None and fitted and os.environ.get("BRASS_PROJECT"):
        import spec
        extra = spec.load("tuning", {}).get(str(m))
    if (m, extra) not in _NOTES:
        import lattice
        n = lattice.Note(m, extra)
        if n.register:
            n.modes = [q for q in n.modes if q[0] > 0.7 * n.f and _on_harmonic(q[0], n.f)]
        _NOTES[m, extra] = n
    return _NOTES[m, extra]


def filters(n, corr=None):
    """What the note radiates its flow through (rt_filters_flow): Z H, the mouthpiece's whole impedance times the bell's
    pressure transfer (the spike's loop.radiate: each fingering's flow through its own transfer), ZH_LEN taps cut from
    a ZH_NFFT grid; `corr`, a gain over frequency (a function of Hz, or an array on that grid), scales it; its sum 0
    (the fade left a DC gain, and the reed's flow carries DC: the clarion's output sat 42 dB under full scale)."""
    from rt_tables import fade_tail
    freqs = np.fft.rfftfreq(ZH_NFFT, 1 / SR)
    Z, H = n.transfer(np.maximum(freqs, 1.0))
    ZH = Z * H
    ZH[0] = 0
    if corr is not None:
        ZH = ZH * (corr(freqs) if callable(corr) else np.asarray(corr))
    return np.ascontiguousarray(no_dc(fade_tail(np.fft.irfft(ZH, ZH_NFFT), ZH_LEN)))


def no_dc(h):
    """The filter with its sum taken out under a Hann window over its length (the correction below about 40 Hz)."""
    w = np.hanning(len(h) + 2)[1:-1]
    return h - h.sum() * w / w.sum()


def noise_table(eq=None, n=1 << 19, seed=1):
    """The breath's noise at the oversampled rate, in live.NOISE_BAND (under live.NOISE_LP), rms NOISE_STD (live.streams'); `eq`, dB at
    NOISE_HZ, shapes it by band."""
    from live import NOISE_BAND, NOISE_HZ, NOISE_LP, OS
    rng = np.random.default_rng(seed)
    nz = sosfilt(butter(2, list(NOISE_BAND), "bandpass", fs=SR * OS, output="sos"), rng.standard_normal(n))
    if NOISE_LP:
        nz = sosfilt(butter(1, NOISE_LP, "lowpass", fs=SR * OS, output="sos"), nz)
    nz = nz * NOISE_STD / np.std(nz)
    if eq is not None:
        fq = np.fft.rfftfreq(n, 1 / (SR * OS))
        g = np.interp(np.log(np.maximum(fq, 1.0)), np.log(NOISE_HZ), eq)
        nz = np.fft.irfft(np.fft.rfft(nz) * 10 ** (g / 20), n)
    return np.ascontiguousarray(nz, dtype=np.float64)


def steady(m, gamma, dur=2.5, breath=0.0, eq=None, extra=None, fitted=True, seed=SEED):
    """Key m held for dur s at mouth pressure gamma (re the reed's closing pressure), the controls constant after the
    breath's rise, radiated by the note's filters: the tongue's seed starts it. `breath`: the noise's share of the flow
    (0: the tone alone, as the fits judge it); `eq`: dB per NOISE_HZ band over the noise. Judge it over 1.8-2.3 s.
    `extra`, `fitted`: the barrel as `note` takes them; `seed`: the tongue's push, m/s."""
    import live
    import reed_tables
    import sim
    import spec
    S = spec.S
    lib, ptr, OS, CTRL = live.lib(), live.ptr, live.OS, live.CTRL
    n = note(m, extra, fitted)
    zh = filters(n)
    T = 1 / (SR * OS)
    E, rr = sim.modal_coeffs([tuple(q) for q in n.modes], T)
    bank = [np.ascontiguousarray(a) for a in (E.real, E.imag, rr.real, rr.imag)]
    hz, y0, width, lips = S["reed_hz"], S["y0"], reed_tables.width(n), reed_tables.lips()
    dec = firwin(96, 20000, fs=SR * OS).astype(np.float64)
    noise = noise_table(eq) if breath > 0 else np.zeros(1)
    burst = breath / NOISE_STD if breath > 0 else 0.0
    s = lib.rt_new()
    live.flow_filters(lib, s, zh)
    pm_top = gamma * S["reed_k"] * y0
    ramp = round(RAMP_S * SR / CTRL)
    pm, out = 0.0, []
    for b in range(int(dur * SR / CTRL)):
        if b == ramp:
            lib.rt_seed(s, seed, n.f, S["seed_s"], float(SR))
        pm1 = pm_top * min(1.0, (b + 1) / ramp)
        o = np.zeros(CTRL)
        lib.rt_process(s, CTRL, pm, pm1, hz, hz, burst, burst, width, width, 0.0, 0.0, 0.0, reed_tables.mu(), len(E),
                       *(ptr(a) for a in bank), 1, *(ptr(a) for a in lips), y0, S["soft"], sim.RHO, float(SR),
                       ptr(noise), len(noise), 0.0, ptr(dec), len(dec), ptr(o))
        pm = pm1
        out.append(o)
    lib.rt_free(s)
    x = np.concatenate(out)
    return x[: int(dur * SR)] if len(x) >= int(dur * SR) else np.pad(x, (0, int(dur * SR) - len(x)))
