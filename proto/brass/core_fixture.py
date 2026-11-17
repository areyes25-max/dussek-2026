#!/usr/bin/env python3
"""The fixture engine/tests/brass_core.rs holds the Rust core to: a synthetic note through rt.c, and the reed's cases
("reed": a note with a seed, a tongue ramp, a glide between two fingerings). Numbers from a formula, no recording.
Usage (from anywhere): BRASS_PROJECT=horn core_fixture.py <out.json>"""
import ctypes
import json
import math
import sys

import numpy as np

import live

CTRL, BLOCKS = live.CTRL, 64
OS = live.OS
D = ctypes.POINTER(ctypes.c_double)


def noise_at(n):
    """A rich, repeatable stream, rounded to f32 as an export's is."""
    i = np.arange(n, dtype=float)
    return np.asarray(0.015 * np.sin(i * 0.37) * np.cos(i * 0.011) + 0.01 * np.sin(i * i * 1e-5), dtype=np.float32)


def filt(n, seed, decay):
    i = np.arange(n, dtype=float)
    return np.asarray(np.sin(i * (0.3 + 0.1 * seed)) * np.exp(-i / decay) / (1 + seed), dtype=np.float32)


def modes(f0, m):
    """Poles and residues as rt_tables.modes_of gives them, at the oversampled rate with E and r per sample."""
    w = 2 * np.pi * f0 * np.arange(1, m + 1)
    Q = 30.0 + 2.0 * np.arange(m)
    Z = 2e7 / (1 + 0.3 * np.arange(m))
    sp = -w / (2 * Q) + 1j * w * np.sqrt(1 - 1 / (4 * Q * Q))
    rc = Z * (w / Q) * sp / (sp - np.conj(sp))
    return sp, rc


def reed_modes(f0, m, register=False):
    """A reed's bore as a formula: modes near the odd harmonics (a cylinder's), a little stretched, as modes() gives them.
    In the register the note is the bore's second mode: the modes lie near 1/3, 1, 5/3 ... of it."""
    k = np.arange(m)
    h = (2 * k + 1) / (3.0 if register else 1.0) * (1 + 0.004 * k)
    w = 2 * np.pi * f0 * h
    Q = 30.0 + 4.0 * k
    Z = 3e7 / (1 + 0.2 * k)
    sp = -w / (2 * Q) + 1j * w * np.sqrt(1 - 1 / (4 * Q * Q))
    rc = Z * (w / Q) * sp / (sp - np.conj(sp))
    return sp, rc


# the reed (one closing mode at 2500 Hz, Q 1.5; its mass per area from its stiffness), its opening, the breath at
# gamma 0.6 of the closing pressure; the articulation's numbers (the spike's)
REED = dict(reed_k=1.2e7, reed_hz=2500.0, y0=4.5e-4, width_low=0.0048, width_high=0.012, soft=4.5e-5, rho=1.2, nl=0.0,
            gamma=0.6, seed=6.0, seed_s=0.02, pair_max=6.0, pair_tol=0.12)


