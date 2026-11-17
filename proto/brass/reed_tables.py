"""A reed instrument's tables for the live player (rt_tables' shape, built and memoized through it): per key its
fingering's modes, the same at every dynamics; its flow-to-air filter (`zh`, Z H with the key's correction, in
place of the lips' bell and residual filters); the reed's
setting (none: the pressure is the key's own); and what the lips' tables have not: the reed's opening (`width`), the
breath's noise (`breath`, the burst against the noise table) and the tongue's push (`seed`, m/s), per dynamics.
`at[key]`'s pressures are log(gamma x reed_k x y0), gamma the mouth pressure re the reed's closing pressure.

The fits fill it (pressures, tuning, seeds, radiation, breath); a key or a fit not there takes the defaults below."""
import math
import multiprocessing
from concurrent.futures import ProcessPoolExecutor

import numpy as np

import reed
import spec

S = spec.S
DYNS = ("pp", "mf", "ff")
GAMMA = {"pp": 0.45, "mf": 0.6, "ff": 0.8}  # the mouth pressure re the reed's closing pressure, per dynamics
SEED = reed.SEED  # the tongue's push, m/s
BREATH = {"pp": 0.0023, "mf": 0.0023, "ff": 0.0049}  # the noise's share of the flow, before the trim (the recordings')
RAD_HZ = (1414.0, 2828.0, 5657.0)  # the radiation's correction: 0 dB at the first, the fit's two above, flat beyond
X = (0.2, 0.55, 0.9)  # the air at which each dynamics plays (live.py's pressure anchors)


def table():
    """The keys, every one its own table: no key borrows a neighbor's."""
    return list(range(S["low"], S["high"] + 1))


def mu():
    """The reed's mass per area, from its stiffness and resonance."""
    return S["reed_k"] / (2 * math.pi * S["reed_hz"]) ** 2


def lips():
    """rt.c's lips arrays (ratio, Q, sign, compliance) for the reed: one mode, closing with the pressure."""
    return [np.array([1.0]), np.array([S["reed_q"]]), np.array([-1.0]), np.array([1.0])]


def between(v, x):
    """A value per dynamics (pp, mf, ff) at the air x, between them as the pressures are; flat beyond pp and ff."""
    if x <= X[1]:
        w = (max(x, X[0]) - X[0]) / (X[1] - X[0])
        return v[0] + (v[1] - v[0]) * w
    if x <= X[2]:
        w = (x - X[1]) / (X[2] - X[1])
        return v[1] + (v[2] - v[1]) * w
    return v[2]


def gamma(m, d):
    return spec.load("pressures", {}).get(str(m), GAMMA).get(d, GAMMA[d])


def seed(m, d):
    return spec.load("seeds", {}).get(f"{m}_{d}", SEED)


def breath(m, d, b=None, trim=True):
    """The noise's share of the flow: the fit's level per dynamics, its key's and the key's dynamics' dB, the ear's trim.
    `b`: a breath fit's value in place of the project's; `trim` False: the fit's own (it matches the recordings' floor,
    the trim goes over it)."""
    b = spec.load("breath", {}) if b is None else b
    db = b.get("key", {}).get(str(m), 0.0) + b.get("key_dyn", {}).get(f"{m}_{d}", 0.0)
    db += S.get("breath_trim_db", 0.0) if trim else 0.0
    return b.get("level", BREATH).get(d, BREATH[d]) * 10 ** (db / 20)


def radiation(m):
    """The key's correction of its radiation, a gain over Hz (None: none)."""
    g = spec.load("radiation", {}).get(str(m))
    if g is None:
        return None
    lf, db = np.log(RAD_HZ), np.array([0.0, *g])
    return lambda f: 10 ** (np.interp(np.log(np.maximum(f, 1.0)), lf, db) / 20)


def width(n):
    return S["width_high"] if n.register else S["width_low"]


def build_note(m):
    import rt_tables
    n = reed.note(m, spec.load("tuning", {}).get(str(m)))
    modes = rt_tables.modes_of(n.modes)
    return m, {"f": n.f, "modes": {d: modes for d in DYNS}, "settings": {d: (0.0, 1.0) for d in DYNS},
               "zh": reed.filters(n, radiation(m)), "width": width(n),
               "breath": {d: breath(m, d) / reed.NOISE_STD for d in DYNS}, "seed": {d: seed(m, d) for d in DYNS}}


def at(m):
    """The key's pressures as live.py reads a table's `at`: (log pressure per dynamics, the lips' centers, the top over ff);
    a reed has no lips' center and nothing over ff."""
    kp = S["reed_k"] * S["y0"]
    return [math.log(gamma(m, d) * kp) for d in DYNS], [1.0, 1.0, 1.0], 1.0


def build():
    """(notes, at) for every key: the lattice solved once, before the workers fork."""
    import lattice
    lattice.layout()
    with ProcessPoolExecutor(8, mp_context=multiprocessing.get_context("fork")) as ex:
        notes = dict(ex.map(build_note, table()))
    return notes, {m: at(m) for m in table()}


def memo_key():
    """What a reed's tables are made of: its project table, its fits, and the code that builds them."""
    values = [{k: v for k, v in S.items() if k != "gain_db"}] + [spec.load(k) for k in ("pressures", "tuning", "seeds", "radiation", "breath")]
    return spec.value_hash(values) + spec.code_hash(["rt_tables.py", "reed_tables.py", "reed.py", "lattice.py", "bore.py"])
