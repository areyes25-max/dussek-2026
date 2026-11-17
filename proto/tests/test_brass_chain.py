"""The brass chains in projects/steps.toml and proto/brass/steps.py."""
import hashlib
import importlib.util
import json
import os
import shutil
import subprocess
import sys

import numpy as np
import pytest

import build
import project as P

FIVE = ("trumpet", "tenor_trombone", "horn", "bass_trombone", "tuba")
BRASS = os.path.join(P.REPO, "proto", "brass")


def test_the_chains_end_at_the_tables_the_report_and_the_export():
    for name in FIVE:
        assert build.chain(name)[-3:] == ["brass_tables", "brass_report", "export_brass"]


def test_only_the_lender_measures_the_mutes():
    for step in ("brass_mute_shapes", "brass_mute_residual"):
        assert step in build.chain("trumpet") and step not in build.chain("tuba")


def test_the_soft_slurs_are_repaired_before_the_levels_are_measured():
    """The repair moves lips and air, so a key's level comes after it; the landing's repair of slurs comes before it."""
    for name in FIVE:
        chain = build.chain(name)
        assert chain.index("brass_land") < chain.index("brass_slurs") < chain.index("brass_level")


def test_the_steps_exist():
    spec = importlib.util.spec_from_file_location("brass_steps", os.path.join(BRASS, "steps.py"))
    steps = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(steps)
    for name in FIVE:
        for step in build.chain(name)[:-1]:  # the export is a script of its own
            assert callable(getattr(steps, step.split("#")[0])), step


def test_brass_fit_access_is_checked():
    # under the driver's environment: brass_speak writing another step's key raises and leaves the file
    path = P.fit_path("tuba")
    before = hashlib.sha256(open(path, "rb").read()).hexdigest()
    env = {**os.environ, "BRASS_PROJECT": "tuba", "PE_STEP": "brass_speak", "PE_PROJECT": "tuba"}
    env.pop("PE_RECORD", None)
    r = subprocess.run([sys.executable, "-c", 'import spec; spec.save("level", {})'], cwd=BRASS, env=env,
                       capture_output=True, text=True)
    assert r.returncode != 0 and "Undeclared" in r.stderr
    assert hashlib.sha256(open(path, "rb").read()).hexdigest() == before


def test_a_mute_s_report_skips_a_note_the_instrument_lacks():
    """The mutes' one measurement is on F4, over the top of the tuba's table."""
    code = "import muted; muted.measured_note(['straight']); print('done')"
    env = {**os.environ, "BRASS_PROJECT": "tuba"}
    out = subprocess.run([sys.executable, "-c", code], cwd=BRASS, env=env, capture_output=True, text=True)
    assert out.returncode == 0 and "done" in out.stdout, out.stderr[-400:]


def picks(project, mute, key):
    """What each of a key's dynamics plays on under a mute: the cents from its open mode to the mode it takes."""
    code = ("import json, muted; "
            f"print(json.dumps({{d: muted.mode_cents({key}, d, {mute!r}) for d in ('pp', 'mf', 'ff')}}))")
    env = {**os.environ, "BRASS_PROJECT": project}
    out = subprocess.run([sys.executable, "-c", code], cwd=BRASS, env=env, capture_output=True, text=True)
    assert out.returncode == 0, out.stderr[-400:]
    return json.loads(out.stdout.splitlines()[-1])


def test_a_split_mode_is_one_mode_for_all_of_a_key_s_dynamics():
    """A mute splits a mode, the open note between the halves: the lower half does not hold, and a few cents
    between the dynamics must not send one of them there."""
    got = picks("tenor_trombone", "straight", 45)  # the straight mute's split key since the mutes' shapes were refitted (step 242)
    assert all(c > 100 for c in got.values()), got


def test_a_key_s_dynamics_that_agree_keep_their_mode():
    got = picks("tenor_trombone", "straight", 50)
    assert max(got.values()) - min(got.values()) < 60 and max(abs(c) for c in got.values()) < 100, got


def test_a_mute_s_steps_end_by_checking_that_its_notes_hold(monkeypatch):
    """The mode fix moves slides and lips: what it leaves is what a player gets."""
    spec = importlib.util.spec_from_file_location("brass_steps", os.path.join(BRASS, "steps.py"))
    steps = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(steps)
    calls = []
    monkeypatch.setattr(steps, "run", lambda *a, **k: calls.append(a), raising=True)
    monkeypatch.setattr(steps.P, "update_fit", lambda *a, **k: None)  # the step starts from none: not the committed fit
    steps.NAME = "tuba"
    steps.brass_mutes()
    assert calls[-1] == ("rt_stability", "straight", "repair")
    assert calls.index(("mode_fix", "lips", "straight")) < len(calls) - 1


