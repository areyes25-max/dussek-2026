"""The tone-hole lattice bore (proto/brass/lattice.py): the layout places each fingering's first impedance peak on its
semitone, the clarion plays the second, and the layout is the spike's. The modules keep an instrument in module state,
so each check runs in a process of its own under the reed project."""
import json
import math
import os
import subprocess
import sys

import project as P
import pytest

BRASS = os.path.join(P.REPO, "proto", "brass")
LAYOUT = json.load(open(os.path.join(os.path.dirname(__file__), "data", "lattice_layout.json")))
WIDE_KEY = 51


def run(code):
    env = {**os.environ, "BRASS_PROJECT": "clarinet", "OMP_NUM_THREADS": "1"}
    env.pop("PE_RECORD", None)
    r = subprocess.run([sys.executable, "-c", code], cwd=BRASS, env=env, capture_output=True, text=True)
    assert r.returncode == 0, r.stderr[-2000:]
    return json.loads(r.stdout.splitlines()[-1])



@pytest.fixture(scope="module")
def bore():
    """Everything the checks read, from one process: the layout is solved once."""
    return run("""
import json, math, lattice
l, h = lattice.layout()
semitone = []
for m in range(50, 69):
    n = lattice.Note(m, extra=0.0)
    semitone.append([m, 1200 * math.log2(n.modes[0][0] / n.f)])
clarion = []
for m in range(69, 83):
    n = lattice.Note(m)
    clarion.append([m, n.register, 1200 * math.log2(n.played_peak() / n.f)])
raised = []
for m in (49, 83):
    try:
        lattice.Note(m)
    except ValueError:
        raised.append(m)
print(json.dumps(dict(length=l, holes=list(h), semitone=semitone, clarion=clarion, raised=raised)))
""")


def test_each_fingering_s_peak_lands_on_its_semitone(bore):
    assert [m for m, _ in bore["semitone"]] == list(range(50, 69))
    for m, cents in bore["semitone"]:
        if m == WIDE_KEY:
            continue
        assert abs(cents) < 5, (m, cents)


def test_the_one_wide_key_is_pinned(bore):
    # the spike's solver pins hole 1 at its bracket's end (4 mm under the bell's start): the key sounds wide with
    # extra=0, and the barrel pulls it in (Note's extra=None); the layout stays the spike's, so the offset is recorded
    cents = dict(bore["semitone"])[WIDE_KEY]
    assert abs(cents - LAYOUT["wide_key_cents"]) < 2, cents


def test_the_clarion_plays_its_second_peak(bore):
    assert [m for m, _, _ in bore["clarion"]] == list(range(69, 83))
    for m, reg, cents in bore["clarion"]:
        assert reg is True
        assert abs(cents) < 2, (m, cents)


def test_the_layout_is_the_spike_s(bore):
    assert abs(bore["length"] - LAYOUT["length"]) < 1e-6
    assert len(bore["holes"]) == len(LAYOUT["holes"]) == 18
    assert all(abs(a - b) < 1e-6 for a, b in zip(bore["holes"], LAYOUT["holes"]))


def test_a_key_outside_the_lattice_raises(bore):
    assert bore["raised"] == [49, 83]
