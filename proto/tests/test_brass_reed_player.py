"""The reed in the live player (proto/brass/live.py, reed_tables.py): its tables per key, a tongued start at the key, a
slur's glide, the release, a key during the release, a key lifted during the tongue's hold, the bend. The modules keep
an instrument in module state, so everything runs in one process of its own under the clarinet project, in a copy of
it with a small fit of its own (the lattice is solved once, the tables built into the copy's cache)."""
import json
import os
import shutil
import subprocess
import sys

import numpy as np
import pytest
from scipy.signal import butter, sosfilt

import project as P

BRASS = os.path.join(P.REPO, "proto", "brass")
SR = 44100

# the fit the tables are built from: the keys without an entry take the defaults (the spike's values)
FIT = {
    "pressures": {"58": {"threshold": 0.35, "pp": 0.5, "mf": 0.62, "ff": 0.82}},
    "seeds": {"62_mf": 7.0},
    "radiation": {"70": [3.0, 6.0]},
    "breath": {"level": {"pp": 0.002, "mf": 0.003, "ff": 0.005}, "shape": [0.0, 0.0, -3.0, -6.0, -9.0],
               "key": {"58": 2.0}, "key_dyn": {"58_ff": -1.0}},
}

CODE = """
import json, sys
import numpy as np
import auto_player, live, reed, rt_tables, spec

out = sys.argv[1]
t = rt_tables.load("open")
got = {"keys": rt_tables.TABLE}
n = t["notes"]
got["width"] = [n[58]["width"], n[70]["width"]]
got["breath"] = {m: n[m]["breath"] for m in (58, 62)}
got["seed"] = {m: n[m]["seed"] for m in (58, 62)}
got["settings"] = [list(n[58]["settings"][d]) for d in ("pp", "mf", "ff")]
got["at"] = {m: [list(t["at"][m][0]), list(t["at"][m][1]), t["at"][m][2]] for m in (58, 62)}
got["same_modes"] = all(np.array_equal(n[62]["modes"][d][k], n[62]["modes"]["mf"][k]) for d in ("pp", "ff") for k in (0, 1))
got["f"] = n[62]["f"]
np.save(out + "/h70.npy", n[70]["zh"])
np.save(out + "/h70_plain.npy", reed.filters(reed.note(70)))
got["reed_hz"], got["noise"] = spec.S["reed_hz"], list(live.noise_shape())
got["band"] = [float(f) for f in live.NOISE_BAND]
got["lp"] = live.NOISE_LP
got["h_sum"] = [float(np.sum(n[m]["zh"])) for m in (58, 70)]
got["h_peak"] = float(np.abs(n[70]["zh"]).max())
got["r_len"] = [len(n[m]["zh"]) for m in (58, 70)]
got["no_lips_filters"] = not any(k in n[m] for m in (58, 70) for k in ("h", "r"))
nz = live.streams(7)[0]  # the breath's noise as the player reads it: the band, then the shape
F = np.fft.rfftfreq(1 << 14, 1 / (live.SR * live.OS))
P_ = np.mean([np.abs(np.fft.rfft(nz[i:i + (1 << 14)] * np.hanning(1 << 14))) ** 2 for i in range(0, len(nz) - (1 << 14), 1 << 14)], axis=0)
shape = lambda f: np.interp(np.log(f), np.log(live.NOISE_HZ), live.noise_shape())
got["noise_db"] = {str(f): float(10 * np.log10(P_[(F > f / 1.2) & (F < f * 1.2)].mean()) - shape(f)) for f in (400.0, 2000.0, 12000.0, 16000.0)}


class Spy:
    # rt_process's lip, burst, width and tongue per block, as the player passes them, after the block's index
    def __init__(self, lib):
        self._lib, self.calls, self.k = lib, [], -1

    def __getattr__(self, k):
        return getattr(self._lib, k)

    def rt_process(self, *a):
        self.calls.append([self.k, a[4], a[5], a[7], a[8], a[9], a[10], a[11]])
        return self._lib.rt_process(*a)


spy = Spy(live.lib())
live._LIB[:] = [spy]
step = live.Live.step


def counted(self):
    spy.k += 1
    return step(self)


live.Live.step = counted


def play(name, events, dur):
    spy.calls, spy.k = [], -1
    x = live.render_events(events, dur)
    np.save(out + "/" + name + ".npy", x)
    np.save(out + "/" + name + "_calls.npy", np.array(spy.calls))


play("one", [(0.1, "on", 62, 80), (1.0, "off", 62)], 1.4)
play("slur", [(0.1, "on", 58, 80), (0.6, "on", 62, 80), (0.62, "off", 58), (1.2, "off", 62)], 1.4)
play("again", [(0.1, "on", 62, 80), (0.6, "off", 62), (0.64, "on", 62, 80), (1.3, "off", 62)], 1.6)
play("short", [(0.1, "on", 62, 80), (0.105, "off", 62)], 0.6)
play("bend", [(0.05, "pb", 1.0), (0.1, "on", 62, 80), (0.9, "off", 62)], 1.1)
play("growl", [(0.05, "cc", 21, 127), (0.05, "cc", 23, 127), (0.05, "at", 127), (0.1, "on", 62, 80), (1.0, "off", 62)], 1.4)
init = live.Live.__init__


def to_flutter(self, *a, **k):  # the aftertouch to flutter
    init(self, *a, **k)
    self.aftertouch_to = "flutter"


live.Live.__init__ = to_flutter
play("flutter", [(0.05, "cc", 21, 127), (0.05, "cc", 23, 127), (0.05, "at", 127), (0.1, "on", 62, 80), (1.0, "off", 62)], 1.4)
live.Live.__init__ = init
# the air from 0.25 to 0 over 2 s on a held clarion key
play("sweep", [(0.05, "cc", 11, 0.25 * 127), (0.1, "on", 74, 80)] + [(0.6 + i * 0.005, "cc", 11, 0.25 * 127 * (1 - i / 400)) for i in range(401)]
     + [(2.8, "off", 74)], 2.7)
got["air"] = {f"{m}_{v}": float(auto_player.level_of(v, m)) for m in (50, 66, 82) for v in (1, 64)}
# a key in the fade on a level per key (the chalumeau's 0 dB, the clarion's -12): the output's level in dB before the key,
# just after it, after its first block and settled
live.LEVEL = {"open": {str(k): [0.0 if k < 69 else -12.0] * 4 for k in rt_tables.TABLE}}
lv = live.Live(virtual=False)
lv.handle(("cc", 11, 70))
lv.note_on(62, 80)
for _ in range(200):
    lv.step()
lv.note_off(62)
for _ in range(20):
    lv.step()
fade = [lv.level_now, lv.releasing is not None and lv.releasing < lv.rel_len]
lv.note_on(74, 80)
fade.append(lv.level_now)
lv.step()
fade.append(lv.level_now)
for _ in range(200):
    lv.step()
got["fade_level"] = fade + [lv.level_now]
live.LEVEL = {}
print(json.dumps(got))
"""


