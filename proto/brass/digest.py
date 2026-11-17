"""A table's numbers in short: what a port or a refit is held to (proto/tests/test_brass_model.py, the
chain's brass_tables). Per dynamics the first three modes' frequency and Q and the output filter's level
per octave band, and the impedance filter's RMS; a reed's its flow-to-air filter's."""
import numpy as np

SR = 44100.0
EDGES = [125.0 * 2 ** i for i in range(8)]  # 125 Hz to 16 kHz


def band_levels(h):
    """The filter's level per octave band, dB."""
    H = np.abs(np.fft.rfft(h, 8192)) ** 2
    f = np.fft.rfftfreq(8192, 1 / SR)
    return [round(float(10 * np.log10(H[(f >= a) & (f < b)].mean() + 1e-30)), 3) for a, b in zip(EDGES, EDGES[1:])]


def of_entry(entry):
    """The digest of one key's table entry."""
    reed = "zh" in entry  # a reed's: its flow-to-air filter in place of the bell's and the residual
    out = {"zh_rms" if reed else "r_rms": float(np.sqrt(np.mean(np.asarray(entry["zh" if reed else "r"]) ** 2)))}
    for d in ("pp", "mf", "ff"):
        sp = entry["modes"][d][0]
        out[d] = {"hz": [float(v) for v in np.abs(sp.imag)[:3] / (2 * np.pi)],
                  "q": [float(v) for v in (np.abs(sp) / (-2 * sp.real))[:3]],
                  "bands": band_levels(entry["zh"] if reed else entry["h"][d])}
    return out
