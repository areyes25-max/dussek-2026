"""The reed's offline renders and measures (proto/brass/reed.py): a held note through rt.c at constant controls,
radiated by the note's own filters, and the measures the fits judge it by. The measures are pure and run here; the
render keeps the reed's bore in module state, so it runs in a process of its own under the reed project (the
layout is solved once)."""
import json
import os
import subprocess
import sys

import numpy as np
import pytest

import project as P

BRASS = os.path.join(P.REPO, "proto", "brass")
sys.path.insert(0, BRASS)
import reed  # noqa: E402  (its bore's modules load on use)

SR = 44100


def run(code):
    env = {**os.environ, "BRASS_PROJECT": "clarinet", "OMP_NUM_THREADS": "1"}
    env.pop("PE_RECORD", None)
    r = subprocess.run([sys.executable, "-c", code], cwd=BRASS, env=env, capture_output=True, text=True)
    assert r.returncode == 0, r.stderr[-2000:]
    return json.loads(r.stdout.splitlines()[-1])


def test_a_sine_s_harmonics():
    t = np.arange(4 * SR) / SR
    x = np.sin(2 * np.pi * 220 * t) + 0.1 * np.sin(2 * np.pi * 660 * t)
    h = reed.harmonics(x, SR, 57)
    assert abs(h[0]) < 0.5 and abs(h[2] + 20) < 0.5, h[:3]
    assert len(h) == 16


def test_a_weighting_at_1_khz_is_flat():
    t = np.arange(2 * SR) / SR
    x = 0.3 * np.sin(2 * np.pi * 1000 * t)
    assert abs(reed.level_a(x, SR) - 10 * np.log10(np.mean(x ** 2))) < 0.1


def test_the_floor_of_a_sine_in_noise():
    rng = np.random.default_rng(3)
    t = np.arange(10 * SR) / SR
    sig = 0.5 * np.sin(2 * np.pi * 220 * t)
    sd = 0.001
    x = sig + sd * rng.standard_normal(len(t))
    # the noise in 2-10 kHz re the whole: its share of the band, over the sine's and the noise's power
    want = 10 * np.log10(sd ** 2 * 8000 / (SR / 2) / (0.5 ** 2 / 2 + sd ** 2))
    assert abs(reed.floor(x, SR, 57) - want) < 1.0


def test_line_fit_leaves_out_an_outlier():
    k = np.arange(10)
    v = 2.0 * k + 1.0
    v[4] += 12.0
    fit = reed.line_fit(k, v)
    assert np.allclose(fit, 2.0 * k + 1.0, atol=1e-9)


def test_the_bands_of_a_harmonic_tone():
    t = np.arange(3 * SR) / SR
    f0 = 440.0 * 2 ** (-9 / 12)  # C4
    x = sum(np.sin(2 * np.pi * k * f0 * t) / k for k in range(1, 30))
    b = reed.bands(x, SR, 60)
    assert len(b) == 4
    assert all(p > f + 30 for p, f in b[:3]), b


@pytest.fixture(scope="module")
def played():
    return run("""
import json, time
import numpy as np
import reed, lattice
t0 = time.time()
x = reed.steady(60, 0.6)
s = x[int(1.8 * reed.SR):int(2.3 * reed.SR)]
n = lattice.Note(60)
h = reed.filters(n)
h2 = reed.filters(n, corr=lambda f: np.full(len(f), 2.0))
b = reed.steady(60, 0.6, dur=1.0, breath=0.1)
print(json.dumps(dict(n=len(x), cents=1200 * np.log2(reed.pitch(s, reed.SR, n.f) / n.f), level=10 * np.log10(np.mean(s ** 2)),
                      hn=len(h), gain=float(np.abs(h2).max() / np.abs(h).max()), 
                      noisy=float(np.std(b)), seconds=time.time() - t0)))
""")


def test_a_reed_note_sounds_on_its_key(played):
    assert played["n"] == int(2.5 * SR)
    assert abs(played["cents"]) < 30, played["cents"]
    assert played["level"] > -60, played["level"]


def test_filters_are_the_notes_own(played):
    assert played["hn"] == 4096  # the flow through the note's Z H: one filter in place of the bell's and the residual
    assert played["gain"] == pytest.approx(2.0)


def test_a_breath_adds_noise(played):
    assert played["noisy"] > 0


# the woodwinds spike's measure.py on Iowa's C4 at mf, computed once: its steady middle's pitch (Hz) and harmonics (dB)
C4_MF = os.path.join(P.REPO, "data", "iowa-mis-woodwinds", "notes", "BbClar", "mf", "60.wav")
SPIKE_PITCH = 260.9049782197358
SPIKE_HARMONICS = [-24.895618837494354, -60.91859370003833, -27.438764026229492, -46.30297426289751, -38.589599508554166,
                   -48.65448136789831, -52.9535763789255, -60.40220644981203, -65.82101595075558, -67.26676344489186,
                   -64.67874558336052, -60.12581847507519, -67.18687125393897, -65.55781165510673, -80.51647258970947,
                   -73.53222040360913]


@pytest.mark.skipif(not os.path.exists(C4_MF), reason="data/iowa-mis-woodwinds not fetched")
def test_the_measures_are_the_spike_s():
    """The pitch and the harmonics the fits judge a recording by are the spike's, on one of its recordings."""
    import soundfile as sf
    x, sr = sf.read(C4_MF, always_2d=True)
    s = reed.sustained(x.mean(axis=1), sr)
    assert len(s) == sr
    assert reed.pitch(s, sr, reed.f_of(60)) == pytest.approx(SPIKE_PITCH, rel=1e-12)
    assert reed.harmonics(s, sr, 60) == pytest.approx(SPIKE_HARMONICS, abs=1e-9)


def test_the_rise_of_a_tone():
    """The rise to 3 dB under the sustained level, in 5 ms frames from the start: a tone growing as 1 - exp(-t / 20 ms)
    reaches 0.707 of its level after 24.6 ms (in the 25 ms frame); nothing before the start counts."""
    t = np.arange(SR) / SR
    x = np.sin(2 * np.pi * 440 * t) * (1 - np.exp(-t / 0.02))
    assert reed.rise(np.concatenate([np.zeros(SR // 10), x]), SR, 0.1) == 25.0
    assert reed.start_of(np.concatenate([np.zeros(SR // 10), x]), SR) == pytest.approx(0.1 - 0.01 + 0.0021, abs=1e-3)
