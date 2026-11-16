"""Piezas de la ciudad neón (inspirada en Tokio): torres con ventanas encendidas, tiendas,
letreros verticales, pantallas gigantes, máquinas expendedoras, farolas, semáforos, vía
elevada, puestos de comida con farolillos, torii, cerezos y coches."""

import math
import random

from kit import Piece, box, cylinder, sphere, gable, pyramid, mix, shade, T, RY, RX, RZ, S, mul

CONCRETE = ['#3c404b', '#454956', '#353843', '#4b4852', '#3b3747']
DARK = '#24262e'
NEON = {'rosa': '#ff3fa4', 'cian': '#2ee6ff', 'amarillo': '#ffd23f', 'violeta': '#9a5cff', 'verde': '#4dff9a',
        'rojo': '#ff4a3d', 'naranja': '#ff8a2a'}
WARM = ['#c98a45', '#b9925e', '#d9a35a', '#6e9fc0', '#a8763e']  # ventanas encendidas (el bloom ya las aclara)
GLASS = '#2a3346'


def glyph(p, m, w, h, color, rnd):
    """Un «carácter» abstracto de neón (trazos rectos en una celda): se lee como letras sin serlo."""
    strokes = rnd.randint(3, 5)
    for _ in range(strokes):
        if rnd.random() < 0.5:  # horizontal
            y = (rnd.randint(0, 4) / 4 - 0.5) * h * 0.8
            x0, x1 = sorted((rnd.random() - 0.5) * w * 0.9 for _ in range(2))
            if x1 - x0 < w * 0.25:
                x1 = x0 + w * 0.4
            p.add(box((x1 - x0) / 2, h * 0.045, 0.03), color, 'Glow', mul(m, T((x0 + x1) / 2, y, 0)))
        else:  # vertical
            x = (rnd.randint(0, 4) / 4 - 0.5) * w * 0.8
            y0, y1 = sorted((rnd.random() - 0.5) * h * 0.9 for _ in range(2))
            if y1 - y0 < h * 0.25:
                y1 = y0 + h * 0.4
            p.add(box(w * 0.045, (y1 - y0) / 2, 0.03), color, 'Glow', mul(m, T(x, (y0 + y1) / 2, 0)))


def vertical_sign(p, m, height, color, seed, glyphs=None):
    """Letrero vertical: caja oscura con borde de neón y caracteres apilados por las dos caras."""
    rnd = random.Random(seed)
    w = 0.9
    p.add(box(w / 2, height / 2, 0.18), DARK, m=mul(m, T(0, height / 2, 0)))
    for z in (0.19, -0.19):
        for x in (-w / 2, w / 2):
            p.add(box(0.03, height / 2, 0.02), color, 'Glow', mul(m, T(x, height / 2, z)))
        n = glyphs or max(2, int(height / 1.1))
        for i in range(n):
            y = height * (i + 0.5) / n
            gm = mul(m, T(0, y, z), RY(0 if z > 0 else math.pi))
            glyph(p, gm, w * 0.7, height / n * 0.75, color if i % 2 else mix(color, '#ffffff', 0.4), rnd)


