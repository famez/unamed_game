"""Piezas de la ciudad de la bahía (inspirada en Río de Janeiro): cocoteros, quioscos de
playa, sombrillas, puesto de socorrista, el empedrado de ondas del paseo, edificios del
paseo marítimo, casas de la favela con su depósito de agua, la escalera de azulejos, los
arcos con el tranvía amarillo, el teleférico, farolas, almendros de playa, barcas y la
estatua de brazos abiertos del cerro."""

import math
import random

from kit import Piece, box, cylinder, sphere, gable, pyramid, rock, mix, shade, T, RY, RX, RZ, S, mul

SAND = '#e8d3a2'
WHITE = '#f2efe8'
BLACK = '#2a2a2e'
LEAF = '#3f8a3a'


def coqueiro(name, seed, height=8.0):
    """Cocotero: tronco curvado con anillos, palmas que caen y cocos."""
    p = Piece(name)
    rnd = random.Random(seed)
    lean = rnd.uniform(0.25, 0.5)
    a = rnd.uniform(0, 6.28)
    prev = (0.0, 0.0, 0.0)
    n = 8
    for k in range(n):
        t = (k + 1) / n
        bend = lean * t * t * height * 0.5
        nxt = (math.cos(a) * bend, height * t, math.sin(a) * bend)
        p.rod(prev, nxt, 0.22 - 0.08 * t + 0.02, lambda q: '#8a7358' if int(q[1] * 3) % 2 else '#a08868', r1=0.2 - 0.08 * t)
        prev = nxt
    top = prev
    for k in range(9):
        b = k * 2 * math.pi / 9 + rnd.random() * 0.3
        rise = rnd.uniform(0.1, 0.6)
        q0 = top
        for s in range(4):
            t = (s + 1) / 4
            ang = rise - t * 1.5
            q1 = (q0[0] + math.cos(b) * math.cos(ang) * 0.95, q0[1] + math.sin(ang) * 0.95, q0[2] + math.sin(b) * math.cos(ang) * 0.95)
            p.beam(q0, q1, 0.55 - 0.1 * s, mix('#2f7a33', '#7fbf4a', t), d=0.04)
            q0 = q1
    for k in range(5):
        b = k * 1.25
        p.add(sphere(0.14, seg=8, rings=6), '#6b4a22', m=T(top[0] + math.cos(b) * 0.22, top[1] - 0.25, top[2] + math.sin(b) * 0.22))
    p.collider((-0.25, 0, -0.25), (0.25, 3.0, 0.25))
    return p


def quiosque(name, color):
    p = Piece(name)
    p.add(cylinder(2.0, 2.0, 0.2, 8, smooth=False), '#d8cfc0', 'Stone')
    p.add(cylinder(1.4, 1.4, 2.4, 8, smooth=False), WHITE, m=T(0, 0.2, 0))
    p.add(cylinder(1.45, 1.45, 0.9, 8, smooth=False), color, m=T(0, 1.0, 0))  # mostrador
    p.add(box(1.0, 0.35, 0.05), '#2a2a2e', m=mul(RY(math.pi / 8), T(0, 2.2, 1.42)))
    p.add(box(0.8, 0.2, 0.06), '#ffd23f', 'Glow', mul(RY(math.pi / 8), T(0, 2.2, 1.45)))
    p.add(cylinder(2.6, 0.2, 1.0, 8, smooth=False), lambda q: color if int(math.atan2(q[2], q[0]) / (math.pi / 4) + 8) % 2 else WHITE,
          m=T(0, 2.6, 0))
    for k in range(3):  # mesas con sombrilla delante
        a = -0.8 + k * 0.8
        x, z = math.cos(a + math.pi / 2) * 3.6, math.sin(a + math.pi / 2) * 3.6
        p.add(cylinder(0.45, 0.45, 0.04, 10), WHITE, m=T(x, 0.72, z))
        p.rod((x, 0, z), (x, 0.72, z), 0.04, WHITE)
        p.rod((x, 0, z), (x, 2.2, z), 0.025, '#d8d8d8')
        p.add(cylinder(1.0, 0.05, 0.4, 10), color if k % 2 else '#ffd23f', m=T(x, 2.0, z))
        for s in (-1, 1):
            p.box((x + s * 0.65, 0.42, z), (0.2, 0.03, 0.2), WHITE)
    p.collider((-1.45, 0, -1.45), (1.45, 2.6, 1.45))
    return p


