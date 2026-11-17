"""The reed's report (usage: reed_report.py; the reed_report step's command, BRASS_PROJECT set): what a person checks
a rebuild by, the spec's accepted-state numbers measured on the live player with every fit in (the spike's check.py,
even.py, bands.py and dyn.py), in cache.report."""
import json

import numpy as np

import reed
from reed_fit import DYNS, HELD_S, heard, play, pool, register, first_clarion, sounding, takes

LINE = [55, 58, 62, 65, 67, 70, 74, 77, 79, 82, 79, 77, 74, 70, 67, 65, 62, 58, 55]  # G3 to B-flat 5 and down
NOTE_S, LAST_S = 0.32, 1.0  # s: each note, the last
OVERLAP_S = 0.02  # s: a slur's next key down before the last comes up
GAP_S = 0.035  # s: a tongued note's last key up before the next (a key in the release's fade is tongued)
HP_HZ = 6000.0  # the note changes' bursts are judged over this
REGISTERS = ("chalumeau", "clarion")


def phrase_events(tongued, cc, air):
    """(events, starts) of the line, the air set by controller `cc` to `air` (0-127) at the start: slurred (each key down
    OVERLAP_S before the last comes up) or tongued (each key up GAP_S before the next); the first note from rest."""
    starts = [i * NOTE_S for i in range(len(LINE))]
    ev = [(0.0, "cc", cc, air)]
    for i, (m, t) in enumerate(zip(LINE, starts)):
        if i:
            ev.append((t - GAP_S if tongued else t + OVERLAP_S, "off", LINE[i - 1]))
        ev.append((t, "on", m, 80))
    ev.append((starts[-1] + LAST_S, "off", LINE[-1]))
    return sorted(ev, key=lambda e: e[0]), starts