@pytest.fixture(scope="module")
def played(tmp_path_factory):
    root = tmp_path_factory.mktemp("reed_player")
    projects = root / "projects"
    os.makedirs(projects / "clarinet" / "fits")
    shutil.copy(os.path.join(P.REPO, "projects", "clarinet", "project.toml"), projects / "clarinet")
    shutil.copy(os.path.join(P.REPO, "projects", "steps.toml"), projects)
    json.dump(FIT, open(projects / "clarinet" / "fits" / "clarinet.json", "w"))
    env = {**os.environ, "BRASS_PROJECT": "clarinet", "PE_PROJECTS": str(projects), "OMP_NUM_THREADS": "1"}
    for k in ("PE_RECORD", "PE_STEP", "PE_PROJECT", "BREATH_DB"):
        env.pop(k, None)
    r = subprocess.run([sys.executable, "-c", CODE, str(root)], cwd=BRASS, env=env, capture_output=True, text=True)
    assert r.returncode == 0, r.stderr[-3000:]
    got = json.loads(r.stdout.splitlines()[-1])
    hp = butter(2, 50, "highpass", fs=SR, output="sos")
    for name in ("one", "slur", "again", "short", "bend", "growl", "flutter", "sweep"):
        got[name] = sosfilt(hp, np.load(root / f"{name}.npy"))  # the breath's DC out
        got[name + "_calls"] = np.load(root / f"{name}_calls.npy")
    got["h70"], got["h70_plain"] = np.load(root / "h70.npy"), np.load(root / "h70_plain.npy")
    return got