def sombrilla(name, c1, c2, seed):
    p = Piece(name)
    rnd = random.Random(seed)
    tilt = rnd.uniform(-0.15, 0.15)
    m = RZ(tilt)
    p.rod((0, 0, 0), (0, 2.3, 0), 0.03, '#e8e8e8', m=m)
    p.add(cylinder(1.3, 0.06, 0.45, 12, smooth=False), lambda q: c1 if int(math.atan2(q[2], q[0]) / (math.pi / 6) + 12) % 2 else c2,
          m=mul(m, T(0, 1.95, 0)))
    for s in (-1, 1):  # sillas de playa
        cm = mul(T(s * 0.9, 0, 0.9), RY(rnd.uniform(-0.3, 0.3)))
        p.add(box(0.28, 0.02, 0.5), c1 if s > 0 else c2, m=mul(cm, T(0, 0.3, 0)))
        p.add(box(0.28, 0.02, 0.35), c1 if s > 0 else c2, m=mul(cm, T(0, 0.55, -0.6), RX(-0.9)))
        for x in (-0.26, 0.26):
            p.rod((x, 0, -0.45), (x, 0.3, 0.4), 0.015, '#cfcfcf', m=cm)
    p.add(box(0.5, 0.01, 0.9), rnd.choice(['#ff5a5f', '#3fb7c9', '#ffd23f', '#7a5cff']), m=T(-0.1, 0.01, -1.4))  # toalla
    return p


def sombrilla_cerrada(name, c1, seed):
    """Sombrilla cerrada y clavada en la arena (la playa vacía del atardecer detenido)."""
    p = Piece(name)
    rnd = random.Random(seed)
    m = mul(RZ(rnd.uniform(-0.12, 0.12)), RX(rnd.uniform(-0.1, 0.1)))
    p.rod((0, 0, 0), (0, 2.4, 0), 0.03, '#d8d8d8', m=m)
    p.add(cylinder(0.16, 0.05, 1.3, 8, smooth=False), lambda q: c1 if int(math.atan2(q[2], q[0]) * 2.5 + 8) % 2 else shade(c1, 0.8),
          m=mul(m, T(0, 1.05, 0)))
    p.add(cylinder(0.05, 0.0, 0.15, 6), '#d8d8d8', m=mul(m, T(0, 2.35, 0)))
    if seed % 2:  # una silla plegada al lado
        p.add(box(0.28, 0.6, 0.03), shade(c1, 0.9), m=mul(T(0.6, 0.55, 0.2), RX(0.25)))
    return p


def socorrista(name):
    p = Piece(name)
    for x in (-0.9, 0.9):
        for z in (-0.9, 0.9):
            p.rod((x, 0, z), (x * 0.8, 3.0, z * 0.8), 0.07, WHITE)
    p.box((0, 3.05, 0), (1.1, 0.08, 1.1), WHITE)
    p.box((0, 4.1, 0), (1.0, 1.0, 1.0), '#ff8a1e')
    for a in range(4):
        p.add(box(0.6, 0.35, 0.04), '#26343f', 'Glass', mul(RY(a * math.pi / 2), T(0, 4.3, 1.01)))
    p.add(gable(1.3, 1.3, 0.6), '#ff8a1e', m=T(0, 5.1, 0))
    p.rod((0, 5.6, 0), (0, 7.0, 0), 0.03, '#d8d8d8')
    p.add(box(0.5, 0.3, 0.01), '#e8322f', m=T(0.5, 6.7, 0))
    for k in range(7):  # escalera
        p.box((0, 0.2 + k * 0.42, 1.2 + (6 - k) * 0.25), (0.45, 0.03, 0.1), WHITE)
    p.collider((-1.0, 0, -1.0), (1.0, 3.1, 1.0))
    return p


