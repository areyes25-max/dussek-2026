"""The reed's fits (usage: reed_fit.py pressures|tune|starts|radiation|breath|levels [key ...]; the steps' own
commands, BRASS_PROJECT set), in the chain's order.

Held notes (2.5 s, judged over 1.8-2.3 s, the breath off): a crescendo lags its own threshold and jumps, where a held
note grows smoothly out of a near-sine. Per key the held threshold (mouth pressure re the reed's closing pressure) by
bisection, then a grid above it; each dynamics takes the point whose 2nd to 7th harmonics (re the fundamental) lie
closest to its recording's, pp and ff also their level against mf, the three as one path over the keys of a register
(a key's own best put one line bright, dark, bright). A point more than 60 c off its register is out, and so is one
under the key's first that keeps its level (just over the threshold a note dies slowly). The barrel then
moves until the mf note plays in tune; the barrel moves the threshold, so a point that no longer sounds on it moves up
its grid to the first that does.

The rest are judged on the live player (live.py: the reference the engine is held to), one key from rest at its
dynamics' air, tongued at the key as a player plays it: the tongue's push per key and dynamics until the note rises
as fast as its recording; the radiation's correction above 2 kHz from the harmonics' peaks at mf; the breath's level
per dynamics and its shape over the octave bands from the floor between the harmonics, then per key and per key and
dynamics on lines through the recordings'; the output's level per key and dynamics on lines through the recordings'.
A step judges its notes without what later steps fit (the radiation without the breath or a fitted push, the starts
without the breath, with the radiation's correction): a build is the same from the same inputs.

`pressures`: fit.pressures {"<midi>": {"threshold", "pp", "mf", "ff"}} (absolute, re the closing pressure); the
recordings' measures in cache.reed_recording, the grids in cache.reed_grid. `tune`: fit.tuning {"<midi>": barrel, m},
and fit.pressures amended where a point fell silent. A note that is silent where a seed or a level is fitted to it
raises. `starts`: fit.seeds {"<midi>_<dyn>": m/s}. `radiation`: fit.radiation {"<midi>": [dB at 2828 Hz, at 5657 Hz]}.
`breath`: fit.breath {"level": {dyn: share of the flow}, "shape": [dB at live.NOISE_HZ], "key": {"<midi>": dB},
"key_dyn": {"<midi>_<dyn>": dB}} (without the ear's trim, which the tables add). `levels`: fit.level {"open":
{"<midi>": [dB at live.LEVEL_X], "<midi>_<dyn>": 0}}. With keys given, those are fitted again and the others kept."""
import multiprocessing
import os
import sys
from concurrent.futures import ProcessPoolExecutor

import numpy as np

import reed

DYNS = ("pp", "mf", "ff")
DUR = 2.5
ABOVE = (0.002, 0.005, 0.01, 0.015, 0.02, 0.03, 0.04, 0.06, 0.08, 0.11, 0.16, 0.22, 0.3, 0.38, 0.46, 0.55)
WEIGHT = {1: 0.7, 2: 1.0, 4: 1.0, 6: 0.6}  # harmonic index (0 the fundamental) -> weight
OUT_OF_REGISTER = 60.0  # c
STEP = 3.0  # dB of cost per 0.05 of pressure change between neighbors
LEVEL_WEIGHT = 0.7  # dB of cost per dB the level re mf is off (pp and ff)
CAP = 15.0  # dB: what a level costs at most
PASSES = 3  # the barrel's rounds
RISE = (25.0, 250.0)  # ms: what the starts match of the recordings' rise
SEEDS = (0.05, 60.0)  # m/s: the push's bracket, bisected in log
SEED_ROUNDS = 8
HOLDS_DB = 10.0  # dB: a push whose note stays this far under what the bracket's top push holds is too small
RAD_CAP = 20.0  # dB: the radiation's correction at most (the spike's 15 held the clarion's 4-8 kHz 3 dB under: its
# thresholds read about 0.011 high, so its pressures played brighter there)
BREATH_ROUNDS = 2
BREATH_CAP = 12.0  # dB: the breath's shape per band at most
START_S = 0.7  # s: a start's render (its rise judged against 250-600 ms)
HELD_S = 1.4  # s: a held note's render
JUDGE = {"bands": (0.4, 1.3), "floor": (0.4, 1.3), "level": (0.4, 1.0)}  # s after the key: what each measure reads
# the mf breath per key over the level's window: the spike's trims judged both on one render (pp and ff its breath_dyn)
MF_FLOOR = (0.4, 1.0)