def reed_cases(lib, sr, noise, dec):
    """The reed's cases, each from rest. "seed": the breath rises over 10 blocks, the seed at its top. "tongue": the
    tongue holds the reed for 6 blocks while the breath rises, then lets go over 3 blocks (a ramp inside a block), the
    seed with it. "glide": a seeded note, then a fingering change a major third up (a glide at block 30) with the
    width's ramp over the change. "register": the same into the register, a minor third up from G4 (the new mode on
    the note is the old one's index plus one: a pairing by index fails it). "flow": the output the flow's (rt_filters_flow)
    from the start, a seeded note, a fingering change (a glide and a new Z H), then two more within 10 ms (3 blocks
    apart): the third finds every slot sounding and takes the one nearest its end. Each case lists its blocks' controls,
    its events and the output."""
    R = REED
    T = 1 / (sr * OS)
    mu = R["reed_k"] / (2 * np.pi * R["reed_hz"]) ** 2
    pm_top = R["gamma"] * R["reed_k"] * R["y0"]
    lips = [np.array([1.0]), np.array([1.5]), np.array([-1.0]), np.array([1.0])]
    h, rr = np.array([1.0]), np.array([0.0])  # the mouthpiece pressure itself
    def bank(f0, register=False):
        sp, rc = reed_modes(f0, 9, register)
        E, r = np.exp(sp * T), rc * T / 2
        return [np.ascontiguousarray(a) for a in (E.real, E.imag, r.real, r.imag)]
    f = {"lo": 233.08, "hi": 293.66, "chal": 392.0, "clar": 466.16}
    banks = {"lo": bank(f["lo"]), "hi": bank(f["hi"]), "chal": bank(f["chal"]), "clar": bank(f["clar"], True)}
    # the flow case's Z H per note: a decaying resonance near the notes (f32 as an export's), long enough that the first
    # still rings at the third change; its changes: block, the note glided to and its filter
    def ring(n, w, decay):
        i = np.arange(n, dtype=float)
        return np.ascontiguousarray(np.asarray(np.cos(i * w) * np.exp(-i / decay), dtype=np.float32).astype(np.float64))
    zh = [ring(1201, 0.035, 300.0), ring(903, 0.045, 200.0), ring(1498, 0.03, 400.0)]  # not all in lanes of 4
    flow_at = {20: ("hi", 1), 34: ("lo", 2), 37: ("hi", 0)}
    cases = {}
    for name in ("seed", "tongue", "glide", "register", "flow"):
        old, new = ("chal", "clar") if name == "register" else ("lo", "hi")
        gliding = name in ("glide", "register")
        state = lib.rt_new()
        lib.rt_filters(state, live.ptr(h), 1, live.ptr(rr), 1)
        pm, width, tongue = 0.0, R["width_low"], 1.0 if name == "tongue" else 0.0
        ctl, events, y = [], [], []
        key = old
        for b in range(64 if name == "flow" else 48):
            if name == "flow":
                if b == 0:
                    assert lib.rt_filters_flow(state, live.ptr(zh[0]), len(zh[0])) == 0
                    events.append([b, "flow", 0])
                if b in flow_at:
                    to, i = flow_at[b]
                    lib.rt_glide(state, len(banks[key][0]), *(live.ptr(a) for a in banks[key]), f[key], f[to], R["pair_max"],
                                 R["pair_tol"])
                    events.append([b, "glide", key, f[key], f[to], R["pair_max"], R["pair_tol"]])
                    assert lib.rt_filters_flow(state, live.ptr(zh[i]), len(zh[i])) == 0
                    events.append([b, "flow", i])
                    key = to
            else:
                key = new if gliding and b >= 30 else old
            pm1 = pm_top * min(1.0, (b + 1) / 10)
            tongue1 = (1.0 if b < 6 else max(0.0, 1.0 - (b - 5) / 3)) if name == "tongue" else 0.0
            width1 = R["width_low"] + (R["width_high"] - R["width_low"]) * min(1.0, (b - 29) / 4) if gliding and b >= 30 else R["width_low"]
            if (name == "tongue" and b == 6) or (name != "tongue" and b == 10):
                lib.rt_seed(state, R["seed"], f[old], R["seed_s"], sr)
                events.append([b, "seed", R["seed"], f[old], R["seed_s"]])
            if gliding and b == 30:
                lib.rt_glide(state, len(banks[old][0]), *(live.ptr(a) for a in banks[old]), f[old], f[new], R["pair_max"], R["pair_tol"])
                events.append([b, "glide", old, f[old], f[new], R["pair_max"], R["pair_tol"]])
            o = np.zeros(CTRL)
            lib.rt_process(state, CTRL, pm, pm1, R["reed_hz"], R["reed_hz"], 0.0, 0.0, width, width1, tongue, tongue1, 0.0, mu,
                           len(banks[key][0]), *(live.ptr(a) for a in banks[key]), 1, *(live.ptr(a) for a in lips), R["y0"],
                           R["soft"], R["rho"], sr, live.ptr(noise), len(noise), R["nl"], live.ptr(dec), len(dec), live.ptr(o))
            ctl.append([pm, pm1, width, width1, tongue, tongue1, key])
            pm, width, tongue = pm1, width1, tongue1
            y.append(o)
        cases[name] = {"ctl": ctl, "events": events, "out": np.concatenate(y).tolist()}
    return {"params": {**R, "mu": mu, "f": f}, "lips": [a.tolist() for a in lips],
            "banks": {k: [a.tolist() for a in v] for k, v in banks.items()}, "zh": [a.tolist() for a in zh], "cases": cases}


