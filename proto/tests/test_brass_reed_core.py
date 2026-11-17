"""rt.c's reed pieces (proto/brass/rt.c): the tongue holds the reed shut, the seed starts a note sooner, a fingering change
glides the paired modes without a step (also into the register, where the pairs do not line up by index), and the
brass's path renders as before. The modules keep an
instrument in module state, so the reed's bore is solved in a process of its own under the clarinet project and the core
runs in another (live.lib() wants a brass project's module state, not its instrument)."""
import json
import os
import subprocess
import sys

import numpy as np
import pytest
from scipy.signal import butter, sosfilt

import project as P

BRASS = os.path.join(P.REPO, "proto", "brass")
ENGINE_FIXTURE = os.path.join(P.REPO, "engine", "tests", "fixtures", "brass_core.json")
SR = 44100
LOW, HIGH = 58, 62  # a major third, both in the chalumeau (Task 2's lattice)
# into the register: G4 fingered in the chalumeau (modes on 1, 3, 5 ... of the note) to B-flat 4 in the clarion
# (modes near 1/3, 1, 5/3 ...): the new mode on the note is the old one's index plus one
CHAL, CLAR = 67, 70


def run(project, code, stdin=""):
    env = {**os.environ, "BRASS_PROJECT": project, "OMP_NUM_THREADS": "1"}
    env.pop("PE_RECORD", None)
    r = subprocess.run([sys.executable, "-c", code], cwd=BRASS, env=env, capture_output=True, text=True, input=stdin)
    assert r.returncode == 0, r.stderr[-2000:]
    return json.loads(r.stdout.splitlines()[-1])


# the reed's numbers (projects/clarinet/project.toml): one reed mode at 2500 Hz, Q 1.5, closing (sign -1); its mass per
# area from the stiffness; the rest opening, the chalumeau's width, the softness of the closing; the breath at
# gamma 0.6 of the closing pressure. The filters pass the mouthpiece pressure (h [1], r [0]): no radiation yet.
CORE = """
import ctypes, json, sys
import numpy as np
from scipy.signal import firwin
import live, sim

SR, OS, CTRL = 44100.0, live.OS, live.CTRL
T = 1 / (SR * OS)
K, FR, Y0, W, SOFT, RHO = 1.2e7, 2500.0, 4.5e-4, 0.0048, 4.5e-5, 1.2
MU = K / (2 * np.pi * FR) ** 2
PM = 0.6 * K * Y0
LIPS = [np.array([1.0]), np.array([1.5]), np.array([-1.0]), np.array([1.0])]
DEC = firwin(96, 20000, fs=SR * OS).astype(np.float64)
H, R, NOISE = np.array([1.0]), np.array([0.0]), np.zeros(1)
RAMP = round(0.04 * SR / CTRL)  # the breath's rise, blocks
SEED, SEED_S = 6.0, 0.02  # the tongue's push, m/s at its peak, over 20 ms
PAIR_MAX, PAIR_TOL = 6.0, 0.12
lib = live.lib()
p = live.ptr


class Bank:
    def __init__(self, f, modes):
        E, r = sim.modal_coeffs([tuple(m) for m in modes], T)
        self.f, self.m = f, len(E)
        self.a = [np.ascontiguousarray(x) for x in (E.real, E.imag, r.real, r.imag)]


class Reed:
    def __init__(self):
        self.s = lib.rt_new()
        lib.rt_filters(self.s, p(H), 1, p(R), 1)
        self.pm = 0.0

    def block(self, bank, pm1, tongue0=0.0, tongue1=0.0):
        o = np.zeros(CTRL)
        lib.rt_process(self.s, CTRL, self.pm, pm1, FR, FR, 0.0, 0.0, W, W, tongue0, tongue1, 0.0, MU, bank.m,
                       *(p(x) for x in bank.a), 1, *(p(x) for x in LIPS), Y0, SOFT, RHO, SR, p(NOISE), 1, 0.0,
                       p(DEC), len(DEC), p(o))
        self.pm = pm1
        return o


def note(bank, blocks, seed, reed=None):
    r = reed or Reed()
    y = []
    for b in range(blocks):
        if seed and b == RAMP:
            lib.rt_seed(r.s, SEED, bank.f, SEED_S, SR)
        y.append(r.block(bank, PM * min(1.0, (b + 1) / RAMP)))
    return r, np.concatenate(y)


banks = {int(k): Bank(*v) for k, v in json.loads(sys.stdin.read()).items()}
lo, hi = banks[%d], banks[%d]
got = {}
r = Reed()
got["tongued"] = float(np.max(np.abs(np.concatenate([r.block(lo, PM, 1.0, 1.0) for _ in range(2)]))))
r = Reed()
got["free"] = float(np.max(np.abs(np.concatenate([r.block(lo, PM) for _ in range(2)]))))
got["seeded"] = note(lo, 690, True)[1].tolist()
got["unseeded"] = note(lo, 690, False)[1].tolist()
before = 345


# a seeded note on old, then new: "glide" (rt_glide), "rest" (a glide from no modes: the new fingering from rest) or
# "carry" (no rt_glide: the state goes on by index, as before the glide)
def change(old, new, kind):
    r, a = note(old, before, True)
    if kind != "carry":
        lib.rt_glide(r.s, old.m if kind == "glide" else 0, *(p(x) for x in old.a), old.f, new.f, PAIR_MAX, PAIR_TOL)
    b = np.concatenate([r.block(new, PM) for _ in range(276)])
    return np.concatenate([a, b]).tolist()


got["glide"], got["rest"] = change(lo, hi, "glide"), change(lo, hi, "rest")
got["register"], got["register_carry"] = change(banks[%d], banks[%d], "glide"), change(banks[%d], banks[%d], "carry")
got["change"] = before * CTRL
got["f_clar"] = banks[%d].f
print(json.dumps(got))
""" % (LOW, HIGH, CHAL, CLAR, CHAL, CLAR, CLAR)