def test_the_slur_repair_covers_what_goes_in_the_bell(monkeypatch):
    spec = importlib.util.spec_from_file_location("brass_steps", os.path.join(BRASS, "steps.py"))
    steps = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(steps)
    calls = []
    monkeypatch.setattr(steps, "run", lambda *a, **k: calls.append(a), raising=True)
    steps.NAME = "tuba"
    steps.brass_slurs()
    assert calls == [("slur_fix", "repair", "open"), ("slur_fix", "repair", "straight")]


def test_the_report_counts_what_can_be_played():
    """Keys outside the range stay quiet: a count that holds them says nothing about the instrument."""
    code = "import json, rt_stability; print(json.dumps(rt_stability.in_range({34: 9, 40: 3, 72: 6, 74: 9})))"
    env = {**os.environ, "BRASS_PROJECT": "tenor_trombone"}
    out = subprocess.run([sys.executable, "-c", code], cwd=BRASS, env=env, capture_output=True, text=True)
    assert out.returncode == 0, out.stderr[-400:]
    assert json.loads(out.stdout.splitlines()[-1]) == {"40": 3, "72": 6}


def landing(project, key, before, seed, vel=None):
    v = "" if vel is None else f", vel={vel}"
    code = f"import slur_fix; print(slur_fix.landing({key}, {before}, {seed}{v}))"
    env = {**os.environ, "BRASS_PROJECT": project}
    out = subprocess.run([sys.executable, "-c", code], cwd=BRASS, env=env, capture_output=True, text=True)
    assert out.returncode == 0, out.stderr[-400:]
    return float(out.stdout.splitlines()[-1])


def test_a_soft_falling_line_lands_on_its_notes():
    """The horn's G4 and F#4, a stronger mode close under them: a soft slur stayed up to 60 c flat for as long as it sounded."""
    for seed in (1, 2):
        assert abs(landing("horn", 67, [69], seed)) < 25
        assert abs(landing("horn", 66, [69, 67], seed)) < 25


def test_a_loud_falling_line_lands_on_its_notes():
    """The trumpet's G-sharp 5 slurred from above at ff: it sat 115 c flat, near G5, for up to a second (heard in the plugin)."""
    for before, seed in (([82], 1), ([81], 2), ([83], 3)):
        assert abs(landing("trumpet", 80, before, seed, vel=127)) < 25


def test_a_slur_repair_that_falls_short_leaves_the_tongued_note_be():
    """Most playing is tongued: a move that only comes closer may not take the tongued note further off."""
    code = ("import slur_fix as f; print(f.may_take(-60, -30, 20, 10), f.may_take(-60, -30, 40, 10), "
            "f.may_take(-60, -30, 40, 45), f.may_take(-60, -55, 10, 10))")
    env = {**os.environ, "BRASS_PROJECT": "tuba"}
    out = subprocess.run([sys.executable, "-c", code], cwd=BRASS, env=env, capture_output=True, text=True)
    assert out.returncode == 0, out.stderr[-400:]
    assert out.stdout.split() == ["True", "False", "True", "False"]


def test_the_prototype_plays_the_export_s_keys():
    """The player and the stability check stop where the export does: at the table's top unless `above` says more."""
    code = ("import json, live, rt_stability, rt_tables; t = rt_tables.TABLE; "
            "print(json.dumps([live.plays(t[-1]), live.plays(t[-1] + 1), live.plays(t[0] - 6), live.plays(t[0] - 7), "
            "rt_stability.KEYS[-1] == t[-1]]))")
    env = {**os.environ, "BRASS_PROJECT": "horn"}
    out = subprocess.run([sys.executable, "-c", code], cwd=BRASS, env=env, capture_output=True, text=True)
    assert out.returncode == 0, out.stderr[-400:]
    assert json.loads(out.stdout.splitlines()[-1]) == [True, False, True, False, True]


def test_an_instrument_without_mutes_still_writes_the_mutes_outputs(monkeypatch):
    """The driver checks a step's outputs: the horn has no mutes, and its fresh fit had none, so the step stayed stale."""
    spec = importlib.util.spec_from_file_location("brass_steps", os.path.join(BRASS, "steps.py"))
    steps = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(steps)
    calls, saved = [], []
    monkeypatch.setattr(steps, "run", lambda *a, **k: calls.append(a), raising=True)
    monkeypatch.setattr(steps.P, "update_fit", lambda part, values: saved.append((part, values)), raising=True)
    steps.NAME = "horn"
    steps.brass_mutes()
    assert calls == [] and saved == [(P.main_part("horn"), {"cells_mutes": {}, "tuning_mutes": {}})]