def brightness_error(h, rh):
    """The weighted rms difference of the harmonics' levels re the fundamental, dB (-70 dB floor)."""
    e, w = 0.0, 0.0
    for i, wt in WEIGHT.items():
        if i < len(h) and h[i] is not None and rh[i] is not None:
            a = max(h[i] - h[0], -70.0)
            b = max(rh[i] - rh[0], -70.0)
            e += wt * (a - b) ** 2
            w += wt
    return float(np.sqrt(e / w)) if w else float("inf")


def register(m, first=69):
    """0: chalumeau, 1: clarion (the keys from `first` up)."""
    return int(m >= first)


def path(costs, offsets, step=STEP):
    """The cheapest choice of one candidate per key: the keys' costs (arrays) plus `step` per 0.05 of change in offset
    between neighbors; the picks' indices (Viterbi)."""
    acc, back = np.asarray(costs[0], float), []
    for i in range(1, len(costs)):
        jump = step * np.abs(np.asarray(offsets[i], float)[:, None] - np.asarray(offsets[i - 1], float)[None, :]) / 0.05
        tot = acc[None, :] + jump
        back.append(tot.argmin(axis=1))
        acc = np.asarray(costs[i], float) + tot.min(axis=1)
    pick = [int(acc.argmin())]
    for b in reversed(back):
        pick.append(int(b[pick[-1]]))
    return pick[::-1]


def select(grids, rec, first=69, step=STEP, level_weight=LEVEL_WEIGHT, cap=CAP):
    """{"<midi>": {"threshold", "pp", "mf", "ff"}} from the grids ({"<midi>": {"threshold", "grid": [[g, {level_db,
    cents, harm_db}], ...]}}) and the recordings' measures ({"<midi>_<dyn>": {level_db, harm_db}}). Per register and
    dynamics one path over the keys; mf first, pp and ff against the level of the mf point chosen."""
    out = {m: {"threshold": g["threshold"]} for m, g in grids.items()}
    for reg in (0, 1):
        for d in ("mf", "pp", "ff"):
            ks = sorted((int(m) for m in grids if register(int(m), first) == reg and f"{m}_{d}" in rec
                         and (d == "mf" or f"{m}_mf" in rec) and (d == "mf" or "mf" in out[m])))
            if not ks:
                continue
            costs, offs = [], []
            for k in ks:
                g = grids[str(k)]
                r = rec[f"{k}_{d}"]
                c = [brightness_error(q["harm_db"], r["harm_db"]) for _, q in g["grid"]]
                if d != "mf":
                    mf = next(q for x, q in g["grid"] if abs(x - out[str(k)]["mf"]) < 1e-9)
                    want = r["level_db"] - rec[f"{k}_mf"]["level_db"]
                    c = [ci + level_weight * min(cap, abs(q["level_db"] - mf["level_db"] - want))
                         for ci, (_, q) in zip(c, g["grid"])]
                costs.append(c)
                offs.append([x - g["threshold"] for x, _ in g["grid"]])
            for k, j in zip(ks, path(costs, offs, step)):
                out[str(k)][d] = float(grids[str(k)]["grid"][j][0])
    return out


# ---- the recordings ----

def measure(x, sr, m):
    """A recorded note's steady middle (reed.sustained): its level (dB re full scale, power), its pitch off the key (c)
    and its harmonics."""
    s = reed.sustained(x, sr)
    f0 = reed.pitch(s, sr, reed.f_of(m))
    return {"level_db": float(10 * np.log10(np.mean(s ** 2) + 1e-20)), "cents": float(1200 * np.log2(f0 / reed.f_of(m))),
            "harm_db": reed.harmonics(s, sr, m)}


def takes():
    """{(midi, dyn): (samples, rate)}: the project's main sources' cut notes, mono."""
    import soundfile as sf
    import mapping
    import project as P
    import spec
    out = {}
    for sid in P.source_ids(spec.NAME):
        root = P.data_root(sid)
        for it in mapping.items(P.source(sid), root):
            x, sr = sf.read(it["path"], always_2d=True)
            out[it["note"], it["dynamic"]] = (x.mean(axis=1), sr)
    return out


