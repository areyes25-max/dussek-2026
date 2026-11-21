"""animar_personaje.py - anima la ilustracion del pianista sincronizada con el MIDI.

Uso:
  py animar_personaje.py --imagen personaje.png --geom personaje.json --midi pieza.mid
                         --tira pieza_deco.mp4 --audio pieza.wav --salida pieza_personaje.mp4 [--fps 30]

Que hace (todo calculado desde el MIDI, sin IA y gratis):
  - cabeza: asiente con los acentos y se mece despacio con la frase
  - cuerpo: respira y se inclina hacia el piano cuando la musica sube de intensidad
  - manos: la izquierda sigue los graves y la derecha los agudos; los dedos "golpean" en cada nota
  - teclas del piano ilustrado: brillan donde caen las notas; chispas doradas suben en cada ataque
  - abajo: la tira de teclado dorada de Dussek (--tira), exacta nota por nota
Las coordenadas del personaje (cabeza, cuello, manos...) estan en el JSON de geometria, en pixeles
de la imagen original: si cambias la ilustracion, ajusta ese archivo.
"""
import argparse
import json
import math
import subprocess

import cv2
import mido
import numpy as np


def eventos(path):
    """Notas (inicio, fin, nota, velocidad) sin pedal: cuando la tecla esta abajo."""
    t, abiertas, notas = 0.0, {}, []
    for msg in mido.MidiFile(path):
        t += msg.time
        if msg.type == 'note_on' and msg.velocity > 0:
            abiertas[msg.note] = (t, msg.velocity)
        elif msg.type in ('note_off', 'note_on') and msg.note in abiertas:
            t0, v = abiertas.pop(msg.note)
            notas.append((t0, t, msg.note, v))
    for n, (t0, v) in abiertas.items():
        notas.append((t0, t, n, v))
    notas.sort()
    return notas, t


def mascara(shape, centro, ejes, angulo, suavizado):
    m = np.zeros(shape, np.float32)
    cv2.ellipse(m, (int(centro[0]), int(centro[1])), (int(ejes[0]), int(ejes[1])), angulo, 0, 360, 1.0, -1)
    return cv2.GaussianBlur(m, (0, 0), suavizado)


def rotacion(xx, yy, pivote, ang):
    """Desplazamiento de una rotacion de `ang` radianes alrededor de `pivote`."""
    c, s = math.cos(ang), math.sin(ang)
    dx, dy = xx - pivote[0], yy - pivote[1]
    return (c * dx - s * dy) - dx, (s * dx + c * dy) - dy