def calcadao(name, w=12.0, d=6.0):
    """Empedrado de ondas blancas y negras del paseo (como el de Copacabana), por tramos."""
    p = Piece(name)
    cell = 0.25
    nz, nx = int(d / cell), int(w / cell)
    for iz in range(nz):
        z = -d / 2 + (iz + 0.5) * cell
        run_start, run_col = 0, None
        for ix in range(nx + 1):
            if ix < nx:
                x = -w / 2 + (ix + 0.5) * cell
                wave = math.sin(x * 2 * math.pi / w * 2) * 1.1
                col = BLACK if (int(math.floor((z - wave) / 1.0)) % 2) else WHITE
            else:
                col = None
            if col != run_col:
                if run_col is not None:
                    x0, x1 = -w / 2 + run_start * cell, -w / 2 + ix * cell
                    p.box(((x0 + x1) / 2, 0.02, z), ((x1 - x0) / 2, 0.02, cell / 2), run_col, 'Stone')
                run_start, run_col = ix, col
    return p


def edificio_playa(name, floors, color, seed, w=14.0, d=12.0):
    """Edificio del paseo marítimo: balcones corridos con barandilla de cristal, toldos abajo."""
    p = Piece(name)
    rnd = random.Random(seed)
    fh = 3.0
    h = floors * fh
    p.box((0, h / 2, 0), (w / 2, h / 2, d / 2), color)
    p.box((0, 2.0, 0), (w / 2 + 0.05, 2.0, d / 2 + 0.05), shade(color, 0.85))
    for k in range(4):  # tiendas abajo con toldos
        x = -w / 2 + (k + 0.5) * w / 4
        p.add(box(w / 8 - 0.2, 1.0, 0.03), '#2c3a48', 'Glass', T(x, 1.3, d / 2 + 0.06))
        p.box((x, 2.6, d / 2 + 0.6), (w / 8 - 0.1, 0.04, 0.6), rnd.choice(['#2a9d8f', '#e76f51', '#f4a261', '#264653']))
    for f in range(1, floors):  # balcones
        y = f * fh + 0.6
        p.box((0, y - 0.55, d / 2 + 0.7), (w / 2 - 0.3, 0.08, 0.7), shade(color, 1.08))
        p.add(box(w / 2 - 0.3, 0.45, 0.02), '#9fc8d8', 'Glass', T(0, y - 0.05, d / 2 + 1.38))
        for k in range(6):
            x = -w / 2 + (k + 0.5) * w / 6
            lit = rnd.random() < 0.12
            p.add(box(w / 12 - 0.3, 0.9, 0.03), '#9a6a3a' if lit else '#33424f', 'Glow' if lit else 'Glass', T(x, y + 0.4, d / 2 + 0.03))
        for side in (-1, 1):  # ventanas de los costados
            for k in range(3):
                z = -d / 2 + (k + 0.5) * d / 3
                p.add(box(0.03, 0.8, d / 8), '#33424f', 'Glass', T(side * (w / 2 + 0.02), y + 0.4, z))
    p.box((0, h + 0.5, 0), (w / 2, 0.5, d / 2), shade(color, 0.9))
    p.add(cylinder(0.9, 0.9, 1.6, 10), '#3d7fc1', m=T(w / 4, h + 1.0, -d / 4))  # depósito
    p.collider((-w / 2, 0, -d / 2), (w / 2, h + 1, d / 2))
    return p


FAVELA = ['#b5653e', '#f2c14e', '#f78e69', '#5fb49c', '#9bc1e8', '#e86a92', '#c9b79c', '#f4f1de', '#8fbf6a']