def test_a_slur_repair_moves_only_the_lips():
    """The air sets a note's level: 0.8 of it left the tenor's wah E-flat 4 5.6 dB under its line on a soft slur, mostly
    the bell's noise; 1.25 put others 0.7 to 4 dB over theirs."""
    code = "import json, slur_fix; m = slur_fix.candidates('open', 60); print(json.dumps([len(m), sorted({a[3] for a in m})]))"
    env = {**os.environ, "BRASS_PROJECT": "tenor_trombone"}
    out = subprocess.run([sys.executable, "-c", code], cwd=BRASS, env=env, capture_output=True, text=True)
    assert out.returncode == 0, out.stderr[-400:]
    count, airs = json.loads(out.stdout.splitlines()[-1])
    assert count > 0 and airs == [1.0]


def test_each_export_carries_its_project_s_output_gain():
    """The brass sit at the grands' loudness (step 243): a gain per project in [export], written into its export."""
    for name in FIVE:
        gain = P.section(name, "export").get("output_gain_db")
        assert gain is not None, name
        with open(os.path.join(P.REPO, "instruments", name, "instrument.json")) as f:
            assert json.load(f).get("output_gain_db") == gain, name


def test_the_reed_steps_exist():
    spec = importlib.util.spec_from_file_location("brass_steps", os.path.join(BRASS, "steps.py"))
    steps = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(steps)
    chain = build.chain("clarinet")
    assert chain[:6] == ["reed_pressures", "reed_tune", "reed_radiation", "reed_starts", "reed_breath", "reed_levels"]
    for step in chain:
        if step != "export_brass":  # the export is a script of its own
            assert callable(getattr(steps, step.split("#")[0])), step


def test_reed_pressures_access_is_checked():
    """Under the driver's environment the pressures' step writing the tuning (the next step's) raises and leaves the file."""
    path = P.fit_path("clarinet")
    before = open(path, "rb").read() if os.path.exists(path) else None
    env = {**os.environ, "BRASS_PROJECT": "clarinet", "PE_STEP": "reed_pressures", "PE_PROJECT": "clarinet"}
    env.pop("PE_RECORD", None)
    r = subprocess.run([sys.executable, "-c", 'import spec; spec.save("tuning", {})'], cwd=BRASS, env=env,
                       capture_output=True, text=True)
    assert r.returncode != 0 and "Undeclared" in r.stderr
    assert (open(path, "rb").read() if os.path.exists(path) else None) == before


def test_the_path_keeps_neighbors_close():
    """Five keys, one whose best point lies far from its neighbors' by a small gain: the path stays with them until the
    step's cost per change stops outweighing the gain."""
    sys.path.insert(0, BRASS)
    import reed_fit
    offs = [[0.1, 0.2, 0.4]] * 5
    costs = [[1.0, 0.0, 1.0]] * 5
    costs[2] = [1.0, 1.0, 0.0]  # the outlier prefers 0.4, by 1 dB
    held = reed_fit.path(costs, offs, step=3.0)
    assert held == [1, 1, 1, 1, 1]  # leaving costs 3 dB x 4 each way, for 1 dB
    free = reed_fit.path(costs, offs, step=0.0)
    assert free == [1, 1, 2, 1, 1]
    picks = reed_fit.path(costs, offs, step=0.1)  # 0.1 x 4 = 0.4 per jump, twice: still less than the gain of 1
    assert picks == [1, 1, 2, 1, 1]
    for step in (0.0, 0.1, 3.0):
        p = reed_fit.path(costs, offs, step)
        jumps = [abs(offs[0][a] - offs[0][b]) / 0.05 * step for a, b in zip(p, p[1:])]
        assert sum(jumps) <= 2 * 1.0 + 1e-9  # no path pays more in jumps than the outlier's gain would save


def grid_of(rows):
    """A key's grid from (gamma, level dB, brightness dB) rows: its harmonics 2, 3, 5 and 7 that far from the first."""
    harm = lambda b: [0.0, b, b, -70.0, b, -70.0, b] + [-70.0] * 9
    return {"threshold": rows[0][0], "grid": [[g, {"level_db": lv, "cents": 0.0, "harm_db": harm(b)}] for g, lv, b in rows]}


def recording(level, b):
    return {"level_db": level, "cents": 0.0, "harm_db": [0.0, b, b, -70.0, b, -70.0, b] + [-70.0] * 9}