DT = 64 / SR  # live.CTRL samples per block
BLOCK, LIP0, LIP1, BURST1, WIDTH0, WIDTH1, TONGUE0, TONGUE1 = range(8)  # a call's columns


def rms_db(x):
    return 10 * np.log10(np.mean(x * x) + 1e-30)


def at(x, t0, t1):
    return x[int(t0 * SR):int(t1 * SR)]


def frames(x, t0, t1, n, hop=None):
    """(start s, dB) of n s RMS frames from t0 to t1, every hop s (n by default)."""
    hop = hop or n
    ts = np.arange(t0, t1 - n + 1e-9, hop)
    return ts, np.array([rms_db(at(x, t, t + n)) for t in ts])


def block_of(t):
    """The first block a key at t s plays in (render_events handles it before that block)."""
    return int(np.ceil(t / DT - 1e-9))


def test_a_tongued_note_sounds_within_15_ms_of_the_let(played):
    """Within 15 ms of the tongue's letting go: the hold (10 ms) is silent by design, and the sound needs the let (4 ms),
    the bore's way to the bell (2 ms) and a few periods to grow (no push gets 62 there under 18 ms from the key)."""
    x = played["one"]
    level = rms_db(at(x, 0.5, 0.9))
    ts, env = frames(x, 0.1, 0.2, 0.001)
    first = ts[np.argmax(env >= level - 20.0)] + 0.001
    assert env.max() >= level - 20.0
    assert env[ts < 0.1 + 0.008].max() < level - 60.0  # the tongue holds the reed shut
    hold = P.config("clarinet")["brass"]["tongue_hold"]
    assert first - 0.1 <= hold + 0.015, (first - 0.1, level)


def test_a_slur_has_no_dip(played):
    x = played["slur"]
    level = rms_db(at(x, 0.9, 1.15))
    _, env = frames(x, 0.6, 0.9, 0.02, 0.005)
    assert env.min() > level - 6.0, (env.min() - level)
    assert rms_db(at(x, 0.4, 0.55)) > level - 12.0  # the first note sounded


ZH_S = 4096 / SR  # the flow-to-air filter's length: the flow's last 93 ms still leave through it


def test_a_key_up_fades_over_its_release(played):
    """Silent once the release (the project's release_s) is over and the filter has played out the last flow."""
    x = played["one"]
    level = rms_db(at(x, 0.5, 0.9))
    _, env = frames(x, 1.0 + P.config("clarinet")["brass"]["release_s"] + ZH_S + 0.01, 1.4, 0.02)
    assert env.max() < level - 60.0, (env.max() - level)


def test_a_key_in_the_fade_is_tongued(played):
    calls, x = played["again_calls"], played["again"]
    key = calls[calls[:, BLOCK] == block_of(0.64)][0]
    assert key[TONGUE0] < 1.0 and key[TONGUE1] == 1.0  # on the reed by the end of the key's own block
    before = calls[calls[:, BLOCK] == block_of(0.64) - 1][0]
    assert before[TONGUE1] == 0.0
    assert calls[-1, TONGUE1] == 0.0
    assert abs(rms_db(at(x, 1.0, 1.25)) - rms_db(at(x, 0.35, 0.55))) < 3.0  # the second note sounds as the first


def test_a_key_lifted_in_the_hold_ends(played):
    x, calls = played["short"], played["short_calls"]
    level = rms_db(at(played["one"], 0.5, 0.9))
    _, env = frames(x, 0.105 + P.config("clarinet")["brass"]["release_s"] + ZH_S + 0.01, 0.6, 0.02)
    assert env.max() < level - 60.0, (env.max() - level)
    assert calls[-1, TONGUE1] == 0.0
    assert np.isfinite(x).all()