def phrase_measures(x, line, starts, ends, tongued=()):
    """Per note of a phrase (its key's start and end, s): its pitch (c, the period's), its A-weighted level and its
    breath (the floor between the harmonics, 2-10 kHz re the note; even.py's windows, ending 40 ms before the next note
    as the pitch's does), the bursts over 6 kHz at each
    change re the note's own steady band (check.py's), and the peaks of the tongued notes' starts re their sine peak
    (`tongued`: their indices)."""
    from scipy.signal import butter, sosfilt
    SR = reed.SR
    n = int(0.01 * SR)

    def env(y):
        k = len(y) // n
        return np.sqrt(np.mean(y[:k * n].reshape(k, n) ** 2, axis=1))

    e_hi = env(sosfilt(butter(4, HP_HZ, "highpass", fs=SR, output="sos"), x))
    out = {"cents": [], "level_db": [], "breath_db": [], "bursts_db": [], "peaks_db": []}
    for i, (m, t0, t1) in enumerate(zip(line, starts, ends)):
        a, b = int(t0 * SR), int(t1 * SR)
        mid = x[a + int(0.12 * SR):b - int(0.04 * SR)]
        f0 = reed.period_f0(mid, SR)
        out["cents"].append(float(1200 * np.log2(f0 / reed.f_of(m))) if np.isfinite(f0) else float("nan"))
        steady = np.median(e_hi[(a + int(0.12 * SR)) // n:(b - int(0.04 * SR)) // n])
        if i:
            w = e_hi[max(0, a // n - 4):a // n + 4]
            out["bursts_db"].append(float(20 * np.log10(w.max() / (steady + 1e-12) + 1e-12)))
        if i in tongued:
            lv = np.sqrt(np.mean(mid ** 2)) * np.sqrt(2)
            out["peaks_db"].append(float(20 * np.log10(np.abs(x[a:a + int(0.1 * SR)]).max() / lv)))
        s = x[a + int(0.1 * SR):b - int(0.04 * SR)]  # the pitch's end: a tongued note's key comes up 35 ms before the next
        out["level_db"].append(reed.level_a(s, SR))
        out["breath_db"].append(reed.floor(s, SR, m))
    out["steps_db"] = [float(v) for v in np.diff(out["level_db"])]
    return out


def phrase_job(tongued):
    """The line at mf's air through the live player, the virtual player off (as the fits play), and its measures."""
    import live
    import reed_tables
    ev, starts = phrase_events(tongued, live.CONTROLS["air"], 127 * reed_tables.X[1])
    x = live.render_events(ev, starts[-1] + LAST_S + 0.4, virtual=False)
    ends = starts[1:] + [starts[-1] + LAST_S]
    return phrase_measures(x, LINE, starts, ends, range(len(LINE)) if tongued else (0,))


def note_job(job):
    """(key, dynamics) as played, every fit in (the level too): its rise from the key, its steady part's bands and
    A-weighted level (dyn.py's window, 0.4-1.3 s)."""
    m, d = job
    x = play((m, d, {}, HELD_S))
    s = x[int(0.4 * reed.SR):int(1.3 * reed.SR)]
    sounding(s, m, d)
    return job, {"rise": float(reed.rise(x, reed.SR, 0.0)), "bands": reed.bands(s, reed.SR, m), "level": reed.level_a(s, reed.SR)}


def summary(phrase, bands, dyn, starts):
    """The report's headline: what the accepted state's numbers are held to."""
    out = {}
    for name, p in phrase.items():
        c = np.abs(np.asarray(p["cents"], float))
        out[name] = {"pitch_c": float(np.max(c)), "pitch_nan": int(np.isnan(c).sum()), "steps_db": [float(np.min(np.abs(p["steps_db"]))), float(np.max(np.abs(p["steps_db"])))],
                     "breath_spread_db": float(np.ptp(p["breath_db"])), "breath_sd_db": float(np.std(p["breath_db"])),
                     "level_spread_db": float(np.ptp(p["level_db"])), "bursts_db": float(np.max(p["bursts_db"])),
                     "bursts_median_db": float(np.median(p["bursts_db"])),
                     "peaks_db": float(np.max(p["peaks_db"])) if p["peaks_db"] else None}
    out["bands_mf"] = {r: [float(v) for v in bands[r]["peak"][:3]] for r in REGISTERS}
    out["dyn"] = {r: {k: float(dyn[r][k]) for k in ("pp_model", "pp_rec", "ff_model", "ff_rec")} for r in REGISTERS}
    out["starts"] = {d: {"median_abs_ms": float(starts[d]["median_abs_ms"]), "within_10_ms": starts[d]["within_10_ms"],
                         "keys": starts[d]["keys"]} for d in DYNS}
    return out


def report():
    """The accepted state's numbers on the live player (reed in the spec): the phrase's pitch per note, the steps
    between its notes, its breath per note, the bursts over 6 kHz at its changes, slurred then tongued at mf; per
    octave band at mf the harmonics' median peak (1-8 kHz) against the recordings', per register; pp and ff re mf
    (A-weighted, the median per register), the model's and the recordings'; the starts' rise against the recordings'
    (per key and dynamics, from the key). Every key must have its level (live.level_db reads it)."""
    import live
    import rt_tables
    import spec
    missing = [m for m in rt_tables.TABLE if str(m) not in live.LEVEL.get("open", {})]
    if missing:
        raise RuntimeError(f"keys without a level: {missing}")
    rec = takes()
    first = first_clarion()
    rt_tables.load("open")
    jobs = [(m, d) for m in rt_tables.TABLE for d in DYNS if (m, d) in rec]
    with pool() as ex:
        phrases = dict(zip(("slurred", "tongued"), ex.map(phrase_job, (False, True))))
        got = dict(ex.map(note_job, jobs))
    lost = [f"{name} {m}" for name, p in phrases.items() for m, c in zip(LINE, p["cents"]) if not np.isfinite(c)]
    if lost:  # a note without a pitch: none, or out of its register's reach (the max would leave it out)
        raise RuntimeError(f"the phrase's notes without a pitch: {lost}")
    bands, dyn, starts = {}, {}, {}
    for r, reg in enumerate(REGISTERS):
        ks = [m for m in rt_tables.TABLE if register(m, first) == r]
        diff = np.array([[[a[i] - b[i] for a, b in zip(got[m, "mf"]["bands"], heard("bands", *rec[m, "mf"], m))] for i in (0, 1)]
                         for m in ks if (m, "mf") in got])  # key, peak or floor, band: model less recording
        bands[reg] = {"peak": np.nanmedian(diff[:, 0], axis=0).tolist(), "floor": np.nanmedian(diff[:, 1], axis=0).tolist()}
        both = [m for m in ks if all((m, d) in got for d in DYNS)]
        lv = {(m, d): heard("level", *rec[m, d], m) for m in both for d in DYNS}
        dyn[reg] = {f"{d}_{w}": float(np.median([(got[m, d]["level"] - got[m, "mf"]["level"]) if w == "model" else
                                                 (lv[m, d] - lv[m, "mf"]) for m in both]))
                    for d in ("pp", "ff") for w in ("model", "rec")}
    for d in DYNS:
        rows = {f"{m}_{d}": [got[m, d]["rise"], heard("rise", *rec[m, d], m)] for m in rt_tables.TABLE if (m, d) in got}
        off = np.array([a - b for a, b in rows.values()])
        starts[d] = {"median_abs_ms": float(np.nanmedian(np.abs(off))), "within_10_ms": int(np.sum(np.abs(off) <= 10.0)),
                     "keys": len(rows), "rise_ms": rows}
    doc = {"summary": summary(phrases, bands, dyn, starts), "phrase": phrases, "bands_mf": bands, "dyn": dyn, "starts": starts,
           "level_keys": len(rt_tables.TABLE)}
    spec.save_cache("report", doc)
    print(json.dumps(doc["summary"], indent=1), flush=True)
    for name, p in phrases.items():
        print(f"{name}: c", " ".join(f"{m}:{c:+.1f}" for m, c in zip(LINE, p["cents"])), flush=True)
        print(f"   steps dB", " ".join(f"{v:+.1f}" for v in p["steps_db"]), "| bursts dB", " ".join(f"{v:+.0f}" for v in p["bursts_db"]),
              "| breath dB", " ".join(f"{v:.1f}" for v in p["breath_db"]), flush=True)
    for d in DYNS:
        print(f"rise {d} ms model/recording:", " ".join(f"{k.split('_')[0]}:{a:.0f}/{b:.0f}" for k, (a, b) in starts[d]["rise_ms"].items()), flush=True)


if __name__ == "__main__":
    report()