def test_select_chooses_per_register_and_weighs_the_level():
    """Two keys either side of the clarion's first: a step too dear to move between neighbors keeps each register on its
    own choice; pp follows the level its recording has against mf, from the mf point chosen, the miss capped."""
    sys.path.insert(0, BRASS)
    import reed_fit
    rows = [(0.40, -10.0, -14.0), (0.45, -5.0, -12.0), (0.50, 0.0, -10.0), (0.55, 3.0, -8.0)]
    grids = {str(m): grid_of(rows) for m in (67, 68, 69, 70)}
    rec = {f"{m}_mf": recording(-20.0, -10.0 if m < 69 else 0.0) for m in (67, 68, 69, 70)}
    picks = reed_fit.select(grids, rec, first=69, step=100.0)
    assert [picks[str(m)]["mf"] for m in (67, 68, 69, 70)] == [0.50, 0.50, 0.55, 0.55]  # the registers apart
    assert all("pp" not in picks[str(m)] for m in (67, 68, 69, 70))  # no pp recorded: none chosen

    # pp 10 dB under mf in the recording, its brightness 0.5 dB from the mf point's: the level wants the softest point
    rec["67_pp"] = recording(-30.0, -10.5)
    one = {"67": grids["67"]}
    assert reed_fit.select(one, rec, first=69, level_weight=0.0)["67"]["pp"] == 0.50  # brightness alone
    assert reed_fit.select(one, rec, first=69, level_weight=1.0)["67"]["pp"] == 0.40  # 3.5 dB of brightness for 10 of level
    # capped at 1 dB the level saves at most 1 dB: the brightness wins
    assert reed_fit.select(one, rec, first=69, level_weight=1.0, cap=1.0)["67"]["pp"] == 0.50

    # mf goes first: on the loudest point (0.55, +3 dB) pp's -10 dB from there is nearest 0.45 (-5 dB), not 0.40
    rec["67_mf"] = recording(-20.0, -8.0)
    rec["67_pp"] = recording(-30.0, -11.0)
    got = reed_fit.select(one, rec, first=69, level_weight=10.0)["67"]
    assert got["mf"] == 0.55 and got["pp"] == 0.45


REED_STEPS = {"reed_radiation": "radiation", "reed_starts": "starts", "reed_breath": "breath", "reed_levels": "levels"}

# the dry run: the live player's renders and the recordings stand-ins (a tone that rises as fast as its push), the
# pool a plain loop; the steps' own code runs as the driver runs it
DRY = """
import os
import sys
import numpy as np
import reed, reed_fit

SR = reed.SR


def tone(m, rise_s, level_db, noise, dur):
    t = np.arange(int(dur * SR)) / SR
    f = reed.f_of(m)
    x = sum(np.sin(2 * np.pi * k * f * t) / k ** 1.5 for k in range(1, 40) if k * f < SR / 2 - 500)
    x = x * (1 - np.exp(-t / rise_s)) * 10 ** (level_db / 20)
    return x + noise * np.random.default_rng(m).standard_normal(len(t))


class Serial:
    def __enter__(self):
        return self

    def __exit__(self, *a):
        return False

    def map(self, f, jobs):
        return list(map(f, jobs))


LEVEL = {"pp": -30.0, "mf": -20.0, "ff": -16.0}
reed_fit.pool = Serial
reed_fit.tables = lambda radiation=True: None
reed_fit.takes = lambda: {(m, d): (np.concatenate([np.zeros(SR // 10), tone(m, 0.04, LEVEL[d] - 0.2 * m, 3e-4, 1.5)]), SR)
                          for m in (60, 62, 76) for d in ("pp", "mf", "ff")}
SILENT = os.environ.get("DRY_SILENT")  # "<midi>_<dyn>": that note dies
WEAK = os.environ.get("DRY_WEAK")  # "<midi>_<dyn>": that note holds only when pushed with 8 m/s or more


def play(job):
    m, d, over, dur = job
    if f"{m}_{d}" == SILENT:
        return np.zeros(int(dur * SR))
    seed = over.get("seed", 6.0)
    weak = 40.0 if f"{m}_{d}" == WEAK and seed < 8.0 else 0.0
    return tone(m, 0.2 / seed, LEVEL[d] - weak, 0.1 * over.get("breath", 0.002), dur)


reed_fit.play = play
getattr(reed_fit, sys.argv[1])()
"""


