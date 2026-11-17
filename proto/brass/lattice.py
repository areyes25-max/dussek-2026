"""A reed instrument's bore: a cylinder with a lattice of tone holes, a register hole and a small bell.

Elements from the reed's end: ("pipe", radius, length) and ("hole", radius, chimney, open). The pipes use the
brass bore's lossy slices (bore.py), the holes a T-network of a shunt and two series halves (Dalmont,
Kergomard: the open hole's inertance with its radiation, the closed one's chimney volume). The holes are placed
from the bell up: opening hole k, with every one below it open, puts the bore's first peak on the k-th semitone over
the lowest key. The register range fingers the note a twelfth below with the register hole open and plays the
second peak. Every size is the project's ([brass.lattice]).
"""
from functools import lru_cache

import numpy as np

import bore as B
import spec

RHO, C, ETA = B.RHO, B.C, B.ETA
L_ = spec.S["lattice"]
A = L_["radius"]                  # the bore's radius
MOUTH = L_["mouth"]               # the mouthpiece and barrel, as bore (the mouthpiece's volume as a length of cylinder)
BELL = tuple(L_["bell"][:2])      # the bell's length and mouth radius
FLARE = L_["bell"][2]             # the bell's Bessel flare
REG = tuple(L_["register"])       # the register hole: from the reed, radius, chimney (narrow and long)
LOW = spec.S["low"]               # the lowest key
NCHAL = L_["holes"]               # the lattice's notes: the lowest key up, one more than the tone holes
HOLE_R = tuple(L_["hole_radius"])  # the largest and the smallest tone hole's radius
CHIMNEY = L_["chimney"]
CLARION = range(L_["clarion"][0], L_["clarion"][1] + 1)  # the register hole's keys: a twelfth over the fingering
FMAX = spec.S["fmax"]


def hole_size(k):
    """Radius and chimney of hole k (1 the lowest): wide low, narrower up the joint."""
    t = (k - 1) / max(1, NCHAL - 2)
    return HOLE_R[0] - (HOLE_R[0] - HOLE_R[1]) * t, CHIMNEY


def pipe_matrix(f, a, L):
    w = 2 * np.pi * f
    S = np.pi * a * a
    rv = a * np.sqrt(RHO * w / ETA)
    d = 1.0433 / rv
    k = (w / C) * (1 + d) - 1j * (w / C) * d
    Zc = RHO * C / S * (1 + d - 1j * d)
    c, s = np.cos(k * L), np.sin(k * L)
    return c, 1j * Zc * s, 1j * s / Zc, c


def hole_matrix(f, b, t, is_open, a=A):
    """A tone hole on a pipe of radius a: shunt Zs, series Za split in two halves."""
    k = 2 * np.pi * f / C
    Zh = RHO * C / (np.pi * b * b)
    d = b / a
    rv = b * np.sqrt(RHO * 2 * np.pi * f / ETA)
    loss = 1.0433 / rv  # the chimney's wall losses, as a pipe's
    if is_open:
        ti = b * (0.82 - 0.193 * d - 1.09 * d**2 + 1.27 * d**3 - 0.71 * d**4)  # the inner end
        to = 0.6133 * b  # the outer end, unflanged-ish
        te = t + ti + to
        Zs = Zh * (1j * k * te * (1 + loss) + k * te * loss + 0.25 * (k * b) ** 2)
        ta = -0.28 * b * d**2
    else:
        ti = b * (0.82 - 0.193 * d - 1.09 * d**2 + 1.27 * d**3 - 0.71 * d**4)
        Zs = -1j * Zh / np.tan(k * (t + ti))
        ta = -0.12 * b * d**2
    Za = 1j * Zh * k * ta
    # T: series Za/2, shunt Zs, series Za/2
    h = Za / 2
    return 1 + h / Zs, h + h * (1 + h / Zs), 1 / Zs, 1 + h / Zs


def chain(f, elems):
    A_, B_, C_, D_ = (np.ones_like(f, dtype=complex), np.zeros_like(f, dtype=complex),
                      np.zeros_like(f, dtype=complex), np.ones_like(f, dtype=complex))
    for e in elems:
        a2, b2, c2, d2 = pipe_matrix(f, e[1], e[2]) if e[0] == "pipe" else hole_matrix(f, *e[1:])
        A_, B_, C_, D_ = A_ * a2 + B_ * c2, A_ * b2 + B_ * d2, C_ * a2 + D_ * c2, C_ * b2 + D_ * d2
    return A_, B_, C_, D_


def bell_parts(n=40):
    x, r = B.bessel_bell(A, BELL[1], BELL[0], FLARE, n + 1)
    return [("pipe", float(0.5 * (r[i] + r[i + 1])), float(x[i + 1] - x[i])) for i in range(n)]


def build(length, holes, open_from, register=False, extra=0.0):
    """The bore's elements. length: from the reed to the bell's start; holes: their distances from the reed
    (lowest first); open_from: the index of the highest open hole + 1 (0: all closed), every hole below it open;
    extra: added before the first hole (the barrel pulled out)."""
    pts = [(REG[0], ("hole", REG[1], REG[2], register))]
    for i, x in enumerate(holes):
        b, t = hole_size(i + 1)
        pts.append((x, ("hole", b, t, i < open_from)))
    pts.sort(key=lambda p: p[0])
    elems, at = [("pipe", A, MOUTH + extra)], MOUTH
    for x, h in pts:
        if x > at:
            elems.append(("pipe", A, x - at))
            at = x
        elems.append(h)
    elems.append(("pipe", A, length - at))
    return elems + bell_parts()