def torre(name, w, d, floors, seed, roof_sign=None, screen=False, setback=False):
    p = Piece(name)
    rnd = random.Random(seed)
    fh = 3.4
    h = floors * fh
    body = CONCRETE[seed % len(CONCRETE)]
    p.box((0, h / 2, 0), (w / 2, h / 2, d / 2), lambda q: shade(body, 0.85 + 0.25 * (q[1] + h / 2) / h))
    p.box((0, 0.6, 0), (w / 2 + 0.05, 0.6, d / 2 + 0.05), shade(body, 0.7))  # zócalo
    # Ventanas: por planta y fachada, tiras encendidas (cálidas o frías) o apagadas.
    lit_color = WARM[seed % len(WARM)]
    for f in range(1, floors):
        y = f * fh + fh * 0.55
        for face in range(4):
            a = face * math.pi / 2
            half = (w if face % 2 == 0 else d) / 2
            depth = (d if face % 2 == 0 else w) / 2
            n = max(1, int(half * 2 / 2.2))
            for k in range(n):
                x = -half + (k + 0.5) * (half * 2 / n)
                m = mul(RY(a), T(x, y, depth + 0.02))
                lit = rnd.random() < 0.4
                c = lit_color if rnd.random() < 0.7 else rnd.choice(WARM)
                p.add(box(half / n * 0.6, fh * 0.22, 0.03), c if lit else GLASS, 'Glow' if lit else 'Glass', m)
        p.box((0, f * fh, 0), (w / 2 + 0.06, 0.08, d / 2 + 0.06), shade(body, 1.15))  # forjado
    top = h
    if setback:  # un cuerpo más estrecho encima
        sw, sd, sh = w * 0.65, d * 0.65, fh * 4
        p.box((0, h + sh / 2, 0), (sw / 2, sh / 2, sd / 2), shade(body, 1.05))
        for f in range(4):
            for face in range(4):
                a = face * math.pi / 2
                half = (sw if face % 2 == 0 else sd) / 2
                depth = (sd if face % 2 == 0 else sw) / 2
                p.add(box(half * 0.7, fh * 0.2, 0.03), lit_color, 'Glow', mul(RY(a), T(0, h + f * fh + fh * 0.55, depth + 0.02)))
        top = h + sh
    # Azotea: pretil, máquinas de aire, depósito y antena con la luz roja.
    p.box((0, top + 0.5, d / 2 * 0.98 if not setback else 0), (w / 2 if not setback else 0.01, 0.5, 0.1), shade(body, 0.9))
    for k in range(3):
        p.box((rnd.uniform(-w / 3, w / 3), top + 0.5, rnd.uniform(-d / 3, d / 3)), (0.7, 0.5, 0.5), '#8a8d96', 'Metal')
    p.add(cylinder(0.9, 0.9, 1.8, 10), '#6d717c', 'Metal', T(-w / 4 if not setback else 0, top, d / 5))
    p.rod((w / 5, top, -d / 5), (w / 5, top + 6, -d / 5), 0.07, '#9a9da6', 'Metal')
    p.add(sphere(0.18, seg=8, rings=6), '#ff3030', 'Glow', T(w / 5, top + 6.1, -d / 5))
    # Banda de neón en lo alto de la fachada principal.
    neon = list(NEON.values())[seed % len(NEON)]
    p.box((0, h - 0.4, d / 2 + 0.06), (w / 2 - 0.3, 0.07, 0.05), neon, 'Glow')
    # Letrero vertical en la esquina.
    vertical_sign(p, mul(T(w / 2 + 0.55, 4.0, d / 2 - 1.0), RY(math.pi / 2)), min(h - 6, 12), neon, seed)
    if roof_sign:  # cartel sobre la azotea
        sw = w * 0.8
        p.box((0, top + 3.0, 0), (sw / 2, 1.3, 0.12), DARK)
        for i in range(4):
            gm = mul(T(-sw / 2 + sw * (i + 0.5) / 4, top + 3.0, 0.13))
            glyph(p, gm, sw / 4 * 0.8, 2.0, roof_sign, rnd)
        for x in (-sw / 3, sw / 3):
            p.beam((x, top, 0), (x, top + 1.7, 0), 0.12, '#6d717c', 'Metal')
    if screen:  # pantalla gigante en la fachada
        sw2, sh2 = w * 0.7, min(h * 0.35, 9)
        y = h * 0.55
        p.box((0, y, d / 2 + 0.2), (sw2 / 2 + 0.25, sh2 / 2 + 0.25, 0.15), DARK)
        p.add(box(sw2 / 2, sh2 / 2, 0.05), lambda q: shade(mix('#c0307e', '#1fa0b8', 0.5 + q[1] / sh2), 0.8) if int((q[1] + sh2) * 3) % 4
              else '#7fd0e0', 'Glow', T(0, y, d / 2 + 0.36))
    # Planta baja: escaparate encendido.
    p.add(box(w / 2 * 0.8, 1.1, 0.03), lit_color, 'Glow', T(0, 1.6, d / 2 + 0.06))
    p.box((0, 2.95, d / 2 + 0.6), (w / 2 * 0.85, 0.06, 0.6), neon, 'Cloth')  # marquesina
    p.collider((-w / 2, 0, -d / 2), (w / 2, top, d / 2))
    return p