@pytest.fixture(scope="module")
def reed():
    """The fingerings' modes from the lattice (solved once), then every case through rt.c."""
    banks = run("clarinet", f"""
import json, lattice
notes = {{m: lattice.Note(m) for m in ({LOW}, {HIGH}, {CHAL}, {CLAR})}}
print(json.dumps({{m: [n.f, [list(map(float, q)) for q in n.modes]] for m, n in notes.items()}}))
""")
    got = run("horn", CORE, json.dumps(banks))
    for k in ("seeded", "unseeded", "glide", "rest", "register", "register_carry"):
        got[k] = sosfilt(butter(2, 50, "highpass", fs=SR, output="sos"), np.array(got[k]))  # the breath's DC out
    return got


def rms_db(x):
    return 10 * np.log10(np.mean(x * x) + 1e-30)


def frames(x, n):
    k = len(x) // n
    return 10 * np.log10(np.mean(x[: k * n].reshape(k, n) ** 2, axis=1) + 1e-30)


def band_db(x, lo=6000.0):
    """The power over `lo` Hz in a Hann window, dB."""
    X = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
    return 10 * np.log10(np.sum(X[np.fft.rfftfreq(len(x), 1 / SR) > lo]) + 1e-30)


def test_the_tongue_holds_the_reed_shut(reed):
    assert reed["free"] > 1.0  # the same blocks untongued: the breath moves the reed and the flow starts
    assert reed["tongued"] == 0.0


def rise(x):
    """When the note's 5 ms RMS first reaches 3 dB under its level (the last 0.3 s), s."""
    level = rms_db(x[-int(0.3 * SR):])
    env = frames(x, int(0.005 * SR))
    return np.argmax(env >= level - 3.0) * 0.005, level


def test_a_seed_starts_a_note_sooner(reed):
    (t_seed, l_seed), (t_free, l_free) = rise(reed["seeded"]), rise(reed["unseeded"])
    assert abs(l_seed - l_free) < 3.0, (l_seed, l_free)  # the same note in the end
    assert t_seed < t_free, (t_seed, t_free)