def recorded():
    """{"<midi>_<dyn>": measure} of the project's main sources' cut notes."""
    return {f"{m}_{d}": measure(x, sr, m) for (m, d), (x, sr) in takes().items()}


def heard(what, x, sr, m):
    """A recording's measure: its start's rise (ms, clamped to RISE), or its steady middle's octave bands, breath floor
    or A-weighted level."""
    if what == "rise":
        return float(np.clip(reed.rise(x, sr, reed.start_of(x, sr)), *RISE))
    return judge(what, reed.sustained(x, sr), sr, m)


def judge(what, s, sr, m):
    return {"bands": reed.bands, "floor": reed.floor, "level": lambda s, sr, m: reed.level_a(s, sr)}[what](s, sr, m)


# ---- the renders ----

SILENT_DB = -60.0  # dB re full scale (power, the DC left out): a note under it does not sound


def ac_db(s):
    """The power of what sounds, dB re full scale: the DC left out. A reed that does not oscillate still lets its flow
    through the radiation's filter (its first taps sum to about 0, not 0): the clarion's wide reed 50 dB under full
    scale, which counted as a note and put every clarion threshold at the bisection's floor."""
    return float(10 * np.log10(np.var(s) + 1e-30))


def held(job):
    """(key, gamma) -> the held note's level, pitch and harmonics (None: silent or no pitch). Rendered on the default
    barrel unless a barrel is given: the fit places the pressures before the tuning."""
    m, g, *rest = job
    x = reed.steady(m, g, dur=DUR, breath=0.0, extra=rest[0] if rest else None, fitted=False)
    s = x[int(1.8 * reed.SR):int(2.3 * reed.SR)]
    lv = ac_db(s)
    if lv < SILENT_DB:
        return job, None
    f0 = reed.pitch(s, reed.SR, reed.f_of(m))
    if not np.isfinite(f0):
        return job, None
    return job, {"level_db": float(lv), "cents": float(1200 * np.log2(f0 / reed.f_of(m))), "harm_db": reed.harmonics(s, reed.SR, m)}


MARGIN_MAX = 0.06  # over the threshold: the points checked for holding (far over it a note holds)
HOLD_DB = 1.0  # dB: a held note's level across its judged window and later on, at most this apart where it holds
LATER = (2.5, 3.0)  # s: the later window a held note's level is checked over


def holds(job):
    """(key, gamma) -> whether the held note keeps its level: over 1.8-2.05 and 2.05-2.3 s, and over LATER in a longer
    render (the same render, run on), within HOLD_DB. Just over its threshold a note still dies slowly (2-3 dB across
    the window at +0.002: E5's pp, a level anchor of 13 dB to make up for it)."""
    m, g = job
    x = reed.steady(m, g, dur=LATER[1], breath=0.0, fitted=False)
    a, b, c = (ac_db(x[int(t0 * reed.SR):int(t1 * reed.SR)]) for t0, t1 in ((1.8, 2.05), (2.05, 2.3), LATER))
    return job, bool(abs(b - a) <= HOLD_DB and abs(c - (a + b) / 2) <= HOLD_DB)


def margin(grid, holding):
    """A key's grid ([[gamma, measure], ...], rising) from its first point that holds (`holding`: gamma -> bool): pp
    takes the smallest point over the threshold that keeps its level, never one the level's anchor has to make up for."""
    first = next((i for i, (g, _) in enumerate(grid) if holding[g]), len(grid))
    return grid[first:]


def sounds(r):
    return r is not None and abs(r["cents"]) < OUT_OF_REGISTER


def thresholds(ex, keys, lo=0.30, hi=0.75, levels=3, per=8):
    """The lowest held pressure that sounds in its register, per key: each level splits the bracket into per + 1."""
    br = {m: (lo, hi) for m in keys}
    for _ in range(levels):
        gs = {m: np.linspace(*br[m], per + 2) for m in keys}
        res = dict(ex.map(held, [(m, float(g)) for m in keys for g in gs[m][1:-1]]))
        for m in keys:
            up = [i for i in range(1, per + 1) if sounds(res[(m, float(gs[m][i]))])]
            i = up[0] if up else per + 1
            br[m] = (gs[m][i - 1], gs[m][min(i, per + 1)])
    return {m: float(br[m][1]) for m in keys}