def tienda(name, color, seed, floors=2):
    """Tienda de dos plantas: escaparate, toldo, cortinilla noren, cartel y aparatos de aire."""
    p = Piece(name)
    rnd = random.Random(seed)
    w, d, fh = 6.0, 8.0, 3.3
    h = fh * floors
    body = rnd.choice(['#5b5560', '#6a6470', '#4f5a62', '#62584f'])
    p.box((0, h / 2, 0), (w / 2, h / 2, d / 2), body)
    p.add(box(w / 2 - 0.4, 1.15, 0.03), mix(color, '#d9b07a', 0.6), 'Glow', T(0, 1.4, d / 2 + 0.02))  # escaparate
    p.box((0, 2.75, d / 2 + 0.55), (w / 2, 0.05, 0.6), color)  # toldo
    for k in range(5):  # noren (cortinilla de tela)
        p.add(box(0.26, 0.35, 0.02), color if k % 2 else shade(color, 0.8), m=T(-1.0 + k * 0.5, 2.2, d / 2 + 0.1))
    p.add(box(w / 2 - 0.2, 0.45, 0.12), DARK, m=T(0, 3.3, d / 2 + 0.12))  # cartel
    glyph(p, T(-1.2, 3.3, d / 2 + 0.25), 1.0, 0.7, color, rnd)
    glyph(p, T(0, 3.3, d / 2 + 0.25), 1.0, 0.7, color, rnd)
    glyph(p, T(1.2, 3.3, d / 2 + 0.25), 1.0, 0.7, color, rnd)
    for f in range(1, floors):
        y = f * fh + 1.6
        for x in (-1.5, 1.5):
            lit = rnd.random() < 0.7
            p.add(box(0.8, 0.6, 0.03), rnd.choice(WARM) if lit else GLASS, 'Glow' if lit else 'Glass', T(x, y, d / 2 + 0.02))
        p.box((2.4, y - 0.9, d / 2 + 0.35), (0.4, 0.3, 0.3), '#c9ccd2', 'Metal')  # aire acondicionado
    p.box((0, h + 0.3, 0), (w / 2 + 0.1, 0.3, d / 2 + 0.1), shade(body, 0.8))
    vertical_sign(p, mul(T(-w / 2 - 0.5, 3.6, d / 2 - 0.7), RY(math.pi / 2)), h - 1.0, color, seed + 7)
    p.collider((-w / 2, 0, -d / 2), (w / 2, h + 0.6, d / 2))
    return p


def letrero(name, color, seed, height=7.0):
    p = Piece(name)
    p.box((0, 0.9, 0), (0.12, 0.9, 0.12), '#6d717c', 'Metal')
    vertical_sign(p, T(0, 1.8, 0), height, color, seed)
    p.collider((-0.5, 0, -0.25), (0.5, 1.8 + height, 0.25))
    return p


def pantalla(name):
    p = Piece(name)
    w, h = 10.0, 6.0
    for x in (-3.5, 3.5):
        p.box((x, 3.0, -0.3), (0.25, 3.0, 0.25), '#5d6070', 'Metal')
    p.box((0, 6.0 + h / 2, 0), (w / 2 + 0.3, h / 2 + 0.3, 0.25), DARK)
    p.add(box(w / 2, h / 2, 0.04), lambda q: shade(mix('#7040c0', '#1fa0b8', 0.5 + q[0] / w), 0.85) if int((q[1] + h) * 2.5) % 5
          else '#80d8e8', 'Glow', T(0, 6.0 + h / 2, 0.27))
    p.collider((-3.8, 0, -0.6), (3.8, 6.0, 0.1))
    return p