def dip(x, c):
    """The 1 ms RMS's deepest dip in the 30 ms after the change, re the 30 ms before, dB."""
    w30 = int(0.03 * SR)
    return frames(x[c:c + w30], int(0.001 * SR)).min() - rms_db(x[c - w30:c])


def band_off(x, c):
    """The band over 6 kHz in the 30 ms after the change re each steady note's (the old before it, the new 0.2 s
    after), dB."""
    w30, at = int(0.03 * SR), c + int(0.2 * SR)
    change = band_db(x[c:c + w30])
    return [change - band_db(x[c - 2 * w30:c - w30]), change - band_db(x[at:at + w30])]


def test_a_fingering_change_glides_without_a_step(reed):
    x, rest, c = reed["glide"], reed["rest"], reed["change"]
    assert dip(rest, c) < -6.0  # the measures see a new fingering starting from rest
    assert not all(abs(d) < 12.0 for d in band_off(rest, c)), band_off(rest, c)
    assert dip(x, c) > -6.0, dip(x, c)
    assert all(abs(d) < 12.0 for d in band_off(x, c)), band_off(x, c)


def pitch(x):
    """The strongest partial over 80 Hz, Hz."""
    X = np.abs(np.fft.rfft(x * np.hanning(len(x)), 8 * len(x)))
    f = np.fft.rfftfreq(8 * len(x), 1 / SR)
    return f[f > 80][np.argmax(X[f > 80])]


def test_a_register_change_pairs_by_harmonic_not_by_index(reed):
    """Carried by index, the old note's vibration lands on the clarion's mode a twelfth under the note: it dips and
    the reed falls to that mode. Paired by harmonic it glides onto the note."""
    c, f = reed["change"], reed["f_clar"]
    at = c + int(0.2 * SR)
    cents = lambda x: 1200 * np.log2(pitch(x[at:at + 4096]) / f)
    assert dip(reed["register"], c) > -6.0, dip(reed["register"], c)
    assert abs(cents(reed["register"])) < 30.0, cents(reed["register"])
    assert dip(reed["register_carry"], c) < dip(reed["register"], c) - 3.0
    assert abs(cents(reed["register_carry"])) > 600.0, cents(reed["register_carry"])


def test_the_brass_path_is_unchanged(tmp_path):
    """core_fixture.py's brass note through rt.c, against the engine's fixture (engine/tests/fixtures/brass_core.json, made
    by the rt.c before the reed): within 1e-9 of the peak, another CPU's rounding under -march=native -ffast-math but no
    change of the loop. The bits on this machine are held by `just reproduce` and the port check
    (test_brass_port.py)."""
    out = tmp_path / "core.json"
    env = {**os.environ, "BRASS_PROJECT": "horn", "OMP_NUM_THREADS": "1"}
    subprocess.run([sys.executable, os.path.join(BRASS, "core_fixture.py"), str(out)], cwd=BRASS, env=env, check=True)
    got = np.array(json.load(open(out))["out"])
    want = np.array(json.load(open(ENGINE_FIXTURE))["out"])
    assert got.shape == want.shape
    assert np.max(np.abs(got - want)) <= 1e-9 * np.max(np.abs(want))