def pool():
    """Workers forked from a process that has loaded the render's modules and the bore's layout."""
    import lattice
    import live
    lattice.layout()
    live.lib()
    return ProcessPoolExecutor(os.cpu_count(), mp_context=multiprocessing.get_context("fork"))


def fitted_keys(keys, dyns=DYNS):
    """The keys the pressures' fit placed (all of `dyns`), those of `keys` if given."""
    import spec
    fit = spec.load("pressures", {})
    return [int(m) for m in sorted(fit, key=int) if all(d in fit[m] for d in dyns) and (not keys or int(m) in keys)]


def last_build(name, keys, inner=None):
    """A fit key's entries for the keys not refitted ("<midi>" or "<midi>_<dyn>"), from the last build; none when every
    key is refitted (`keys` None). `inner`: the table inside the key that holds them."""
    import spec
    if not keys:
        return {}
    old = spec.load(name, {})
    old = old.get(inner, {}) if inner else old
    return {k: v for k, v in old.items() if int(k.split("_")[0]) not in keys}


def pressures(keys=None):
    import spec
    S = spec.S
    first = S.get("lattice", {}).get("clarion", [69])[0]
    keys = list(keys or range(S["low"], S["high"] + 1))
    rec = recorded()
    spec.save_cache("reed_recording", rec)
    kept = spec.load_cache("reed_grid", {}) if keys != list(range(S["low"], S["high"] + 1)) else {}
    grids = {m: g for m, g in kept.items() if int(m) not in keys}
    with pool() as ex:
        thr = thresholds(ex, keys)
        print("thresholds:", " ".join(f"{m}:{thr[m]:.3f}" for m in keys), flush=True)
        res = dict(ex.map(held, [(m, thr[m] + d) for m in keys for d in ABOVE]))
        low = [(m, thr[m] + d) for m in keys for d in ABOVE if d <= MARGIN_MAX and sounds(res[(m, thr[m] + d)])]
        holding = dict(ex.map(holds, low))
    for m in keys:
        ok = [[thr[m] + d, res[(m, thr[m] + d)]] for d in ABOVE if sounds(res[(m, thr[m] + d)])]
        held_grid = margin(ok, {g: holding.get((m, g), True) for g, _ in ok})
        if len(held_grid) < len(ok):
            print(f"{m}: from +{held_grid[0][0] - thr[m]:.3f} (under it the note does not hold)" if held_grid else f"{m}: nothing holds", flush=True)
        if held_grid:
            grids[str(m)] = {"threshold": thr[m], "grid": held_grid}
        else:
            print(m, "nothing plays")
    spec.save_cache("reed_grid", grids)
    chosen = select(grids, rec, first)
    for m, row in chosen.items():
        print(m, f"thr {row['threshold']:.3f}", " ".join(f"{d} +{row[d] - row['threshold']:.3f}" for d in DYNS if d in row), flush=True)
    spec.save("pressures", {k: chosen[k] for k in sorted(chosen, key=int)})


def retune(extra, cents, m, clarion):
    """The barrel after a note played `cents` off: a length dL at the reed's end moves a note by about -dL / L, L the
    bore's acoustic length for the played mode's quarter wave (three of them on a clarion note)."""
    from bore import C
    L = (3 if clarion else 1) * C / (4 * reed.f_of(m))
    return extra + L * (2 ** (cents / 1200) - 1)