def casa_favela(name, seed):
    """Casa de la favela: dos o tres plantas de ladrillo o pintadas, depósito de agua azul,
    ventanitas, barras de hierro que asoman (para una planta más) y una escalera por fuera."""
    p = Piece(name)
    rnd = random.Random(seed)
    w, d = rnd.uniform(4.0, 5.5), rnd.uniform(4.0, 5.0)
    floors = rnd.choice([2, 2, 3, 3, 4])
    fh = 2.7
    y = 0.0
    ox = 0.0
    for f in range(floors):
        col = FAVELA[0] if rnd.random() < 0.35 else rnd.choice(FAVELA[1:])
        brick = col == FAVELA[0]
        fw = w - (0.6 if f == floors - 1 and rnd.random() < 0.5 else 0)
        c = (ox, y + fh / 2, 0)
        if brick:
            p.box(c, (fw / 2, fh / 2, d / 2), lambda q: shade(FAVELA[0], 0.88 if int(q[1] * 4) % 2 else 1.0))
        else:
            p.box(c, (fw / 2, fh / 2, d / 2), col)
        p.box((ox, y + fh - 0.06, 0), (fw / 2 + 0.06, 0.06, d / 2 + 0.06), '#9a9590', 'Stone')  # losa
        for k in range(rnd.randint(1, 2)):  # ventanas (al atardecer, muchas encendidas)
            x = ox + rnd.uniform(-fw / 3, fw / 3)
            if rnd.random() < 0.55:
                p.add(box(0.42, 0.47, 0.03), rnd.choice(['#d9a35a', '#c98a45', '#e0b070']), 'Glow', T(x, y + 1.5, d / 2 + 0.02))
            else:
                p.add(box(0.45, 0.5, 0.03), rnd.choice(['#2b2622', '#3d6fa8', '#c8202f', '#2a7d4f']), m=T(x, y + 1.5, d / 2 + 0.02))
            p.box((x, y + 0.95, d / 2 + 0.06), (0.55, 0.04, 0.08), '#d8d2c8')
        if f == 0:
            p.add(box(0.45, 1.0, 0.04), rnd.choice(['#5a3a24', '#3d6fa8', '#2a7d4f', '#c8202f']), m=T(ox + fw / 2 - 0.8, y + 1.0, d / 2 + 0.02))
        y += fh
    top = y
    p.add(cylinder(0.55, 0.6, 0.9, 10), '#2f78c4', m=T(rnd.uniform(-w / 4, w / 4), top, rnd.uniform(-d / 4, d / 4)))  # caixa d'água
    for k in range(rnd.randint(2, 5)):  # varillas que asoman
        x, z = rnd.uniform(-w / 2 + 0.2, w / 2 - 0.2), rnd.choice([-d / 2 + 0.15, d / 2 - 0.15])
        p.rod((x, top, z), (x, top + 0.8, z), 0.02, '#6b4a3a', 'Metal', seg=4)
    p.box((0, top + 0.25, d / 2 - 0.05), (w / 2, 0.25, 0.05), '#c9b79c')  # pretil
    if floors > 2:  # escalera exterior
        side = rnd.choice([-1, 1])
        for k in range(8):
            p.box((side * (w / 2 + 0.5), 0.15 + k * 0.33, -d / 2 + 0.4 + k * 0.4), (0.45, 0.05, 0.2), '#9a9590', 'Stone')
    p.add(box(0.2, 0.25, 0.01), '#d81e2c', m=T(-w / 3, 0.9, d / 2 + 0.03))  # cartel pintado
    p.collider((-w / 2, 0, -d / 2), (w / 2, top, d / 2))
    return p


TILES = ['#d81e2c', '#f2c230', '#2a9d55', '#1b6fbc', '#f4f1de', '#e8641a']