FLOW = CORE.split("banks = {int(k)")[0] + """
lib.rt_filters_flow.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_double), ctypes.c_int]
lib.rt_filters_flow.restype = ctypes.c_int
banks = {int(k): Bank(*v) for k, v in json.loads(sys.stdin.read()).items()}
lo, hi = banks[%d], banks[%d]
DELAY = 1000  # base samples: longer than the fade (441), so a fade of the outputs would cut what the old one still rings


def flow_note(zh_old, zh_new):
    r = Reed()
    lib.rt_filters_flow(r.s, p(zh_old), len(zh_old))
    _, a = note(lo, 345, True, r)
    lib.rt_filters_flow(r.s, p(zh_new), len(zh_new))
    lib.rt_glide(r.s, lo.m, *(p(x) for x in lo.a), lo.f, hi.f, PAIR_MAX, PAIR_TOL)
    b = np.concatenate([r.block(hi, PM) for _ in range(60)])
    return np.concatenate([a, b])


one, two = np.array([1.0]), np.array([2.0])
delayed = np.zeros(DELAY); delayed[-1] = 1.0
got = {"same": flow_note(one, one).tolist(), "double": flow_note(one, two).tolist(),
       "ring": flow_note(delayed, np.zeros(1)).tolist(), "change": 345 * CTRL}


# one note held, the output stage changed to an identity filter again at each block in `at` (a block listed twice:
# two changes before it): the output is the flow times the shares' sum
def shares(at):
    r = Reed()
    lib.rt_filters_flow(r.s, p(one), 1)
    y = []
    for b in range(420):
        for _ in range(at.count(b)):
            lib.rt_filters_flow(r.s, p(one), 1)
        if b == RAMP:
            lib.rt_seed(r.s, SEED, lo.f, SEED_S, SR)
        y.append(r.block(lo, PM * min(1.0, (b + 1) / RAMP)))
    return np.concatenate(y).tolist()


got["shares"] = {"none": shares([]), "one": shares([345]), "two within 10 ms": shares([345, 348]),
                 "two in a block": shares([345, 345])}
got["too_long"] = lib.rt_filters_flow(Reed().s, p(np.zeros(4097)), 4097)
print(json.dumps(got))
""" % (LOW, HIGH)


@pytest.fixture(scope="module")
def flow():
    banks = run("clarinet", f"""
import json, lattice
notes = {{m: lattice.Note(m) for m in ({LOW}, {HIGH})}}
print(json.dumps({{m: [n.f, [list(map(float, q)) for q in n.modes]] for m, n in notes.items()}}))
""")
    got = run("horn", FLOW, json.dumps(banks))
    return {k: np.array(v) if isinstance(v, list) and k != "shares" else v for k, v in got.items()}


def test_the_flow_s_shares_sum_to_one(flow):
    """A note change gives the new note the flow's share rising, the old ones what they had, falling, at every change
    (also two within the 10 ms fade, or two before one block): with an identity
    filter on every slot the output is the flow itself throughout (the shares summed to 2 on two changes in a block,
    +6 dB, and a slot jumped back up to 1: a click through Z H)."""
    base = np.array(flow["shares"]["none"])
    tol = 1e-9 * np.abs(base).max()
    for name, x in flow["shares"].items():
        assert np.abs(np.array(x) - base).max() <= tol, (name, np.abs(np.array(x) - base).max() / np.abs(base).max())


def test_an_oversize_flow_filter_is_refused(flow):
    """rt.c holds HMAX (4096) taps of history: a longer filter is an error, never cut short."""
    assert flow["too_long"] != 0


def test_a_reed_radiates_each_note_s_share_of_the_flow(flow):
    """rt_filters_flow: each note's share of the flow through its own filter (the spike's loop.radiate). From an
    identity filter to a doubling one the output rises from the flow to twice it as the new note's share does, over
    the fade (10 ms); an old filter that delays by 1000 samples still plays its own past flow long after the fade (a
    fade of the two filters' outputs would cut it at 441), and then nothing (the new filter is 0)."""
    c, same, double, ring = flow["change"], flow["same"], flow["double"], flow["ring"]
    k = np.arange(c + 50, c + 400)
    g = (k - c + 1) / 441.0
    ok = np.abs(same[k]) > 1e-6 * np.abs(same).max()
    assert np.allclose(double[k][ok] / same[k][ok], 1.0 + g[ok], rtol=0.02, atol=0.01)
    assert np.allclose(double[c + 500:], 2.0 * same[c + 500:], rtol=1e-9, atol=1e-12)
    steady = np.abs(same[c - 2000:c]).max()
    assert np.abs(ring[c + 600:c + 900]).max() > 1e-3 * steady  # the old note rings out its own past flow
    assert np.all(ring[c + 1000 + 441 + 1:] == 0.0)