def tune(keys=None):
    import spec
    fit = spec.load("pressures", {})
    keys = [int(k) for k in (keys or fit) if str(k) in fit and "mf" in fit[str(k)]]
    clar = lambda m: reed.note(m, fitted=False).register
    barrel = {m: reed.note(m, fitted=False).extra for m in keys}
    for rnd in range(PASSES):
        with pool() as ex:
            res = dict(ex.map(held, [(m, fit[str(m)]["mf"], barrel[m]) for m in keys]))
        cents = {m: (res[(m, fit[str(m)]["mf"], barrel[m])] or {}).get("cents", np.nan) for m in keys}
        print(f"round {rnd}:", " ".join(f"{m}:{cents[m]:+.0f}" for m in keys), flush=True)
        for m in keys:
            if np.isfinite(cents[m]):
                barrel[m] = retune(barrel[m], cents[m], m, clar(m))
    kept = {k: v for k, v in spec.load("tuning", {}).items() if int(k) not in keys}
    kept.update({str(m): float(b) for m, b in barrel.items()})
    spec.save("tuning", {k: kept[k] for k in sorted(kept, key=int)})
    # the barrel moves a key's threshold: a pressure placed just over it on the untuned barrel fell silent (a pp died)
    grids = spec.load_cache("reed_grid", {})
    points = sorted({(m, fit[str(m)][d]) for m in keys for d in DYNS if d in fit[str(m)]})
    with pool() as ex:
        plays = {(m, g): sounds(r) for (m, g, _), r in ex.map(held, [(m, g, barrel[m]) for m, g in points])}
    above = sorted({(m, float(g)) for m, g0 in points if not plays[m, g0]
                    for g, _ in grids.get(str(m), {}).get("grid", []) if g > g0 and (m, float(g)) not in plays})
    with pool() as ex:
        plays.update({(m, g): sounds(r) for (m, g, _), r in ex.map(held, [(m, g, barrel[m]) for m, g in above])})
    lifted, moves = lift({str(m): fit[str(m)] for m in keys}, grids, plays)
    for m, d, a, b in moves:
        print(f"lifted {m} {d}: {a:.3f} -> {b:.3f} (silent on the tuned barrel)", flush=True)
    if moves:
        spec.save("pressures", {**fit, **lifted})


def lift(pressures, grids, plays):
    """The pressures with each (key, dynamics) whose point does not sound on the tuned barrel moved up its key's grid
    to the first point that does: (pressures, [(key, dyn, old, new)]). `plays`: {(key, gamma): sounds} on the tuned
    barrel, for the points and the grid's points above the silent ones."""
    out, moves = {}, []
    for k, row in pressures.items():
        row = dict(row)
        for d in DYNS:
            g0 = row.get(d)
            if g0 is None or plays[int(k), g0]:
                continue
            up = [g for g, _ in grids.get(k, {}).get("grid", []) if g > g0 and plays.get((int(k), float(g)))]
            if not up:
                raise RuntimeError(f"key {k} at {d}: nothing on its grid sounds on the tuned barrel")
            row[d] = float(up[0])
            moves.append((int(k), d, g0, row[d]))
        out[k] = row
    return out, moves


# ---- the live player's renders: one key from rest at its dynamics' air, tongued at the key ----

def tables(radiation=True):
    """The reed's tables, loaded in this process for the workers to fork with; the output's level left out (live's own
    fit.level: the measures read the player's sound). `radiation` False: every key's filter without its correction
    (a step before the radiation's, or the radiation's own, judges the bore's radiation alone)."""
    import live
    import rt_tables
    t = rt_tables.load("open")
    live.LEVEL.clear()
    if not radiation:
        with pool() as ex:
            hs = dict(ex.map(plain_filter, sorted(t["notes"])))
        for m, n in t["notes"].items():
            n["zh"] = hs[m]
    return t


def plain_filter(m):
    return m, reed.filters(reed.note(m))


def play(job):
    """(key, dynamics, {what: value}, dur s) -> the key pressed at 0 s at the dynamics' air (the virtual player off),
    through the live player, its table's entries for the key replaced by `what`'s: "seed" (m/s), "breath" (the noise's
    share of the flow), "shape" (the breath's dB at live.NOISE_HZ); the table as it was afterwards."""
    import live
    import reed_tables
    import rt_tables
    m, d, over, dur = job
    n = rt_tables.load("open")["notes"][m]
    was, shape = dict(n), live.noise_shape
    try:
        if "seed" in over:
            n["seed"] = dict.fromkeys(DYNS, over["seed"])
        if "breath" in over:
            n["breath"] = dict.fromkeys(DYNS, over["breath"] / reed.NOISE_STD)
        if "shape" in over:
            live.noise_shape = lambda: list(over["shape"])
        air = 127 * reed_tables.X[DYNS.index(d)]
        return live.render_events([(0.0, "cc", live.CONTROLS["air"], air), (0.0, "on", m, 80)], dur, virtual=False)
    finally:
        n.clear()
        n.update(was)
        live.noise_shape = shape


def sounding(x, m, d):
    """Raises if the steady part x of key m at dynamics d is silent: no seed or level is fitted to a note that dies."""
    lv = ac_db(x)
    if lv < SILENT_DB:
        raise RuntimeError(f"key {m} at {d} is silent ({lv:.0f} dB): nothing to fit it to")