class Curvas:
    """Senales suaves por instante calculadas desde las notas."""

    def __init__(self, notas, corte):
        self.notas = notas
        self.corte = corte
        self.onsets = np.array([n[0] for n in notas]) if notas else np.zeros(0)
        self.vel = np.array([n[3] / 127 for n in notas]) if notas else np.zeros(0)
        self.alt = np.array([n[2] for n in notas]) if notas else np.zeros(0)

    def acento(self, t, tau=0.35):
        """Suma de ataques recientes, decayendo (0..~1)."""
        d = t - self.onsets
        m = (d >= 0) & (d < 5 * tau)
        return float(np.sum(self.vel[m] * np.exp(-d[m] / tau)))

    def intensidad(self, t, ventana=2.0):
        """Energia media de las ultimas `ventana` s (0..1)."""
        d = t - self.onsets
        m = (d >= 0) & (d < ventana)
        return float(min(1.0, np.sum(self.vel[m]) / (ventana * 4.0)))

    def golpes(self, t, mano, tau=0.09):
        """Golpe de dedos reciente de una mano (0..1)."""
        d = t - self.onsets
        lado = (self.alt < self.corte) if mano == 'izq' else (self.alt >= self.corte)
        m = lado & (d >= 0) & (d < 5 * tau)
        return float(min(1.0, np.sum(self.vel[m] * np.exp(-d[m] / tau))))

    def altura_media(self, t, mano, ventana=1.2):
        d = t - self.onsets
        lado = (self.alt < self.corte) if mano == 'izq' else (self.alt >= self.corte)
        m = lado & (d >= 0) & (d < ventana)
        if not np.any(m):
            return None
        w = np.exp(-d[m] / 0.5)
        return float(np.sum(self.alt[m] * w) / np.sum(w))

    def sonando(self, t):
        return [(n, v, t - t0) for (t0, t1, n, v) in self.notas if t0 <= t < t1 + 0.25]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--imagen', required=True)
    ap.add_argument('--geom', required=True)
    ap.add_argument('--midi', required=True)
    ap.add_argument('--tira', default=None, help='video Deco de Dussek (1920x1080) para la tira de teclado')
    ap.add_argument('--audio', required=True)
    ap.add_argument('--salida', required=True)
    ap.add_argument('--fps', type=float, default=30.0)
    ap.add_argument('--ancho', type=int, default=1920)
    ap.add_argument('--alto', type=int, default=1080)
    a = ap.parse_args()

    g = json.load(open(a.geom, encoding='utf-8'))
    img = cv2.imread(a.imagen, cv2.IMREAD_COLOR)
    if img is None:
        raise SystemExit('No pude abrir la imagen ' + a.imagen)
    H, W = img.shape[:2]
    if 'quitar' in g:  # zonas a borrar de la ilustracion (p. ej. una mano de mas)
        m = np.zeros((H, W), np.uint8)
        for q in g['quitar']:
            cv2.ellipse(m, tuple(q['centro']), tuple(q['ejes']), q.get('angulo', 0), 0, 360, 255, -1)
        img = cv2.inpaint(img, m, 21, cv2.INPAINT_TELEA)
    base = img.astype(np.float32)
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)

    k = g.get('escala_mascaras', 1.0)
    mc = mascara((H, W), g['cabeza']['centro'], g['cabeza']['ejes'], 0, 45 * k)
    mb = mascara((H, W), g['cuerpo']['centro'], g['cuerpo']['ejes'], 0, 60 * k)
    mi = mascara((H, W), g['mano_izq']['centro'], g['mano_izq']['ejes'], g['mano_izq'].get('angulo', 0), 18 * k)
    md = mascara((H, W), g['mano_der']['centro'], g['mano_der']['ejes'], g['mano_der'].get('angulo', 0), 18 * k)
    di = mascara((H, W), g['dedos_izq']['centro'], g['dedos_izq']['ejes'], 0, 10 * k)
    dd = mascara((H, W), g['dedos_der']['centro'], g['dedos_der']['ejes'], 0, 10 * k)
    mb = np.clip(mb - 0.6 * (mi + md), 0, 1)  # el cuerpo no arrastra las manos (estan sobre las teclas)
    eje = np.array(g['eje_teclado'], np.float32)
    eje /= np.linalg.norm(eje)
    pulsar = np.array(g['direccion_pulsar'], np.float32)
    pulsar /= np.linalg.norm(pulsar)

    notas, fin = eventos(a.midi)
    cv = Curvas(notas, g.get('corte_manos', 60))
    dur = fin + 4.0
    nfr = int(dur * a.fps)

    # encuadre: cubrir el lienzo y centrar en el personaje y el piano
    esc = max(a.ancho / W, a.alto / H)
    Ws, Hs = int(round(W * esc)), int(round(H * esc))
    cx = int(g.get('centro_encuadre_x', W / 2) * esc)
    x0 = min(max(0, cx - a.ancho // 2), Ws - a.ancho)
    y0 = (Hs - a.alto) // 2
    tira_y = int(a.alto * 0.805)

    rng = np.random.default_rng(1)
    chispas = []  # [x, y, vx, vy, vida, brillo]
    ya = set()
    suav = {'izq': 0.0, 'der': 0.0, 'lean': 0.0, 'nod': 0.0}

    cmd = ['ffmpeg', '-y', '-hide_banner', '-loglevel', 'error', '-f', 'rawvideo', '-pix_fmt', 'bgr24',
           '-s', f'{a.ancho}x{a.alto}', '-r', str(a.fps), '-i', '-']
    if a.tira:
        cmd += ['-i', a.tira]
    cmd += ['-i', a.audio]
    if a.tira:
        h_tira = a.alto - tira_y
        cmd += ['-filter_complex', f'[1:v]scale={a.ancho}:{a.alto},crop={a.ancho}:{h_tira}:0:{a.alto - h_tira}[k];[0:v][k]overlay=0:{tira_y}:shortest=1[v]',
                '-map', '[v]', '-map', '2:a:0']
    else:
        cmd += ['-map', '0:v:0', '-map', '1:a:0']
    cmd += ['-c:v', 'libx264', '-preset', 'medium', '-crf', '19', '-pix_fmt', 'yuv420p', '-c:a', 'aac', '-b:a', '256k',
            '-shortest', '-movflags', '+faststart', a.salida]
    ff = subprocess.Popen(cmd, stdin=subprocess.PIPE)

    oscuro = np.ones((a.alto, 1), np.float32)
    ramp = np.clip((np.arange(a.alto) - (tira_y - 140)) / 140.0, 0, 1)
    oscuro[:, 0] = 1 - 0.85 * ramp  # oscurece hacia la tira para que se funda

    for f in range(nfr):
        t = f / a.fps
        # senales suavizadas
        inten = cv.intensidad(t)
        acc = cv.acento(t)
        suav['lean'] += (inten - suav['lean']) * 0.06
        suav['nod'] += (min(1.0, acc) - suav['nod']) * 0.25
        for mano, base_alt in (('izq', 48), ('der', 72)):
            m = cv.altura_media(t, mano)
            objetivo = 0.0 if m is None else np.clip((m - base_alt) / 12.0, -1.2, 1.2)
            suav[mano] += (objetivo - suav[mano]) * 0.08

        dx = np.zeros((H, W), np.float32)
        dy = np.zeros((H, W), np.float32)
        # cuerpo: respiracion + inclinacion hacia el piano con la intensidad + vaiven lento
        ang_b = math.radians(0.35 * math.sin(2 * math.pi * t / 6.1) + 1.1 * suav['lean'])
        rx, ry = rotacion(xx, yy, g['caderas'], ang_b)
        resp = 0.005 * math.sin(2 * math.pi * t / 4.2)
        dx += mb * rx
        dy += mb * (ry - resp * (yy - g['caderas'][1]))
        # cabeza: asiente con los acentos y acompana el vaiven
        ang_c = math.radians(1.6 * suav['nod'] + 0.5 * math.sin(2 * math.pi * t / 5.3 + 1.0) + 0.6 * suav['lean'])
        rx, ry = rotacion(xx, yy, g['cuello'], ang_c)
        dx += mc * (rx + 0.6 * ang_b * 0)  # (el cuerpo ya la arrastra via mb)
        dy += mc * ry
        # manos: se desplazan por el teclado y los dedos pulsan
        for mano, mm, mdd in (('izq', mi, di), ('der', md, dd)):
            desp = g.get('recorrido_manos', 9.0) * suav[mano]
            golpe = cv.golpes(t, mano) * g.get('profundidad_pulsar', 4.0)
            dx += mm * eje[0] * desp + mdd * pulsar[0] * golpe
            dy += mm * eje[1] * desp + mdd * pulsar[1] * golpe
        # mapa inverso (aproximado): muestrear desde p - D(p)
        frame = cv2.remap(base, xx - dx, yy - dy, cv2.INTER_LINEAR, borderMode=cv2.BORDER_REFLECT)

        # luz: teclas ilustradas y chispas
        luz = np.zeros((H, W, 3), np.float32)
        oro = np.array([60, 170, 255], np.float32)  # BGR
        for (n, v, edad) in cv.sonando(t):
            mano = 'izq' if n < cv.corte else 'der'
            p = np.array(g['teclas_' + mano], np.float32) + eje * (n - (48 if mano == 'izq' else 72)) * g.get('px_por_tecla', 3.2)
            fuerza = (v / 127) * (0.55 + 0.45 * math.exp(-edad / 0.3))
            cv2.circle(luz, (int(p[0]), int(p[1])), int(9 + 10 * fuerza), (oro * fuerza).tolist(), -1, cv2.LINE_AA)
        for i, (t0, t1, n, v) in enumerate(notas):
            if t0 <= t < t0 + 1.0 / a.fps and i not in ya:
                ya.add(i)
                mano = 'izq' if n < cv.corte else 'der'
                p = np.array(g['teclas_' + mano], np.float32) + eje * (n - (48 if mano == 'izq' else 72)) * g.get('px_por_tecla', 3.2)
                for _ in range(1 + int(3 * v / 127)):
                    chispas.append([p[0], p[1], rng.normal(0, 18), -rng.uniform(40, 110), 1.0, 0.6 + 0.4 * v / 127])
        vivas = []
        for c in chispas:
            c[0] += c[2] / a.fps
            c[1] += c[3] / a.fps
            c[3] += 25 / a.fps
            c[4] -= 1 / (1.4 * a.fps)
            if c[4] > 0:
                cv2.circle(luz, (int(c[0]), int(c[1])), 3, (oro * c[4] * c[5] * 1.6).tolist(), -1, cv2.LINE_AA)
                vivas.append(c)
        chispas = vivas
        luz = cv2.GaussianBlur(luz, (0, 0), 7) * 1.6 + cv2.GaussianBlur(luz, (0, 0), 2) * 0.6
        frame = frame * (1.0 + 0.06 * suav['lean']) + luz

        frame = np.clip(frame, 0, 255).astype(np.uint8)
        frame = cv2.resize(frame, (Ws, Hs), interpolation=cv2.INTER_CUBIC)[y0:y0 + a.alto, x0:x0 + a.ancho]
        if a.tira:
            frame = (frame.astype(np.float32) * oscuro[:, :, None]).astype(np.uint8)
        try:
            ff.stdin.write(frame.tobytes())
        except BrokenPipeError:  # la tira o el audio terminaron antes: el video ya esta completo
            break
        if f % int(a.fps * 10) == 0:
            print(f'  {t:6.1f} / {dur:.1f} s', flush=True)
    try:
        ff.stdin.close()
    except BrokenPipeError:
        pass
    if ff.wait() != 0:
        raise SystemExit('ffmpeg fallo')
    print('video ->', a.salida)


if __name__ == '__main__':
    main()