def test_the_reed_steps_own_their_keys(tmp_path):
    """Each of the starts', the radiation's, the breath's and the levels' steps, run under the driver's checks in a copy
    of the project (the renders and the recordings stand-ins), writes what its entry owns: no more (a write it does not
    declare raises), no less."""
    projects = tmp_path / "projects"
    os.makedirs(projects / "clarinet" / "fits")
    shutil.copy(os.path.join(P.REPO, "projects", "clarinet", "project.toml"), projects / "clarinet")
    shutil.copy(os.path.join(P.REPO, "projects", "steps.toml"), projects)
    fit = projects / "clarinet" / "fits" / "clarinet.json"
    rows = {"threshold": 0.4, "pp": 0.45, "mf": 0.6, "ff": 0.8}
    json.dump({"pressures": {"60": rows, "62": rows, "76": rows}, "tuning": {}}, open(fit, "w"))
    cache = projects / "clarinet" / "cache" / "clarinet"

    def state():
        doc = json.load(open(fit))
        files = sorted(os.listdir(cache)) if os.path.isdir(cache) else []
        return ({f"fit.{k}": json.dumps(v, sort_keys=True) for k, v in doc.items()},
                {f"cache.{n[:-5]}": open(cache / n).read() for n in files if n.endswith(".json")})

    for step, what in REED_STEPS.items():
        before = state()
        env = {**os.environ, "BRASS_PROJECT": "clarinet", "PE_PROJECTS": str(projects), "PE_STEP": step,
               "PE_PROJECT": "clarinet", "OMP_NUM_THREADS": "1"}
        for k in ("PE_RECORD", "REED_KEYS", "BREATH_DB"):
            env.pop(k, None)
        r = subprocess.run([sys.executable, "-c", DRY, what], cwd=BRASS, env=env, capture_output=True, text=True)
        assert r.returncode == 0, (step, r.stderr[-3000:])
        after = state()
        wrote = {k for part in (0, 1) for k, v in after[part].items() if before[part].get(k) != v}
        entry = P.step_entry(step)
        assert wrote == set(entry["owns"]) - set(entry.get("memos", [])), step

    got = json.load(open(fit))
    assert sorted(got["seeds"]) == sorted(f"{m}_{d}" for m in (60, 62, 76) for d in ("pp", "mf", "ff"))
    assert sorted(got["radiation"]) == ["60", "62", "76"] and all(len(g) == 2 for g in got["radiation"].values())
    assert sorted(got["breath"]) == ["key", "key_dyn", "level", "shape"] and len(got["breath"]["shape"]) == 5
    assert all(len(got["level"]["open"][str(m)]) == 4 for m in (60, 62, 76))


def test_the_breath_trim_is_applied_after_the_fit(tmp_path):
    """The fit matches the recordings' floor and keeps that level; the tables take the ear's trim (-8 dB) over it."""
    projects = tmp_path / "projects"
    os.makedirs(projects / "clarinet" / "fits")
    shutil.copy(os.path.join(P.REPO, "projects", "clarinet", "project.toml"), projects / "clarinet")
    shutil.copy(os.path.join(P.REPO, "projects", "steps.toml"), projects)
    b = {"level": {"pp": 0.01, "mf": 0.01, "ff": 0.01}, "shape": [0.0] * 5, "key": {}, "key_dyn": {}}
    json.dump({"breath": b}, open(projects / "clarinet" / "fits" / "clarinet.json", "w"))
    code = ("import json, reed_tables; print(json.dumps([reed_tables.S['breath_trim_db'], reed_tables.breath(60, 'mf'), "
            "reed_tables.breath(60, 'mf', trim=False), reed_tables.breath(60, 'mf', {'level': {'mf': 0.02}}, trim=False)]))")
    env = {**os.environ, "BRASS_PROJECT": "clarinet", "PE_PROJECTS": str(projects)}
    for k in ("PE_RECORD", "PE_STEP", "PE_PROJECT"):
        env.pop(k, None)
    r = subprocess.run([sys.executable, "-c", code], cwd=BRASS, env=env, capture_output=True, text=True)
    assert r.returncode == 0, r.stderr[-2000:]
    trim, played, fitted, other = json.loads(r.stdout.splitlines()[-1])
    assert trim == -8.0
    assert played == pytest.approx(0.00398, abs=5e-6)
    assert fitted == 0.01 and other == 0.02


def test_the_level_lines_keep_the_rise_with_pitch():
    """The recordings rise 0.5 dB per key (one take 9 dB off its line), the model is even: the mf trims rise with
    them, their median 0 (the loudness is the gain's); pp and ff take the recordings' steps re mf."""
    sys.path.insert(0, BRASS)
    import reed_fit
    keys = list(range(50, 83))
    rng = np.random.default_rng(5)
    line = np.array([-30.0 + 0.5 * (k - 50) for k in keys])
    rec = {"mf": line.copy(), "pp": line - 15.0, "ff": line + 3.0}
    rec["mf"][7] += 9.0  # a take off its line: left out
    mod = {"mf": -20.0 + 0.3 * rng.standard_normal(len(keys))}
    mod["pp"], mod["ff"] = mod["mf"] - 4.0, mod["mf"] + 0.5
    t = reed_fit.level_table(keys, rec, mod)["open"]
    mf = np.array([t[str(k)][1] for k in keys])
    assert np.allclose(mf, line - mod["mf"] - np.median(line - mod["mf"]), atol=1e-9)
    assert np.allclose(np.diff(mf + mod["mf"]), 0.5, atol=1e-9)
    for k in keys:  # the steps' lines through the takes, the one off it left out, less the model's own
        pp, a, ff, top = t[str(k)]
        assert pp - a == pytest.approx(-15.0 + 4.0, abs=1e-9)
        assert ff - a == pytest.approx(3.0 - 0.5, abs=1e-9) and top == ff
        assert t[f"{k}_pp"] == 0.0 and t[f"{k}_ff"] == 0.0  # one table at every dynamics: no step to close