def escadaria(name, seed):
    """Tramo de escalera de azulejos de colores (como la de Selarón): 16 peldaños, 4 m de subida."""
    p = Piece(name)
    rnd = random.Random(seed)
    steps, rise, run, w = 16, 0.25, 0.5, 3.4
    for k in range(steps):
        y, z = k * rise, -steps * run / 2 + k * run
        p.box((0, y + rise / 2, z + run / 2), (w / 2, rise / 2, run / 2), '#cfc6b8', 'Stone')
        for t in range(8):  # la tabica, de azulejos
            x = -w / 2 + (t + 0.5) * w / 8
            p.add(box(w / 16 - 0.01, rise / 2 - 0.01, 0.01), rnd.choice(TILES), m=T(x, y + rise / 2, z - 0.005))
        p.collider((-w / 2, 0, z), (w / 2, y + rise, z + run))
    for s in (-1, 1):  # muretes de azulejo a los lados
        for k in range(steps):
            y, z = k * rise, -steps * run / 2 + k * run
            p.add(box(0.12, 0.45, run / 2), rnd.choice(TILES), m=T(s * (w / 2 + 0.12), y + 0.45 + rise, z + run / 2))
    return p


def arcos(name):
    """Tramo de los arcos (acueducto blanco de dos pisos) con la vía del tranvía arriba."""
    p = Piece(name)
    span, n = 6.0, 5
    L = span * n
    for tier, (y0, ah, pw) in enumerate(((0.0, 8.0, 1.4), (8.6, 6.0, 1.0))):
        for k in range(n + 1):
            x = -L / 2 + k * span
            p.box((x, y0 + ah / 2, 0), (pw / 2, ah / 2, 1.6), WHITE, 'Stone')
        for k in range(n):  # el arco: dovelas en semicírculo
            cx = -L / 2 + (k + 0.5) * span
            r = span / 2 - pw / 2
            for j in range(9):
                a = math.pi * j / 8
                p.add(box(0.35, 0.35, 1.6), WHITE, 'Stone', mul(T(cx + math.cos(a) * r, y0 + ah - r * 0.4 + math.sin(a) * r * 0.75, 0), RZ(a)))
        p.box((0, y0 + ah + 0.3, 0), (L / 2 + 0.6, 0.3, 1.7), WHITE, 'Stone')
    top = 8.6 + 6.0 + 0.6
    for z in (-0.5, 0.5):
        p.box((0, top + 0.05, z), (L / 2, 0.05, 0.05), '#8a8d96', 'Metal')
    for x in [-L / 2 + k * 4 for k in range(int(L / 4) + 1)]:
        p.rod((x, top, 1.5), (x, top + 4.5, 1.5), 0.05, '#5d6070', 'Metal')
    p.box((0, top + 4.4, 0.8), (L / 2, 0.02, 0.02), '#2a2a2e', 'Metal')
    for k in range(n + 1):
        x = -L / 2 + k * span
        p.collider((x - 0.7, 0, -1.6), (x + 0.7, 8.0, 1.6))
    p.collider((-L / 2 - 0.6, top - 0.6, -1.7), (L / 2 + 0.6, top, 1.7))
    return p


def bonde(name):
    """Tranvía amarillo abierto (el «bondinho»)."""
    p = Piece(name)
    y = '#f2c230'
    p.box((0, 0.65, 0), (1.1, 0.25, 4.0), '#2a4a7a')
    p.box((0, 1.0, 0), (1.15, 0.12, 4.05), y)
    for z in [-3.2 + k * 1.6 for k in range(5)]:
        p.box((0, 1.25, z), (1.05, 0.06, 0.25), '#8a5a34')
        p.box((0, 1.6, z - 0.2), (1.05, 0.3, 0.04), '#8a5a34')
    for x in (-1.1, 1.1):
        for z in [-3.9 + k * 1.95 for k in range(5)]:
            p.box((x, 2.0, z), (0.05, 0.9, 0.05), y)
    p.box((0, 2.95, 0), (1.25, 0.08, 4.15), y)
    p.add(gable(4.15, 1.25, 0.25), '#d8d2c8', m=mul(T(0, 3.03, 0), RY(math.pi / 2)))
    p.rod((0, 3.2, -1.0), (0, 4.4, 0.5), 0.03, BLACK, 'Metal', seg=4)
    for z in (-2.6, 2.6):
        for x in (-0.75, 0.75):
            p.add(cylinder(0.32, 0.32, 0.12, 12), '#2a2a2e', 'Metal', mul(T(x, 0.32, z), RZ(math.pi / 2), T(0, -0.06, 0)))
    p.add(cylinder(0.12, 0.12, 0.05, 10), '#fff1c8', 'Glow', mul(T(0, 1.5, 4.08), RX(math.pi / 2)))
    p.add(box(0.5, 0.15, 0.02), '#2a4a7a', m=T(0, 2.75, 4.18))
    p.collider((-1.2, 0, -4.1), (1.2, 3.0, 4.1))
    return p