def judged(job):
    """(key, dynamics, {what: value}, measure) -> the measure of play's note: "rise" (ms from the key), "held" (its
    start's render: raises if silent over the rise's 250-600 ms), "start" (the rise and the level over 250-600 ms, dB
    without the DC), "bands", "floor" or "level" (of its steady part,
    JUDGE, mf's floor MF_FLOOR; the level raises if silent)."""
    m, d, over, what = job
    if what in ("rise", "held", "start"):
        x = play((m, d, over, START_S))
        held_part = x[int(0.25 * reed.SR):int(0.6 * reed.SR)]
        if what == "held":
            return sounding(held_part, m, d)
        if what == "start":
            return reed.rise(x, reed.SR, 0.0), ac_db(held_part)
        return reed.rise(x, reed.SR, 0.0)
    x = play((m, d, over, HELD_S))
    a, b = MF_FLOOR if (what, d) == ("floor", "mf") else JUDGE[what]
    s = x[int(a * reed.SR):int(b * reed.SR)]
    if what == "level":
        sounding(s, m, d)
    return judge(what, s, reed.SR, m)


def first_clarion():
    import spec
    return spec.S.get("lattice", {}).get("clarion", [69])[0]


# ---- the starts ----

def starts(keys=None):
    """The tongue's push per key and dynamics: the note from the key rises to 3 dB under its level as fast as its
    recording from its own start (clamped to RISE), by bisection in log over SEEDS; the breath off, the radiation's
    correction on (fitted before: through the note's own Z H it lifts a start's bright first milliseconds by up to
    20 dB on the clarion, which read as a start 80-110 ms early).
    A push whose note stays HOLDS_DB under what the bracket's top push holds is too small: a note just over its
    threshold holds only when started near its level, and pushed less it fades, fast to its weak level."""
    import spec
    fit = spec.load("pressures", {})
    rec = takes()
    jobs = [(m, d) for m in fitted_keys(keys, ()) for d in DYNS if d in fit[str(m)] and (m, d) in rec]
    want = {k: heard("rise", *rec[k], k[0]) for k in jobs}
    tables()
    with pool() as ex:  # what each note holds when pushed hardest: a push it stays far under did not start it
        top = {k: lv for k, (_, lv) in zip(jobs, ex.map(judged, [(m, d, {"seed": SEEDS[1], "breath": 0.0}, "start") for m, d in jobs]))}
    br = {k: (np.log(SEEDS[0]), np.log(SEEDS[1])) for k in jobs}
    for rnd in range(SEED_ROUNDS):
        amp = {k: float(np.exp(sum(br[k]) / 2)) for k in jobs}
        with pool() as ex:
            got = list(ex.map(judged, [(m, d, {"seed": amp[m, d], "breath": 0.0}, "start") for m, d in jobs]))
        for k, (ms, lv) in zip(jobs, got):
            slow = not np.isfinite(ms) or ms > want[k] or lv < top[k] - HOLDS_DB
            br[k] = (np.log(amp[k]), br[k][1]) if slow else (br[k][0], np.log(amp[k]))
        print(f"round {rnd}:", " ".join(f"{m}_{d}:{ms:.0f}/{want[m, d]:.0f}" + ("(weak)" if lv < top[m, d] - HOLDS_DB else "")
                                        for (m, d), (ms, lv) in zip(jobs, got)), flush=True)
    final = {k: float(np.exp(sum(br[k]) / 2)) for k in jobs}
    with pool() as ex:  # a note that does not sound at its push takes no seed
        list(ex.map(judged, [(m, d, {"seed": final[m, d], "breath": 0.0}, "held") for m, d in jobs]))
    seeds = last_build("seeds", keys)
    seeds.update({f"{m}_{d}": final[m, d] for m, d in jobs})
    spec.save("seeds", {k: seeds[k] for k in sorted(seeds, key=lambda k: (int(k.split("_")[0]), k))})


# ---- the radiation ----

def radiation_lines(keys, diff, first, cap=RAD_CAP):
    """{"<midi>": [dB at 2828 Hz, at 5657 Hz]}: per register a line over the keys through the recordings' harmonics'
    peaks less the model's (diff: per key, the 2-4 and 4-8 kHz bands), capped."""
    keys, diff = np.asarray(keys), np.asarray(diff, float)
    out = {}
    for reg in (0, 1):
        sel = np.array([register(int(m), first) == reg for m in keys], bool)
        if not sel.any():
            continue
        lines = [reed.line_fit(keys[sel], diff[sel, i]) for i in range(2)]
        for j, m in enumerate(keys[sel]):
            out[str(int(m))] = [float(np.clip(lines[i][j], -cap, cap)) for i in range(2)]
    return out


