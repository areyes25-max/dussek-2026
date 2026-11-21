"""pad_from_midi.py - genera una capa de pad sincronizada a partir del MIDI de piano.

Uso: py pad_from_midi.py entrada.mid salida_pad.wav [--sr 48000] [--ventana 0.5] [--brillo 1400] [--tail 6]
     [--mezclar-con piano.wav --salida-mezcla mezcla.wav --nivel -11]

Idea: en cada ventana de tiempo se toman las notas que suenan (con pedal incluido), se reducen
a sus clases de nota mas pesadas (max 4), se colocan en registro medio (C3-C5) mas una
fundamental grave suave, y se sintetizan con sierras desafinadas filtradas, ataque/caida lentos,
coro estereo y reverb larga. Mismo eje de tiempo que el MIDI: queda sincronizado con el piano.
"""
import argparse
import re
import subprocess
import numpy as np
import mido
import soundfile as sf


def notas_sonando(path):
    """Lista de (inicio_s, fin_s, nota, velocidad) con el pedal de sustain aplicado."""
    mid = mido.MidiFile(path)
    t, activas, pedal, sostenidas, out = 0.0, {}, False, {}, []
    for msg in mid:  # iterar el MidiFile da tiempos en segundos (tempo aplicado)
        t += msg.time
        if msg.type == 'control_change' and msg.control == 64:
            pedal = msg.value >= 64
            if not pedal:
                for n, (t0, v) in sostenidas.items():
                    out.append((t0, t, n, v))
                sostenidas = {}
        elif msg.type == 'note_on' and msg.velocity > 0:
            if msg.note in sostenidas:
                t0, v = sostenidas.pop(msg.note)
                out.append((t0, t, msg.note, v))
            activas[msg.note] = (t, msg.velocity)
        elif msg.type in ('note_off', 'note_on'):
            if msg.note in activas:
                t0, v = activas.pop(msg.note)
                if pedal:
                    sostenidas[msg.note] = (t0, v)
                else:
                    out.append((t0, t, msg.note, v))
    for d in (activas, sostenidas):
        for n, (t0, v) in d.items():
            out.append((t0, t, n, v))
    return out, t


def acordes(notas, dur, ventana):
    """Por ventana: (clases de nota ordenadas por peso, nota mas grave)."""
    nwin = int(np.ceil(dur / ventana)) + 1
    res = []
    for w in range(nwin):
        a, b = w * ventana, (w + 1) * ventana
        peso, grave = np.zeros(12), None
        for (t0, t1, n, v) in notas:
            sol = min(t1, b) - max(t0, a)
            if sol <= 0:
                continue
            # pesa la duracion dentro de la ventana, la fuerza, y favorece el registro grave/medio
            frescura = np.exp(-max(0.0, a - t0) / 1.5)  # una nota recien tocada pesa mas que una que solo sigue sonando por el pedal
            peso[n % 12] += sol * (v / 127) * (1.5 if n < 60 else 1.0) * frescura
            grave = n if grave is None or n < grave else grave
        clases = [int(c) for c in np.argsort(-peso) if peso[c] > 0.12 * max(peso.max(), 1e-9)][:4]
        res.append((tuple(sorted(clases)), grave))
    return res


def voz(clases, grave):
    """Notas del pad: clases en C3..C5 (48..71) + fundamental grave suave."""
    notas = sorted(48 + ((c - 48) % 12) + (12 if (48 + ((c - 48) % 12)) < 55 else 0) for c in clases)
    bajo = None
    if grave is not None:
        bajo = 36 + (grave - 36) % 12  # C2..B2
    return notas, bajo


def sintetizar(segmentos, dur, sr, brillo):
    n = int(dur * sr)
    L, R = np.zeros(n), np.zeros(n)
    t_all = np.arange(n) / sr
    ataque, caida = 0.9, 1.8
    for (t0, t1, notas, bajo) in segmentos:
        i0, i1 = int(t0 * sr), min(n, int((t1 + caida) * sr))
        if i1 <= i0:
            continue
        t = t_all[i0:i1] - t0
        env = np.clip(t / ataque, 0, 1)
        rel = t1 - t0
        env *= np.where(t > rel, np.exp(-(t - rel) / (caida / 4)), 1.0)
        env = env ** 1.5
        for k, m in enumerate(notas + ([bajo] if bajo is not None else [])):
            f = 440 * 2 ** ((m - 69) / 12)
            amp = (0.10 if m != bajo else 0.035) / max(1, len(notas)) ** 0.5
            for det, pan in ((-7, 0.25), (0, 0.5), (7, 0.75)):  # 3 voces desafinadas (cents), abiertas en estereo
                fd = f * 2 ** (det / 1200)
                ph = np.random.rand() * 2 * np.pi
                # sierra suave con pocos armonicos (banda limitada) + seno
                s = np.sin(2 * np.pi * fd * t + ph)
                for h in range(2, 7):
                    if fd * h < brillo * 1.6:
                        s += np.sin(2 * np.pi * fd * h * t + ph * h) / (h ** 1.6)
                s *= amp * env
                L[i0:i1] += s * (1 - pan)
                R[i0:i1] += s * pan
    return np.stack([L, R], 1)