def expendedora(name, color):
    p = Piece(name)
    p.box((0, 0.95, 0), (0.5, 0.95, 0.4), color)
    p.add(box(0.42, 0.55, 0.02), '#f6f8ff', 'Glow', T(0, 1.25, 0.41))
    rnd = random.Random(len(name))
    for row in range(3):
        for col in range(5):
            c = rnd.choice(['#e8433a', '#3a8ee8', '#f2c230', '#36b36a', '#ffffff', '#ff8a2a'])
            p.add(box(0.05, 0.1, 0.03), c, 'Cloth', T(-0.32 + col * 0.16, 0.95 + row * 0.27, 0.43))
    p.box((0.3, 0.45, 0.42), (0.1, 0.12, 0.02), '#1a1c22')
    p.box((-0.1, 0.25, 0.42), (0.3, 0.08, 0.03), '#1a1c22')
    p.collider((-0.5, 0, -0.4), (0.5, 1.9, 0.4))
    return p


def farola_led(name):
    p = Piece(name)
    p.add(cylinder(0.11, 0.07, 6.5, 8), '#8a8e98', 'Metal')
    p.beam((0, 6.4, 0), (0, 6.6, 1.5), 0.09, '#8a8e98', 'Metal')
    p.add(box(0.18, 0.06, 0.35), '#5d6070', 'Metal', T(0, 6.62, 1.6))
    p.add(box(0.15, 0.02, 0.3), '#eef6ff', 'Glow', T(0, 6.55, 1.6))
    p.light((0, 6.45, 1.6), '#E8F4FF')
    p.collider((-0.15, 0, -0.15), (0.15, 6.5, 0.15))
    return p


def semaforo(name):
    p = Piece(name)
    p.add(cylinder(0.09, 0.08, 4.0, 8), '#8a8e98', 'Metal')
    p.beam((0, 3.8, 0), (0, 3.8, 2.6), 0.08, '#8a8e98', 'Metal')
    p.box((0, 4.2, 2.5), (0.95, 0.25, 0.15), '#3a3d45', 'Metal')
    for k, (c, on) in enumerate((('#38ff8a', True), ('#ffb22e', False), ('#ff3b30', False))):
        p.add(cylinder(0.17, 0.17, 0.04, 10), c if on else shade(c, 0.25), 'Glow' if on else 'Cloth',
              mul(T(-0.6 + k * 0.6, 4.2, 2.66), RX(math.pi / 2)))
    p.box((0, 2.4, 0.15), (0.17, 0.35, 0.1), '#3a3d45', 'Metal')  # de peatones
    p.add(box(0.11, 0.12, 0.02), '#38ff8a', 'Glow', T(0, 2.25, 0.26))
    p.collider((-0.15, 0, -0.15), (0.15, 4.0, 0.15))
    return p


def via_elevada(name):
    """Tramo de 20 m de la vía del tren elevado: pilar en el centro y tablero con raíles."""
    p = Piece(name)
    p.box((0, 3.6, 0), (1.0, 3.6, 0.9), CONCRETE[1], 'Stone')
    p.box((0, 7.4, 0), (3.4, 0.25, 1.2), CONCRETE[1], 'Stone')
    p.box((0, 8.0, 0), (3.2, 0.35, 10.0), CONCRETE[0], 'Stone')
    for x in (-3.1, 3.1):
        p.box((x, 8.75, 0), (0.12, 0.45, 10.0), shade(CONCRETE[0], 1.1), 'Stone')
        p.box((x, 9.15, 0), (0.14, 0.04, 10.0), '#ff3fa4' if x < 0 else '#2ee6ff', 'Glow')
    for x in (-1.9, -0.9, 0.9, 1.9):
        p.box((x, 8.42, 0), (0.05, 0.07, 10.0), '#9a9da6', 'Metal')
    for z in range(-9, 10, 2):
        p.box((0, 8.37, z), (1.6, 0.04, 0.15), '#3a332e')
    p.collider((-1.0, 0, -0.9), (1.0, 7.4, 0.9))
    p.collider((-3.4, 7.65, -10.0), (3.4, 8.4, 10.0))
    return p