def radiation(keys=None):
    """The radiation's correction per fingering above 2 kHz (the spike's rad_fit): the bore radiates every opening as a
    monopole, the holes' and the bell's beaming of the highs left out. At mf (the chalumeau's pp lacks the top for its
    timbre, not its radiation), from the bore's own radiation, the breath off (the harmonics' peaks), pushed with
    reed.SEED: it comes before the starts."""
    import spec
    rec = takes()
    ks = [m for m in fitted_keys(keys, ("mf",)) if (m, "mf") in rec]
    tables(radiation=False)
    with pool() as ex:
        mod = list(ex.map(judged, [(m, "mf", {"breath": 0.0, "seed": reed.SEED}, "bands") for m in ks]))  # no fitted push yet
    diff = [[heard("bands", *rec[m, "mf"], m)[i][0] - b[i][0] for i in (1, 2)] for m, b in zip(ks, mod)]
    out = last_build("radiation", keys)
    out.update(radiation_lines(ks, diff, first_clarion()))
    print(" ".join(f"{m}:{v[0]:+.0f}/{v[1]:+.0f}" for m, v in out.items()), flush=True)
    spec.save("radiation", {k: out[k] for k in sorted(out, key=int)})


# ---- the breath ----

def along(keys, rec, mod):
    """Per key what puts the model's values on a line through the recordings' (reed.line_fit), its median 0: the
    spread over the keys, not the amount."""
    t = reed.line_fit(keys, rec) - np.asarray(mod, float)
    return t - np.median(t)


def breath_round(b, med, cap=BREATH_CAP):
    """The breath's level and shape after one round, from the floors between the harmonics per octave band (model less
    recording, the median over the keys) per dynamics: 2-16 kHz carries the level, the rest of each band the shape
    (the dynamics' mean), at live.NOISE_HZ (its 750 Hz as its 1414: the shape flat beyond the bands)."""
    lvl = {d: float(np.nanmean(med[d][1:])) for d in med}
    shape = np.nanmean([np.asarray(med[d], float) - lvl[d] for d in med], axis=0)
    eq = np.clip(np.asarray(b["shape"][1:], float) - shape, -cap, cap)
    return {**b, "level": {d: float(b["level"][d] * 10 ** (-lvl[d] / 20)) for d in med},
            "shape": [float(eq[0]), *(float(e) for e in eq)]}


def breath(keys=None):
    """The breath (the spike's breath_fit, trims' breath and breath_dyn): its level per dynamics and its shape over the
    octave bands on the recordings' floor between the harmonics (BREATH_ROUNDS rounds, without the ear's trim: the fit
    matches the floor); then per key at mf and per key at pp and ff re that, on lines through the recordings' floor
    (2-10 kHz re the note), judged as played (the trim on: the floor holds the harmonics' unsteadiness as well as the
    breath, so the trim moves it by less than its dB)."""
    import reed_tables
    import spec
    rec = takes()
    ks = fitted_keys(keys)
    jobs = [(m, d) for d in DYNS for m in ks if (m, d) in rec]
    floors = {k: [f for _, f in heard("bands", *rec[k], k[0])] for k in jobs}
    b = {"level": dict(reed_tables.BREATH), "shape": [0.0] * 5, "key": {}, "key_dyn": {}}
    tables()

    def floor_of(m, d, what, trim):
        return (m, d, {"breath": reed_tables.breath(m, d, b, trim), "shape": b["shape"]}, what)

    for rnd in range(BREATH_ROUNDS):
        with pool() as ex:
            mod = dict(zip(jobs, ex.map(judged, [floor_of(m, d, "bands", False) for m, d in jobs])))
        med = {d: np.nanmedian([[f - r for (_, f), r in zip(mod[m, dd], floors[m, dd])] for m, dd in jobs if dd == d], axis=0)
               for d in DYNS if any(dd == d for _, dd in jobs)}
        b = breath_round(b, med)
        print(f"round {rnd}: model - recording per band:", "; ".join(f"{d} " + " ".join(f"{v:+.1f}" for v in med[d]) for d in med),
              "| level", {d: round(v, 5) for d, v in b["level"].items()}, "shape", [round(v, 1) for v in b["shape"]], flush=True)
    rec_f = {k: heard("floor", *rec[k], k[0]) for k in jobs}
    mf = [m for m, d in jobs if d == "mf"]
    with pool() as ex:
        mod = list(ex.map(judged, [floor_of(m, "mf", "floor", True) for m in mf]))
    b["key"] = {str(m): float(v) for m, v in zip(mf, along(mf, [rec_f[m, "mf"] for m in mf], mod))}
    b["key_dyn"] = {}
    for d in ("pp", "ff"):
        ds = [m for m, dd in jobs if dd == d]
        with pool() as ex:
            mod = list(ex.map(judged, [floor_of(m, d, "floor", True) for m in ds]))
        b["key_dyn"].update({f"{m}_{d}": float(v) for m, v in zip(ds, along(ds, [rec_f[m, d] for m in ds], mod))})
    b["key"] = {**last_build("breath", keys, "key"), **b["key"]}
    b["key_dyn"] = {**last_build("breath", keys, "key_dyn"), **b["key_dyn"]}
    print("key:", " ".join(f"{m}:{v:+.1f}" for m, v in b["key"].items()), flush=True)
    print("key_dyn:", " ".join(f"{k}:{v:+.1f}" for k, v in b["key_dyn"].items()), flush=True)
    b["key"] = {k: b["key"][k] for k in sorted(b["key"], key=int)}
    b["key_dyn"] = {k: b["key_dyn"][k] for k in sorted(b["key_dyn"], key=lambda k: (int(k.split("_")[0]), k))}
    spec.save("breath", b)