def filtro_paso_bajo(x, sr, fc):
    from numpy.fft import rfft, irfft, rfftfreq
    out = np.empty_like(x)
    f = rfftfreq(len(x), 1 / sr)
    h = 1 / np.sqrt(1 + (f / fc) ** 4)  # 2o orden suave
    h *= 1 / np.sqrt(1 + (90 / np.maximum(f, 1)) ** 4)  # quita graves < 90 Hz (sin barro bajo el piano)
    for c in range(x.shape[1]):
        out[:, c] = irfft(rfft(x[:, c]) * h, len(x))
    return out


def reverb(x, sr, segundos=4.5, mezcla=0.45, seed=7):
    """Reverb por convolucion con ruido decreciente (estereo decorrelado)."""
    from scipy.signal import fftconvolve
    rng = np.random.default_rng(seed)
    n = int(segundos * sr)
    t = np.arange(n) / sr
    env = np.exp(-6.9 * t / segundos)
    out = np.zeros_like(x)
    for c in range(2):
        ir = rng.standard_normal(n) * env
        ir[: int(0.02 * sr)] *= np.linspace(0, 1, int(0.02 * sr))  # predelay suave
        ir /= np.sqrt(np.sum(ir ** 2))
        out[:, c] = fftconvolve(x[:, c], ir)[: len(x)]
    return (1 - mezcla) * x + mezcla * out


def coro(x, sr):
    n = len(x)
    t = np.arange(n) / sr
    out = x.copy()
    for c, fase in ((0, 0.0), (1, np.pi / 2)):
        d = (0.012 + 0.004 * np.sin(2 * np.pi * 0.25 * t + fase)) * sr
        idx = np.clip(np.arange(n) - d, 0, n - 1)
        out[:, c] = 0.7 * x[:, c] + 0.3 * np.interp(idx, np.arange(n), x[:, c])
    return out


def lufs(path):
    o = subprocess.run(['ffmpeg', '-hide_banner', '-nostats', '-i', path, '-af', 'ebur128', '-f', 'null', '-'],
                       capture_output=True, text=True).stderr
    v = re.findall(r'I:\s+(-?[\d.]+) LUFS', o)
    if not v:
        raise SystemExit('No pude medir LUFS con ffmpeg: ' + path)
    return float(v[-1])


def mezclar(piano, pad, salida, nivel):
    a, sr = sf.read(piano, always_2d=True)
    b, sr2 = sf.read(pad, always_2d=True)
    if sr != sr2:
        raise SystemExit(f'Frecuencias distintas: piano {sr}, pad {sr2}')
    n = max(len(a), len(b))
    A = np.zeros((n, 2)); B = np.zeros((n, 2))
    A[:len(a)] = a[:, :2] if a.shape[1] > 1 else np.repeat(a, 2, 1)
    B[:len(b)] = b
    la, lb = lufs(piano), lufs(pad)
    g = 10 ** (((la + nivel) - lb) / 20)
    sf.write(salida, (A + B * g).astype(np.float32), sr, subtype='FLOAT')
    print(f'mezcla: piano {la} LUFS, pad a {nivel} dB bajo el piano -> {salida}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('midi')
    ap.add_argument('salida')
    ap.add_argument('--sr', type=int, default=48000)
    ap.add_argument('--ventana', type=float, default=0.5, help='segundos por analisis de acorde')
    ap.add_argument('--brillo', type=float, default=1400, help='corte del filtro (Hz)')
    ap.add_argument('--tail', type=float, default=6.0)
    ap.add_argument('--mezclar-con', default=None, help='WAV del piano para mezclar debajo')
    ap.add_argument('--salida-mezcla', default=None)
    ap.add_argument('--nivel', type=float, default=-11.0, help='dB del pad bajo el piano')
    a = ap.parse_args()

    notas, fin = notas_sonando(a.midi)
    dur = fin + a.tail
    ac = acordes(notas, fin, a.ventana)
    segs = []
    for w, (cl, grave) in enumerate(ac):
        if not cl:
            continue
        t0, t1 = w * a.ventana, (w + 1) * a.ventana
        if segs and segs[-1][4] == cl:  # mismo acorde: alarga el segmento
            segs[-1][1] = t1
        else:
            nv, bajo = voz(cl, grave)
            segs.append([t0, t1, nv, bajo, cl])
    print(f'{len(notas)} notas, {len(segs)} acordes de pad, {fin:.1f} s')
    x = sintetizar([(s[0], s[1], s[2], s[3]) for s in segs], dur, a.sr, a.brillo)
    x = filtro_paso_bajo(x, a.sr, a.brillo)
    x = coro(x, a.sr)
    x = reverb(x, a.sr)
    pk = np.max(np.abs(x))
    if pk > 0:
        x *= 0.5 / pk  # el nivel final lo fija la mezcla
    sf.write(a.salida, x.astype(np.float32), a.sr, subtype='FLOAT')
    print('pad ->', a.salida)
    if a.mezclar_con:
        mezclar(a.mezclar_con, a.salida, a.salida_mezcla or a.mezclar_con, a.nivel)


if __name__ == '__main__':
    main()