def puesto(name, color):
    """Puesto de comida (yatai) con farolillos rojos encendidos."""
    p = Piece(name)
    wood = '#7a4f30'
    p.box((0, 0.5, 0), (1.2, 0.5, 0.6), wood)
    for x in (-1.0, 1.0):
        for z in (-0.5, 0.5):
            p.box((x, 0.8, z), (0.05, 0.8 if z < 0 else 1.2, 0.05), shade(wood, 0.8))
    p.add(gable(1.5, 0.9, 0.5), shade(wood, 0.7), m=T(0, 2.4, 0))
    for k in range(5):
        p.add(box(0.2, 0.32, 0.02), color if k % 2 else '#f2ead8', m=T(-0.9 + k * 0.45, 2.05, 0.92))
    for x in (-0.7, 0.0, 0.7):
        p.rod((x, 2.3, 1.0), (x, 2.0, 1.0), 0.01, '#222222', seg=4)
        p.add(sphere(0.17, 0.22, 0.17, 10, 7), '#ff5a3a', 'Glow', T(x, 1.8, 1.0))
        p.add(cylinder(0.08, 0.08, 0.05, 8), '#2a2a2a', m=T(x, 1.57, 1.0))
    p.box((0, 1.02, 0.2), (1.1, 0.03, 0.35), '#c9a06a')
    for k in range(3):
        p.add(cylinder(0.12, 0.1, 0.12, 8), '#f2f2f2', m=T(-0.6 + k * 0.6, 1.05, 0.2))
    p.light((0, 1.8, 1.1), '#FF7A50')
    p.collider((-1.2, 0, -0.6), (1.2, 1.05, 0.6))
    return p


def torii(name):
    p = Piece(name)
    red, black = '#d8402a', '#1f1f22'
    for x in (-2.0, 2.0):
        p.add(cylinder(0.26, 0.22, 5.0, 12), red, m=T(x, 0, 0))
        p.add(cylinder(0.32, 0.32, 0.4, 12), black, m=T(x, 0, 0))
    p.box((0, 4.2, 0), (2.6, 0.18, 0.2), red)
    p.box((0, 5.15, 0), (3.2, 0.2, 0.3), red)
    p.box((0, 5.45, 0), (3.5, 0.13, 0.36), black)
    p.box((0, 4.65, 0.22), (0.35, 0.45, 0.05), black)
    p.collider((-2.3, 0, -0.3), (-1.7, 5.0, 0.3))
    p.collider((1.7, 0, -0.3), (2.3, 5.0, 0.3))
    return p


def cerezo(name, seed):
    p = Piece(name)
    rnd = random.Random(seed)
    p.add(cylinder(0.28, 0.18, 2.4, 9), '#4a3433')
    for i in range(5):
        a = i * 1.3 + rnd.random()
        p.rod((0, 2.0, 0), (math.cos(a) * 1.6, 3.2, math.sin(a) * 1.6), 0.1, '#4a3433', r1=0.05)
    blobs = [(0, 3.7, 0, 1.8)] + [(math.cos(a) * 1.6, 3.3 + rnd.random() * 0.7, math.sin(a) * 1.6, 1.2 + rnd.random() * 0.3)
                                  for a in [i * 1.26 + rnd.random() * 0.4 for i in range(5)]]
    for k, (x, y, z, r) in enumerate(blobs):
        p.add(sphere(r, r * 0.75, r, 12, 8), lambda q, r=r, k=k: mix('#f59ac2' if k % 2 else '#ffb7d5', '#ffe3ef', max(0, q[1] / r) * 0.7),
              m=T(x, y, z))
    p.collider((-0.3, 0, -0.3), (0.3, 2.5, 0.3))
    return p