def teleferico(name):
    p = Piece(name)
    p.box((0, 1.1, 0), (1.3, 1.0, 1.9), '#e8e4dc')
    p.add(box(1.31, 0.45, 1.6), '#2c3e50', 'Glass', T(0, 1.4, 0))
    p.add(box(1.25, 0.45, 1.91), '#2c3e50', 'Glass', T(0, 1.4, 0))
    p.box((0, 2.2, 0), (1.4, 0.12, 2.0), '#d8402a')
    p.rod((0, 2.3, 0), (0, 3.6, 0), 0.08, '#5d6070', 'Metal')
    p.box((0, 3.7, 0), (0.25, 0.15, 0.9), '#5d6070', 'Metal')
    return p


def cristo(name):
    """Estatua de brazos abiertos sobre su pedestal (Art Déco, piedra clara), en lo alto del cerro."""
    p = Piece(name)
    stone = '#dcd7cc'
    p.box((0, 4.0, 0), (4.0, 4.0, 4.0), shade(stone, 0.92), 'Stone')
    p.box((0, 8.2, 0), (3.3, 0.25, 3.3), shade(stone, 0.85), 'Stone')
    base = 8.45
    # Túnica: cono alargado con pliegues verticales.
    p.add(cylinder(2.2, 1.35, 13.0, 16, smooth=False), lambda q: shade(stone, 0.93 if int((math.atan2(q[2], q[0]) + 4) * 4) % 2 else 1.0),
          'Stone', T(0, base, 0))
    p.add(cylinder(1.35, 1.6, 2.5, 16), stone, 'Stone', T(0, base + 13.0, 0))  # pecho
    # Brazos abiertos en cruz, con las mangas que caen.
    for s in (-1, 1):
        p.beam((s * 1.2, base + 14.8, 0), (s * 13.5, base + 14.6, 0), 1.25, stone, 'Stone', d=1.1)
        p.add(box(1.8, 1.2, 0.5), shade(stone, 0.95), 'Stone', T(s * 5.0, base + 13.9, 0))
        p.add(sphere(0.65, 0.5, 0.55, 10, 6), stone, 'Stone', T(s * 13.9, base + 14.6, 0))
    p.add(cylinder(0.55, 0.55, 0.7, 12), stone, 'Stone', T(0, base + 15.4, 0))
    p.add(sphere(1.05, 1.3, 1.05, 14, 10), stone, 'Stone', T(0, base + 17.0, 0))
    p.add(sphere(1.12, 0.9, 1.12, 14, 8), shade(stone, 0.85), 'Stone', T(0, base + 17.5, -0.12))  # pelo
    p.collider((-4, 0, -4), (4, 8.45, 4))
    p.collider((-2.0, 8.45, -2.0), (2.0, base + 15, 2.0))
    p.light((0, 2.0, 4.6), '#FFE9C8')  # foco que la alumbra
    return p