def test_the_bend_leaves_the_reed(played):
    calls, x = played["bend_calls"], played["bend"]
    assert np.all(calls[:, LIP0] == played["reed_hz"]) and np.all(calls[:, LIP1] == played["reed_hz"])
    assert np.isfinite(x).all()
    # it plays, quieter: the modes take the bend, the bore's flow-to-air filter stays the key's, so two semitones up
    # its harmonics leave off the bore's peaks (13 dB down)
    assert rms_db(at(x, 0.5, 0.85)) > rms_db(at(played["one"], 0.5, 0.9)) - 16.0
    sys.path.insert(0, BRASS)
    import reed
    f = played["f"] * 2 ** (2 / 12)  # the modes take the bend: two semitones up
    assert abs(1200 * np.log2(reed.pitch(at(x, 0.5, 0.85), SR, f) / f)) < 30.0


def band_db(h, lo=6000.0):
    H = np.abs(np.fft.rfft(h, 8192)) ** 2
    return 10 * np.log10(np.sum(H[np.fft.rfftfreq(8192, 1 / SR) > lo]))


def test_the_reed_tables_carry_width_breath_seed(played):
    toml = P.config("clarinet")["brass"]
    assert played["keys"] == list(range(toml["low"], toml["high"] + 1))
    assert played["width"] == [toml["width_low"], toml["width_high"]]  # 58 the chalumeau, 70 the clarion
    trim = toml["breath_trim_db"]
    want = {"58": {d: FIT["breath"]["level"][d] * 10 ** ((2.0 + (-1.0 if d == "ff" else 0.0) + trim) / 20) / 0.015
                   for d in ("pp", "mf", "ff")},
            "62": {d: FIT["breath"]["level"][d] * 10 ** (trim / 20) / 0.015 for d in ("pp", "mf", "ff")}}
    for m in ("58", "62"):
        for d in ("pp", "mf", "ff"):
            assert played["breath"][m][d] == pytest.approx(want[m][d], rel=1e-12)
    assert played["seed"]["62"] == {"pp": 6.0, "mf": 7.0, "ff": 6.0}
    assert played["seed"]["58"] == {"pp": 6.0, "mf": 6.0, "ff": 6.0}
    assert played["settings"] == [[0.0, 1.0]] * 3
    kp = toml["reed_k"] * toml["y0"]
    lp, ce, top = played["at"]["58"]
    assert lp == pytest.approx([np.log(g * kp) for g in (0.5, 0.62, 0.82)], rel=1e-12)
    assert (ce, top) == ([1.0, 1.0, 1.0], 1.0)
    assert played["at"]["62"][0] == pytest.approx([np.log(g * kp) for g in (0.45, 0.6, 0.8)], rel=1e-12)
    assert played["same_modes"]
    assert played["noise"] == FIT["breath"]["shape"]
    # 70's radiation 6 dB up over 5657 Hz, 3 dB at 2828 Hz: over 6 kHz about 6 dB
    assert abs(band_db(played["h70"]) - band_db(played["h70_plain"]) - 6.0) < 1.0


def test_growl_and_flutter_leave_a_reed_unchanged(played):
    """Growl and flutter are the lips': a reed ignores CC 21 and 23 and the aftertouch to either, sample for sample
    (the engine's player: brass_engine.rs, growl_and_flutter_leave_a_reed_unchanged; both together: port_check.py)."""
    for name in ("growl", "flutter"):
        assert np.array_equal(played["one"], played[name]), name
        assert np.array_equal(played["one_calls"], played[name + "_calls"]), name


def test_a_reed_s_breath_takes_its_own_band(played):
    """The reed's noise from 300 Hz to 18 kHz (the project's noise_band, the spike's) under a first-order low-pass at
    500 Hz (noise_lp: radiated, as flat as the recordings' floor), then the shape: the breath fills the recordings'
    floor over 8 kHz, where the brass's 500-6000 Hz left the shape at its cap, and at 1-2 kHz, where without the
    low-pass the shape sat at its cap at 750 and 1414 Hz."""
    toml = P.config("clarinet")["brass"]
    assert played["band"] == toml["noise_band"] == [300.0, 18000.0]
    assert played["lp"] == toml["noise_lp"] == 500.0
    db = played["noise_db"]
    lp = lambda f: -10 * np.log10(1 + (f / 500.0) ** 2)
    for f in ("400.0", "12000.0", "16000.0"):
        assert abs(db[f] - db["2000.0"] - (lp(float(f)) - lp(2000.0))) < 2.5, db