def coche(name, color, taxi=False):
    p = Piece(name)
    p.box((0, 0.65, 0), (0.9, 0.35, 2.1), color)
    p.box((0, 1.2, -0.2), (0.8, 0.3, 1.2), shade(color, 0.95))
    p.add(box(0.81, 0.24, 1.0), '#1d2733', 'Glass', T(0, 1.22, -0.2))
    for x in (-0.9, 0.9):
        for z in (-1.35, 1.35):
            p.add(cylinder(0.33, 0.33, 0.25, 12), '#1a1a1d', m=mul(T(x - 0.12 if x < 0 else x + 0.12, 0.33, z), RZ(math.pi / 2),
                                                                     T(0, -0.125, 0)))
    for x in (-0.6, 0.6):
        p.add(box(0.18, 0.08, 0.02), '#fff4d6', 'Glow', T(x, 0.75, 2.11))
        p.add(box(0.18, 0.06, 0.02), '#ff3030', 'Glow', T(x, 0.8, -2.11))
    if taxi:
        p.box((0, 1.6, -0.2), (0.3, 0.1, 0.18), '#fff2b0', 'Glow')
    p.collider((-0.95, 0, -2.1), (0.95, 1.5, 2.1))
    return p


def paso_cebra(name):
    p = Piece(name)
    for k in range(7):
        p.box((-4.2 + k * 1.4, 0.012, 0), (0.42, 0.012, 2.0), '#e8e8e8')
    return p


def poste_electrico(name):
    p = Piece(name)
    p.add(cylinder(0.18, 0.13, 9.0, 8), '#8f8a82', 'Stone')
    for y in (7.6, 8.4):
        p.box((0, y, 0), (1.2, 0.07, 0.07), '#5d5a55', 'Metal')
    p.add(cylinder(0.3, 0.3, 0.9, 10), '#6d717c', 'Metal', T(0.4, 6.2, 0.3))
    for x in (-1.1, -0.4, 0.4, 1.1):
        p.add(cylinder(0.04, 0.04, 0.15, 6), '#d8d8d8', m=T(x, 8.47, 0))
        p.rod((x, 8.6, 0), (x, 8.0, 2.5), 0.015, '#151515', seg=4)  # cables que se pierden
    p.collider((-0.2, 0, -0.2), (0.2, 9.0, 0.2))
    return p


def piezas():
    return [torre('torre_neon_a', 12, 12, 8, 1, roof_sign='#ff3fa4'), torre('torre_neon_b', 10, 10, 12, 2, setback=True),
            torre('torre_neon_c', 16, 10, 6, 3, screen=True), torre('torre_neon_d', 9, 14, 10, 4, roof_sign='#2ee6ff'),
            tienda('tienda_ramen', '#e8433a', 1), tienda('tienda_kombini', '#2fb36a', 2, 1), tienda('tienda_recreativos', '#9a5cff', 3, 3),
            letrero('letrero_rosa', '#ff3fa4', 11), letrero('letrero_cian', '#2ee6ff', 12, 5.0),
            letrero('letrero_amarillo', '#ffd23f', 13, 8.0), pantalla('pantalla_gigante'),
            expendedora('maquina_expendedora', '#e8e9ee'), expendedora('maquina_expendedora_roja', '#d8352f'),
            farola_led('farola_led'), semaforo('semaforo'), via_elevada('via_elevada'), puesto('puesto_comida', '#d8352f'),
            torii('torii'), cerezo('cerezo', 1), coche('coche', '#3a6fd8'), coche('taxi', '#f2c230', True),
            paso_cebra('paso_cebra'), poste_electrico('poste_electrico')]