def dry_project(tmp_path):
    projects = tmp_path / "projects"
    os.makedirs(projects / "clarinet" / "fits")
    shutil.copy(os.path.join(P.REPO, "projects", "clarinet", "project.toml"), projects / "clarinet")
    shutil.copy(os.path.join(P.REPO, "projects", "steps.toml"), projects)
    rows = {"threshold": 0.4, "pp": 0.45, "mf": 0.6, "ff": 0.8}
    json.dump({"pressures": {"60": rows, "62": rows, "76": rows}, "tuning": {}},
              open(projects / "clarinet" / "fits" / "clarinet.json", "w"))
    return projects


@pytest.mark.parametrize("step", ["reed_starts", "reed_levels"])
def test_a_silent_note_takes_no_seed_or_level(tmp_path, step):
    """A note that dies (76 at pp, silent in the stand-in's renders) stops the step, named, before it writes: a seed or
    a level fitted to it would put up to 60 dB on what is left of it."""
    projects = dry_project(tmp_path)
    fit = projects / "clarinet" / "fits" / "clarinet.json"
    before = open(fit).read()
    env = {**os.environ, "BRASS_PROJECT": "clarinet", "PE_PROJECTS": str(projects), "PE_STEP": step,
           "PE_PROJECT": "clarinet", "OMP_NUM_THREADS": "1", "DRY_SILENT": "76_pp"}
    for k in ("PE_RECORD", "REED_KEYS", "BREATH_DB"):
        env.pop(k, None)
    r = subprocess.run([sys.executable, "-c", DRY, REED_STEPS[step]], cwd=BRASS, env=env, capture_output=True, text=True)
    assert r.returncode != 0 and "key 76 at pp is silent" in r.stderr, r.stderr[-2000:]
    assert open(fit).read() == before


def test_the_tuning_lifts_a_pressure_the_barrel_silenced():
    """The tuned barrel moved 76's threshold over its pp: pp moves up the grid past a point that is silent too, to the
    first that sounds; the points that still sound stay; a key with nothing sounding above raises."""
    sys.path.insert(0, BRASS)
    import reed_fit
    grid = lambda gs: {"threshold": gs[0], "grid": [[g, {}] for g in gs]}
    grids = {"76": grid([0.30, 0.33, 0.38, 0.40, 0.42, 0.46]), "60": grid([0.43, 0.45, 0.65, 0.73])}
    pressures = {"76": {"threshold": 0.30, "pp": 0.33, "mf": 0.42, "ff": 0.46},
                 "60": {"threshold": 0.43, "pp": 0.45, "mf": 0.65, "ff": 0.73}}
    plays = {(76, 0.33): False, (76, 0.38): False, (76, 0.40): True, (76, 0.42): True, (76, 0.46): True,
             (60, 0.45): True, (60, 0.65): True, (60, 0.73): True}
    got, moves = reed_fit.lift(pressures, grids, plays)
    assert got["76"] == {"threshold": 0.30, "pp": 0.40, "mf": 0.42, "ff": 0.46}
    assert got["60"] == pressures["60"]
    assert moves == [(76, "pp", 0.33, 0.40)]
    with pytest.raises(RuntimeError, match="key 76 at pp"):
        reed_fit.lift(pressures, grids, {**plays, (76, 0.40): False, (76, 0.42): False, (76, 0.46): False})


def test_the_reed_chain_ends_at_the_tables_the_report_and_the_export():
    assert build.chain("clarinet")[-3:] == ["brass_tables", "reed_report", "export_brass"]


REED_FITS = ("fit.breath", "fit.pressures", "fit.radiation", "fit.seeds")


def test_a_reed_s_fits_are_no_brass_table_s_input():
    """The tables and the export read a reed's fits (reed_tables.py): in the reed chain inputs, linked to the steps that
    write them; for the brass, which have none, seeds no earlier step writes, left out of their fingerprints (the brass
    chains stay fresh)."""
    for step in ("brass_tables", "export_brass"):
        reads, links, _ = build._in_chain("clarinet", step)
        assert set(REED_FITS) <= reads and set(REED_FITS) <= links, step
        for name in FIVE:
            reads, links, _ = build._in_chain(name, step)
            assert not set(REED_FITS) & (reads | links), (name, step)