def test_a_reed_s_filters(played):
    """A reed note radiates its flow through its own Z H (4096 taps), in place of the bell's filter and the residual;
    it sums to 0 (the reed's flow carries DC: the clarion's output sat 42 dB under full scale)."""
    assert max(abs(v) for v in played["h_sum"]) < 1e-9 * played["h_peak"]
    assert played["r_len"] == [4096, 4096] and played["no_lips_filters"]


def test_the_brass_keep_their_breath_band():
    """A brass project has no noise_band or noise_lp: its breath stays 500-6000 Hz (the brass exports reproduce)."""
    code = "import json, live; print(json.dumps([float(f) for f in live.NOISE_BAND] + [live.NOISE_LP]))"
    env = {**os.environ, "BRASS_PROJECT": "horn"}
    for k in ("PE_RECORD", "PE_STEP", "PE_PROJECT", "PE_PROJECTS"):
        env.pop(k, None)
    r = subprocess.run([sys.executable, "-c", code], cwd=BRASS, env=env, capture_output=True, text=True)
    assert r.returncode == 0, r.stderr[-2000:]
    assert json.loads(r.stdout.splitlines()[-1]) == [500.0, 6000.0, None]


def test_a_reed_fades_under_pp_s_air(played):
    """Under pp's air (x 0.2) a reed holds pp's pressure and fades its output by d / 0.2 (its pp sits just over its
    threshold: the pressure scaled down with the air silenced it within a step). The air swept from 0.25 to 0 over 2 s
    on a held clarion key: no 1.45 ms block's level (over the last 8 blocks) more than 1.5 dB under the last above x 0.05,
    and the note alive above x 0.02 (within 6 dB of x 0.2's level less the fade)."""
    x = played["sweep"]
    n = len(x) // 64
    e = (x[:n * 64].reshape(n, 64) ** 2).sum(axis=1)
    lv = np.array([10 * np.log10(e[max(0, k - 7):k + 1].mean() + 1e-30) for k in range(n)])
    t = np.arange(n) * DT
    xs = np.clip(0.25 * (1 - (t - 0.6) / 2.0), 0.0, 0.25)
    held = t >= 0.6
    steps = np.diff(lv)[(held & (xs > 0.05))[1:]]
    assert steps.min() >= -1.5, steps.min()
    pp = np.argmax(held & (xs <= 0.2))
    alive = (np.arange(n) >= pp) & (xs > 0.02)
    left = lv[alive] - 20 * np.log10(xs[alive] / 0.2) - lv[pp]
    assert left.min() > -6.0, (left.min(), xs[alive][np.argmin(left)])


def test_a_reed_s_velocity_reaches_its_fitted_air(played):
    """The project's vel_air: velocity 1 gives the fitted pp's air (x 0.2) and 64 mf's (0.55) on every key, the reed's air
    carrying no key's offset (its pressures are fitted per key)."""
    for m in (50, 66, 82):
        assert played["air"][f"{m}_1"] == pytest.approx(25.4, abs=1e-12)
        assert played["air"][f"{m}_64"] == pytest.approx(69.85, abs=1e-12)


def test_a_reed_s_tongued_key_in_the_fade_keeps_its_level(played):
    """A key during the release's fade is tongued on the sounding reed: its output's level eases from the last note's (a
    tenth of the way per block), never set at once: a step of gain at the key."""
    before, fading, kept, first, end = played["fade_level"]
    assert fading
    assert end == pytest.approx(-12.0, abs=0.1) and before == pytest.approx(0.0, abs=0.1)
    assert kept == before
    assert first == pytest.approx(before + 0.1 * (end - before), abs=0.05)