def farola_rio(name):
    p = Piece(name)
    p.add(cylinder(0.16, 0.1, 6.0, 10), '#2f6f6a', 'Metal')
    for s in (-1, 1):  # doble brazo curvo (como las del paseo)
        prev = (0, 5.8, 0)
        for k in range(4):
            t = (k + 1) / 4
            nxt = (s * 1.3 * math.sin(t * math.pi / 2), 5.8 + 0.6 * math.sin(t * math.pi), 0)
            p.rod(prev, nxt, 0.05, '#2f6f6a', 'Metal', seg=6)
            prev = nxt
        p.add(sphere(0.28, 0.22, 0.28, 10, 6), '#fff4d8', 'Glow', T(s * 1.3, 5.6, 0))
        p.light((s * 1.3, 5.55, 0), '#FFEFC8')
    p.collider((-0.18, 0, -0.18), (0.18, 6.0, 0.18))
    return p


def almendro(name, seed):
    """Almendro de playa: copa ancha en pisos horizontales, hojas grandes (algunas rojizas)."""
    p = Piece(name)
    rnd = random.Random(seed)
    p.add(cylinder(0.3, 0.2, 3.0, 9), '#5e4a3a')
    for tier, (y, r) in enumerate(((3.0, 3.2), (4.2, 2.5), (5.2, 1.6))):
        for k in range(5):
            a = k * 1.26 + tier
            p.rod((0, y - 0.6, 0), (math.cos(a) * r * 0.7, y, math.sin(a) * r * 0.7), 0.08, '#5e4a3a', r1=0.04)
        top = '#c0512f' if rnd.random() < 0.3 else '#5fa846'  # alguna hoja ya roja
        p.add(sphere(r, 0.45, r, 12, 6), lambda q, top=top, r=r: mix('#2f7a33', top, 0.25 + 0.5 * max(0, q[1]) / 0.45),
              m=T(0, y + 0.2, 0))
    p.collider((-0.3, 0, -0.3), (0.3, 3.0, 0.3))
    return p


def barca(name, color):
    p = Piece(name)
    for k in range(5):
        z = -2.0 + k
        wdt = 0.95 - abs(k - 2) * 0.22
        p.box((0, 0.4, z * 0.95), (wdt, 0.35, 0.5), color)
        p.box((0, 0.78, z * 0.95), (wdt + 0.04, 0.04, 0.52), WHITE)
    p.box((0, 0.5, 0), (0.75, 0.05, 1.6), '#8a5a34')
    p.box((0, 0.55, 0.3), (0.75, 0.05, 0.12), '#8a5a34')
    p.rod((0.3, 0.6, -0.3), (0.9, 0.6, 2.5), 0.03, '#a07a50', seg=5)
    p.collider((-0.95, 0, -2.3), (0.95, 0.8, 2.3))
    return p


def piezas():
    out = [coqueiro('cocotero', 1), coqueiro('cocotero_alto', 2, 10.5), quiosque('quiosco_playa', '#2a9d55'),
           quiosque('quiosco_playa_azul', '#1b6fbc'), sombrilla('sombrilla_playa', '#ff5a5f', '#f4f1de', 1),
           sombrilla('sombrilla_playa_amarilla', '#ffd23f', '#1b6fbc', 2), sombrilla_cerrada('sombrilla_cerrada', '#c8202f', 1),
           sombrilla_cerrada('sombrilla_cerrada_azul', '#1b6fbc', 2), socorrista('puesto_socorrista'), calcadao('paseo_ondas'),
           edificio_playa('edificio_playa_blanco', 14, '#f2efe8', 1), edificio_playa('edificio_playa_crema', 11, '#e9dcc0', 2),
           edificio_playa('edificio_playa_rosa', 16, '#f1c9c3', 3), edificio_playa('edificio_playa_azul', 12, '#c9dbe6', 4, 18, 12),
           escadaria('escalera_azulejos', 1), arcos('arcos'), bonde('tranvia_amarillo'), teleferico('teleferico'),
           cristo('estatua_cerro'), farola_rio('farola_paseo'), almendro('almendro_playa', 1), barca('barca', '#2a7fc4'),
           barca('barca_roja', '#c8202f')]
    out += [casa_favela('casa_favela_%d' % (i + 1), 100 + i * 7) for i in range(8)]
    return out