def main(out):
    lib = live.lib()
    state = lib.rt_new()
    sr, T = 44100.0, 1 / (44100.0 * OS)
    sp, rc = modes(220.0, 15)
    E = np.exp(sp * T)
    r = rc * T / 2
    lr, lq, ls, lc = np.array([1.0, 1.1]), np.array([5.0, 5.0]), np.array([1.0, -1.0]), np.array([1.0, 0.5])
    params = dict(width=0.011, y0=0.00015, soft=1e-4, rho=1.2, sr=sr, nl=0.25, mu=1.5)
    noise = noise_at(4096 * OS + 1).astype(np.float64)
    dec = np.sin(np.arange(1, 97) * np.pi / 97) ** 2
    dec = dec / dec.sum()
    filters = [(filt(256, 0, 40.0), filt(64, 0, 10.0)), (filt(256, 1, 60.0), filt(64, 1, 12.0)), (filt(200, 2, 50.0), filt(48, 2, 9.0))]
    keep = [(a.astype(np.float64), b.astype(np.float64)) for a, b in filters]
    def to(c, i):
        h, rr = keep[i]
        lib.rt_filters(state, h.ctypes.data_as(D), len(h), rr.ctypes.data_as(D), len(rr))
    to(0, 0)
    change = {16: 1, 18: 2, 40: 0}
    y, ctl = [], []
    pm, lip, burst = 0.0, 200.0, 1.0
    for b in range(BLOCKS):
        if b in change:
            to(None, change[b])
        pm1 = 2500.0 * min(1.0, (b + 1) / 12) * (1 + 0.1 * math.sin(b * 0.4))
        lip1 = 220.0 * (1 + 0.01 * math.sin(b * 0.2))
        burst1 = 1.0 + 5.0 * math.exp(-b / 10)
        kick = 0.5 if b in (0, 30) else 0.0
        o = np.zeros(CTRL)
        lib.rt_process(state, CTRL, pm, pm1, lip, lip1, burst, burst1, params["width"], params["width"], 0.0, 0.0, kick, params["mu"], len(E),
                       live.ptr(E.real.copy()), live.ptr(E.imag.copy()), live.ptr(r.real.copy()), live.ptr(r.imag.copy()), 2,
                       live.ptr(lr), live.ptr(lq), live.ptr(ls), live.ptr(lc), params["y0"], params["soft"], params["rho"], sr,
                       live.ptr(noise), len(noise), params["nl"], live.ptr(dec), len(dec), live.ptr(o))
        ctl.append([pm, pm1, lip, lip1, burst, burst1, kick])
        pm, lip, burst = pm1, lip1, burst1
        y.append(o)
    json.dump({"params": params, "E": [E.real.tolist(), E.imag.tolist()], "r": [r.real.tolist(), r.imag.tolist()],
               "lips": [lr.tolist(), lq.tolist(), ls.tolist(), lc.tolist()], "dec": dec.tolist(),
               "filters": [[a.tolist(), b.tolist()] for a, b in keep], "change": {str(k): v for k, v in change.items()},
               "ctl": ctl, "noise_n": len(noise), "out": np.concatenate(y).tolist(),
               "reed": reed_cases(lib, sr, np.zeros(1), dec)}, open(out, "w"))


if __name__ == "__main__":
    main(sys.argv[1])