MEMO = """
import live, reed_tables, rt_tables
reed_tables.memo_key()
live.noise_shape()
print("read")
"""


@pytest.mark.parametrize("step", ["brass_tables", "reed_report", "export_brass"])
def test_the_reed_s_last_steps_declare_its_fits(tmp_path, step):
    """Under the driver's checks the tables' memo key (every reed fit) and the breath's shape read as declared."""
    projects = dry_project(tmp_path)
    env = {**os.environ, "BRASS_PROJECT": "clarinet", "PE_PROJECTS": str(projects), "PE_STEP": step,
           "PE_PROJECT": "clarinet", "OMP_NUM_THREADS": "1"}
    for k in ("PE_RECORD", "REED_KEYS", "BREATH_DB"):
        env.pop(k, None)
    r = subprocess.run([sys.executable, "-c", MEMO], cwd=BRASS, env=env, capture_output=True, text=True)
    assert r.returncode == 0 and "read" in r.stdout, r.stderr[-2000:]


def test_the_mf_floor_is_judged_as_the_level(monkeypatch):
    """The mf breath per key is judged over the level's window (0.4-1.0 s, the spike's trims: both on one render); pp
    and ff over 0.4-1.3 s (its breath_dyn)."""
    sys.path.insert(0, BRASS)
    import reed_fit
    seen = []
    monkeypatch.setattr(reed_fit, "play", lambda job: np.zeros(int(job[3] * 44100)))
    monkeypatch.setattr(reed_fit, "judge", lambda what, s, sr, m: seen.append((what, len(s) / sr)))
    for d in ("pp", "mf", "ff"):
        reed_fit.judged((60, d, {}, "floor"))
    assert [round(t, 6) for _, t in seen] == [0.9, 0.6, 0.9]


def test_the_report_s_phrase_is_played_as_a_keyboard_player_plays_it():
    """Slurred: each key down before the last comes up (an overlap); tongued: each key up 35 ms before the next (a key
    in the release's fade is tongued). The spike's line, 0.32 s notes, the last 1.0 s, at mf's air."""
    sys.path.insert(0, BRASS)
    import reed_report
    line = reed_report.LINE
    assert line[0] == 55 and max(line) == 82 and line[-1] == 55 and line.index(82) == len(line) // 2
    for tongued in (False, True):
        ev, starts = reed_report.phrase_events(tongued, 11, 127 * 0.55)
        ons = [e for e in ev if e[1] == "on"]
        offs = [e for e in ev if e[1] == "off"]
        assert [e[2] for e in ons] == line and [round(e[0], 9) for e in ons] == [round(t, 9) for t in starts]
        assert np.allclose(np.diff(starts), 0.32)
        assert ev[0] == (0.0, "cc", 11, 127 * 0.55)
        for i, (t, _, m, _) in enumerate(ons[1:]):
            off = next(e for e in offs if e[2] == line[i] and e[0] > ons[i][0])
            assert (off[0] - t == pytest.approx(-0.035)) if tongued else (off[0] > t)
        assert offs[-1][0] == pytest.approx(starts[-1] + 1.0)


def test_the_report_measures_a_phrase():
    """On a made phrase of in-tune notes at known levels: the pitch per note, the steps between them, the breath per note
    and the bursts over 6 kHz at the changes (none here)."""
    sys.path.insert(0, BRASS)
    import reed
    import reed_report
    SR = reed.SR
    line = [55, 58, 62, 58, 55]
    starts = [0.0, 0.32, 0.64, 0.96, 1.28]
    ends = starts[1:] + [2.28]
    levels = [0.0, -2.0, -1.0, -3.0, 0.0]
    rng = np.random.default_rng(3)
    x = np.zeros(int(2.5 * SR))
    alone = []
    for m, a, b, lv in zip(line, starts, ends, levels):
        t = np.arange(int((b - a) * SR)) / SR
        f = reed.f_of(m)
        tone = sum(np.sin(2 * np.pi * k * f * t) / k for k in range(1, 60, 2) if k * f < 9000) * 0.1 * 10 ** (lv / 20)
        i = int(a * SR)
        fade = np.minimum(1.0, np.minimum(t, t[-1] - t) / 0.005)  # 5 ms in and out: no click at the changes
        x[i:i + len(t)] += tone * fade
        alone.append(reed.level_a(tone[int(0.1 * SR):], SR))  # A-weighted: the same level reads by pitch
    x += 1e-5 * rng.standard_normal(len(x))
    got = reed_report.phrase_measures(x, line, starts, ends)
    assert max(abs(c) for c in got["cents"]) < 1.0
    assert np.allclose(got["steps_db"], np.diff(alone), atol=0.3)
    assert len(got["breath_db"]) == len(line) and len(got["bursts_db"]) == len(line) - 1
    assert max(got["bursts_db"]) < 3.0, got["bursts_db"]  # changes without a click read as none