def impedance(f, elems):
    """Input impedance, and the pressure at 1 m per unit mouthpiece pressure: every opening a monopole (the open
    holes and the bell, their path differences left out)."""
    w = 2 * np.pi * f
    Zr = B.radiation(f, BELL[1])
    A_, B_, C_, D_ = chain(f, elems)
    Zin = (A_ * Zr + B_) / (C_ * Zr + D_)
    # march from the bell to the reed with unit flow out of the bell: every hole's flow, then scale by p_in
    p, U = Zr * 1.0, np.ones_like(f, dtype=complex)
    radiated = U.copy()  # the bell's flow
    for e in reversed(elems):
        if e[0] == "pipe":
            a2, b2, c2, d2 = pipe_matrix(f, e[1], e[2])
            p, U = a2 * p + b2 * U, c2 * p + d2 * U
        else:
            b, t, is_open = e[1:]
            a2, b2, c2, d2 = hole_matrix(f, b, t, is_open)
            ph = p  # the pressure at the hole, before its downstream half: near enough for the hole's flow
            Zs = 1 / c2
            if is_open:
                radiated = radiated + ph / Zs
            p, U = a2 * p + b2 * U, c2 * p + d2 * U
    H = 1j * RHO * w / (4 * np.pi) * radiated / p
    return Zin, H


def peaks(f, Z, n=2):
    return [m[0] for m in B.modes(f, Z, fmax=f[-1] - 1, fmin=f[0] + 1)][:n]


@lru_cache(maxsize=None)
def layout():
    """(length, holes): the bore's length (reed to bell) for D3 with every hole shut, then each hole's place."""
    def first_peak(length, holes, k):
        ft = 440 * 2 ** ((LOW + k - 69) / 12)
        f = np.arange(0.55 * ft, 1.8 * ft, 0.25)
        Z, _ = impedance(f, build(length, holes, k))
        pk = peaks(f, Z, 1)
        return pk[0] if pk else 1e9  # none on the grid: the peak lies above it

    def solve(fun, target, a, b):
        fa = fun(a) - target
        for _ in range(40):
            m = 0.5 * (a + b)
            fm = fun(m) - target
            if np.sign(fm) == np.sign(fa):
                a, fa = m, fm
            else:
                b = m
        return 0.5 * (a + b)

    f_of = lambda m: 440 * 2 ** ((m - 69) / 12)
    holes = []
    length = 0.55
    for _pass in range(3):  # each pass sees the closed holes the last one placed
        prev = list(holes)
        length = solve(lambda L: -first_peak(L, prev, 0), -f_of(LOW), 0.40, 0.70)
        holes = []
        for k in range(1, NCHAL):
            later = prev[k:] if len(prev) >= NCHAL - 1 else []
            hi = holes[-1] if holes else length
            x = solve(lambda x: -first_peak(length, holes + [x] + later, k), -f_of(LOW + k), max(REG[0] + 0.02, hi - 0.09), hi - 0.004)
            holes.append(x)
    return length, tuple(holes)


class Note:
    """One fingering, as the brass player's Note: .modes for the reed's loop, .transfer() for the radiation."""

    def __init__(self, midi, extra=None, fgrid=None):
        if fgrid is None:
            fgrid = np.arange(50, FMAX, 0.5)
        self.midi = midi
        self.f = 440 * 2 ** ((midi - 69) / 12)
        length, holes = layout()
        if midi in CLARION:
            self.k, self.register, self.peak = midi - NCHAL - LOW, True, 2
        elif LOW <= midi < LOW + NCHAL:
            self.k, self.register, self.peak = midi - LOW, False, 1
        else:
            raise ValueError(f"no fingering for MIDI {midi}")
        self.length, self.holes = length, holes
        self.extra = 0.0
        if extra is None:  # the barrel tunes the played peak onto the note (the register's twelfths come out wide)
            for _ in range(3):
                fp = self.played_peak()
                self.extra += (MOUTH + length) * (fp / self.f - 1) * (0.5 if self.register else 1.0)
        else:
            self.extra = extra
        self.elems = build(length, holes, self.k, self.register, self.extra)
        Z, _ = impedance(fgrid, self.elems)
        self.modes = [m for m in B.modes(fgrid, Z, fmax=fgrid[-1] - 1, fmin=fgrid[0] + 1) if m[1] > 2]
        self._tr = None

    def played_peak(self):
        f = np.arange(0.4 * self.f / self.peak, 1.4 * self.f, 0.25)
        Z, _ = impedance(f, build(self.length, self.holes, self.k, self.register, self.extra))
        pk = peaks(f, Z, 3)
        near = min(pk, key=lambda p: abs(np.log(p / self.f)))
        return near

    def transfer(self, freqs):
        if self._tr is None:
            fg = np.concatenate([np.arange(1, 4000, 1.0), np.arange(4000, 22050, 4.0), np.arange(22050, 96001, 16.0)])
            fg = fg[fg >= 1]
            Z, H = impedance(fg, self.elems)
            self._tr = (fg, Z, H)
        fg, Z, H = self._tr
        ip = lambda X: np.interp(freqs, fg, X.real, left=0) + 1j * np.interp(freqs, fg, X.imag, left=0)
        return ip(Z), ip(H)