# ---- the levels ----

def level_table(keys, rec, mod):
    """fit.level's open table from the recordings' and the model's A-weighted levels per dynamics ({dyn: [dB per key]}):
    mf on a line through the recordings' (the rise with pitch kept), the median 0; pp and ff that far from mf as a line
    through the recordings' steps re mf (each take's own jumped 6-30 dB between neighbors), less the model's own step.
    At the air's anchors (live.LEVEL_X: 0.315, 0.55, 0.9, 1.0) pp's (flat below 0.315: pp plays at 0.2), mf's, ff's and
    ff's again (nothing rises over ff). One table at every dynamics: no table's step to close."""
    mf = along(keys, rec["mf"], mod["mf"])
    step = {d: reed.line_fit(keys, np.subtract(rec[d], rec["mf"])) - np.subtract(mod[d], mod["mf"]) for d in ("pp", "ff")}
    t = {}
    for i, m in enumerate(keys):
        a = float(mf[i])
        pp, ff = a + float(step["pp"][i]), a + float(step["ff"][i])
        t[str(m)] = [pp, a, ff, ff]
        t[f"{m}_pp"], t[f"{m}_ff"] = 0.0, 0.0
    return {"open": t}


def levels(keys=None):
    """The output's level per key and dynamics (the spike's trims and dyn_trims), the A-weighted levels of the notes as
    played (every earlier fit in the tables) through the live player at the dynamics' airs, without a level: the
    output is the note's times the level's gain, so the anchors are the trims."""
    import spec
    rec = takes()
    ks = [m for m in fitted_keys(keys) if all((m, d) in rec for d in DYNS)]
    tables()
    jobs = [(m, d) for d in DYNS for m in ks]
    with pool() as ex:
        got = dict(zip(jobs, ex.map(judged, [(m, d, {}, "level") for m, d in jobs])))
    t = level_table(ks, {d: [heard("level", *rec[m, d], m) for m in ks] for d in DYNS},
                    {d: [got[m, d] for m in ks] for d in DYNS})["open"]
    print(" ".join(f"{m}:{t[str(m)][0]:+.1f}/{t[str(m)][1]:+.1f}/{t[str(m)][2]:+.1f}" for m in ks), flush=True)
    t = {**last_build("level", keys, "open"), **t}
    spec.save("level", {"open": {k: t[k] for k in sorted(t, key=lambda k: (int(k.split("_")[0]), k))}})


if __name__ == "__main__":
    what, keys = sys.argv[1], [int(a) for a in sys.argv[2:]] or None
    {"pressures": pressures, "tune": tune, "starts": starts, "radiation": radiation, "breath": breath,
     "levels": levels}[what](keys)