def test_a_note_s_flow_is_not_its_sound(monkeypatch):
    """A held note that does not oscillate still lets its flow through: a DC 50 dB under full scale (the clarion's wide
    reed) is silence, not a note at its threshold (every clarion threshold fell to the bisection's floor)."""
    sys.path.insert(0, BRASS)
    import reed_fit
    SR = 44100
    flow = lambda m, g, **k: np.full(int(2.5 * SR), 10 ** (-50 / 20))
    monkeypatch.setattr(reed_fit.reed, "steady", flow)
    assert reed_fit.held((69, 0.35)) == ((69, 0.35), None)
    with pytest.raises(RuntimeError, match="key 69 at pp is silent"):
        reed_fit.sounding(flow(69, 0.35), 69, "pp")
    t = np.arange(int(2.5 * SR)) / SR
    monkeypatch.setattr(reed_fit.reed, "steady", lambda m, g, **k: flow(m, g) + 0.05 * np.sin(2 * np.pi * 440.0 * t))
    _, r = reed_fit.held((69, 0.42))
    assert r is not None and abs(r["cents"]) < 1.0


def test_a_start_is_pushed_until_its_note_holds(tmp_path):
    """A pp just over its threshold holds only when its push starts it near its level; pushed less, it rings and fades,
    so it reaches 3 dB under its weak level at once and reads as too fast: the push went to the bracket's floor (76 at
    pp, 0.05 m/s, then 32 dB of level on a note 46 dB under its mf). A push whose note stays 10 dB under what the
    bracket's top push holds is too small."""
    projects = dry_project(tmp_path)
    fit = projects / "clarinet" / "fits" / "clarinet.json"
    env = {**os.environ, "BRASS_PROJECT": "clarinet", "PE_PROJECTS": str(projects), "PE_STEP": "reed_starts",
           "PE_PROJECT": "clarinet", "OMP_NUM_THREADS": "1", "DRY_WEAK": "76_pp"}
    for k in ("PE_RECORD", "REED_KEYS", "BREATH_DB", "DRY_SILENT"):
        env.pop(k, None)
    r = subprocess.run([sys.executable, "-c", DRY, "starts"], cwd=BRASS, env=env, capture_output=True, text=True)
    assert r.returncode == 0, r.stderr[-3000:]
    seeds = json.load(open(fit))["seeds"]
    assert seeds["76_pp"] >= 8.0, seeds
    assert 3.0 < seeds["76_mf"] < 8.0 and 3.0 < seeds["60_pp"] < 8.0, seeds


def test_pp_takes_a_margin_that_holds():
    """A key's grid starts at its first point that keeps its level: the points under it (just over the threshold, a note
    still dying slowly) are out, the ones over it stay, whether they hold or not (far over the threshold a loud note's
    beating moves its level, and it is no near-threshold point)."""
    sys.path.insert(0, BRASS)
    import reed_fit
    grid = [[0.402, "a"], [0.405, "b"], [0.41, "c"], [0.42, "d"], [0.5, "e"]]
    holding = {0.402: False, 0.405: False, 0.41: True, 0.42: True, 0.5: False}
    assert reed_fit.margin(grid, holding) == [[0.41, "c"], [0.42, "d"], [0.5, "e"]]
    assert reed_fit.margin(grid, dict.fromkeys(holding, True)) == grid
    assert reed_fit.margin(grid, dict.fromkeys(holding, False)) == []


def test_a_note_that_dies_slowly_does_not_hold(monkeypatch):
    """Its level falls 2 dB across the judged window: it does not hold; one that keeps its level later on does."""
    sys.path.insert(0, BRASS)
    import reed_fit
    SR = 44100
    t = np.arange(int(3.0 * SR)) / SR
    dying = lambda m, g, **k: 0.1 * 10 ** (-8.0 * t / 20) * np.sin(2 * np.pi * 440 * t)  # 8 dB/s: 2 dB over 0.25 s
    monkeypatch.setattr(reed_fit.reed, "steady", dying)
    assert reed_fit.holds((76, 0.403)) == ((76, 0.403), False)
    monkeypatch.setattr(reed_fit.reed, "steady", lambda m, g, **k: 0.1 * np.sin(2 * np.pi * 440 * t))
    assert reed_fit.holds((76, 0.41)) == ((76, 0.41), True)
