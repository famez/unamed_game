#!/usr/bin/env python3
"""Habitantes de los mapas: personajes con el esqueleto de la protagonista, hechos por piezas
(cara anime, pelo, ropa y accesorios), escritos en assets/models/habitantes/*.glb.

    python3 tools/personajes/habitantes.py [CARPETA] [--solo nombre1,nombre2]

Cada uno se describe con una ficha (diccionario): piel, pelo, ojos, ropa de arriba y de abajo,
calzado y accesorios. Para rehacer uno con otra herramienta basta con mantener el esqueleto
(ver assets/models/habitantes/FICHA_HABITANTES.md).
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from figura import Figure, Skeleton, box, cylinder, sphere, mix, shade, T, RX, RY, RZ, S, mul, basis_y  # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')

# --- Cuerpo ------------------------------------------------------------------------------


def lerp3(a, b, t):
    return tuple(a[k] + (b[k] - a[k]) * t for k in range(3))


HEAD_C = (0.0, 1.655, 0.005)
HEAD_R = (0.104, 0.118, 0.108)


def on_face(x, y, lift=0.0):
    """Punto de la superficie de la cara (la cabeza es un elipsoide) delante de (x, y)."""
    cx, cy, cz = HEAD_C
    rx, ry, rz = HEAD_R
    u = 1 - (x / rx) ** 2 - ((y - cy) / ry) ** 2
    return (x, y, cz + rz * math.sqrt(max(u, 0.0)) + lift)


def face(f, c):
    f.on('neck')  # cuello: del pecho a la cabeza
    f.add(cylinder(0.046, 0.042, 0.17, 10), shade(c['piel'], 0.92), m=T(0, 1.41, -0.005))
    f.on('head')
    f.add(sphere(*HEAD_R, 16, 12), c['piel'], m=T(*HEAD_C))
    for s in (-1, 1):  # orejas
        f.add(sphere(0.022, 0.032, 0.016, 8, 6), shade(c['piel'], 0.95), m=T(s * 0.102, 1.645, -0.005))
    for s in (-1, 1):  # ojos grandes: blanco, iris, pupila y brillo
        x = s * 0.04
        eye = c.get('ojos', '#3b2a20')
        p = on_face(x, 1.643, -0.004)
        f.add(sphere(0.026, 0.03, 0.01, 10, 6), '#f8f6f2', m=T(*p))
        p = on_face(x, 1.64, 0.0)
        f.add(sphere(0.019, 0.026, 0.008, 10, 6), eye, m=T(*p))
        f.add(sphere(0.01, 0.014, 0.006, 8, 5), shade(eye, 0.35), m=T(*on_face(x, 1.638, 0.004)))
        f.add(sphere(0.006, 0.006, 0.004, 6, 4), '#ffffff', m=T(*on_face(x + 0.007, 1.65, 0.008)))
        p = on_face(x, 1.675, 0.002)  # ceja
        f.add(box(0.018, 0.004, 0.004), c.get('cejas', shade(c['pelo'], 0.8)), m=mul(T(*p), RZ(-s * 0.12)))
        f.add(box(0.024, 0.004, 0.004), '#2a2020', m=T(*on_face(x, 1.661, 0.004)))  # pestañas de arriba
    f.add(sphere(0.008, 0.008, 0.008, 6, 4), shade(c['piel'], 0.9), m=T(*on_face(0, 1.618, 0.004)))  # nariz
    f.add(box(0.012, 0.003, 0.003), '#9a4a46', m=T(*on_face(0, 1.592, 0.002)))  # boca
    if c.get('mejillas'):
        for s in (-1, 1):
            f.add(sphere(0.014, 0.008, 0.004, 8, 4), c['mejillas'], m=T(*on_face(s * 0.058, 1.612, 0.0)))


def hair(f, c):
    f.on('head')
    h = c['pelo']
    style = c.get('peinado', 'corto')
    dark = shade(h, 0.85)
    if style == 'calvo':
        for s in (-1, 1):
            f.add(sphere(0.04, 0.05, 0.08, 8, 6), h, m=T(s * 0.095, 1.64, -0.03))
        f.add(sphere(0.09, 0.05, 0.05, 10, 6), h, m=T(0, 1.62, -0.08))
        return
    # Casquete: cubre arriba y detrás.
    f.add(sphere(0.112, 0.122, 0.118, 16, 10), h, m=T(0, 1.672, -0.012))
    # Flequillo: mechones en arco por delante.
    for k in range(7):
        x = -0.072 + k * 0.024
        y = 1.71 - abs(x) * 0.25
        p = on_face(x * 1.02, y, 0.006)
        f.add(sphere(0.022, 0.034, 0.016, 8, 6), h if k % 2 else dark, m=mul(T(*p), RZ(x * 2.2), RX(-0.35)))
    if style in ('melena', 'bob', 'trenzas', 'coleta', 'rizado', 'afro', 'mono'):
        for s in (-1, 1):  # mechones a los lados de la cara
            f.add(sphere(0.026, 0.07 if style != 'bob' else 0.06, 0.04, 8, 6), h, m=T(s * 0.098, 1.6, 0.02))
    if style == 'melena':
        f.add(sphere(0.11, 0.2, 0.06, 12, 8), h, m=T(0, 1.5, -0.085))
    elif style == 'bob':
        f.add(sphere(0.122, 0.095, 0.1, 14, 8), h, m=T(0, 1.61, -0.04))
    elif style == 'coleta':
        for k in range(5):
            f.add(sphere(0.04 - k * 0.004, 0.05, 0.04 - k * 0.004, 8, 6), h if k % 2 else dark, m=T(0, 1.69 - k * 0.07, -0.13 - k * 0.012))
    elif style == 'mono':
        f.add(sphere(0.055, seg=10, rings=8), h, m=T(0, 1.75, -0.08))
    elif style == 'trenzas':
        for s in (-1, 1):
            for k in range(7):
                f.add(sphere(0.026, 0.032, 0.026, 8, 6), h if k % 2 else dark, m=T(s * (0.1 + k * 0.006), 1.56 - k * 0.055, 0.03 + k * 0.012))
            f.add(sphere(0.014, seg=6, rings=4), c.get('lazo', '#e8433a'), m=T(s * 0.142, 1.18, 0.11))
    elif style in ('rizado', 'afro'):
        n = 22 if style == 'afro' else 14
        big = 0.055 if style == 'afro' else 0.04
        for k in range(n):  # rizos por arriba, los lados y detrás (la cara queda libre)
            a = math.pi * (0.15 + 1.7 * k / (n - 1)) + math.pi * 0.5
            y = 1.7 + 0.07 * math.sin(k * 1.7) + (0.06 if style == 'afro' else 0)
            r = 0.125 if style == 'afro' else 0.105
            x, z = math.cos(a) * r, math.sin(a) * r - 0.03
            if z > 0.03 and y < 1.74:
                y = 1.76
            f.add(sphere(big, seg=8, rings=6), h if k % 2 else dark, m=T(x, y, z))
    if c.get('barba'):
        f.add(sphere(0.085, 0.07, 0.06, 12, 8), c['barba'], m=T(0, 1.565, 0.05))
        f.add(sphere(0.04, 0.012, 0.012, 8, 4), c['barba'], m=T(*on_face(0, 1.6, 0.006)))
    elif c.get('bigote'):
        for s in (-1, 1):
            f.add(sphere(0.022, 0.009, 0.01, 8, 4), c['bigote'], m=mul(T(*on_face(s * 0.016, 1.603, 0.005)), RZ(s * 0.3)))


def torso(f, c):
    top, bottom = c['arriba'], c['abajo']
    kind = c.get('ropa', 'camisa')
    w = c.get('ancho', 1.0)
    fem = c.get('mujer', False)
    f.on('pelvis')
    f.add(sphere(0.15 * w, 0.1, 0.105 * w, 12, 8), bottom, m=T(0, 0.96, 0))
    f.add(cylinder(0.152 * w, 0.152 * w, 0.03, 12), c.get('cinturon', shade(bottom, 0.6)), m=T(0, 1.01, 0))
    f.on('spine')
    f.add(sphere(0.13 * w * (0.9 if fem else 1.0), 0.13, 0.095 * w, 12, 8), top, m=T(0, 1.11, 0))
    f.on('chest')
    f.add(sphere(0.165 * w * (0.92 if fem else 1.0), 0.15, 0.11 * w, 14, 10), top, m=T(0, 1.3, 0))
    if fem:
        for s in (-1, 1):
            f.add(sphere(0.055, 0.05, 0.05, 10, 6), top, m=T(s * 0.058, 1.3, 0.075))
    f.add(cylinder(0.075, 0.06, 0.05, 10), shade(top, 0.9), m=T(0, 1.43, -0.005))  # cuello de la prenda
    if kind in ('camisa', 'traje', 'chef'):  # solapas o cuello de camisa
        for s in (-1, 1):
            f.add(box(0.03, 0.05, 0.008), c.get('cuello', '#f4f2ee'), m=mul(T(s * 0.035, 1.415, 0.075), RZ(s * 0.5), RX(-0.4)))
    if kind == 'traje':
        f.add(box(0.045, 0.13, 0.01), c.get('camisa', '#f4f2ee'), m=T(0, 1.32, 0.105))
        f.add(box(0.014, 0.1, 0.008), c.get('corbata', '#b8202f'), m=T(0, 1.31, 0.115))
        for s in (-1, 1):
            f.add(box(0.03, 0.12, 0.008), shade(top, 0.8), m=mul(T(s * 0.045, 1.33, 0.108), RZ(s * 0.25)))
        f.on('pelvis')
        f.add(sphere(0.16 * w, 0.12, 0.115 * w, 12, 8), top, m=T(0, 0.98, 0))  # faldón de la chaqueta
    if kind == 'chef':
        for k in range(3):
            for s in (-1, 1):
                f.add(sphere(0.008, seg=6, rings=4), '#2a2a2e', m=T(s * 0.04, 1.36 - k * 0.07, 0.11))
    if kind == 'sudadera':
        f.on('chest')
        f.add(sphere(0.12, 0.07, 0.07, 12, 6), shade(top, 0.9), m=T(0, 1.43, -0.09))  # capucha
        f.add(box(0.07, 0.04, 0.01), shade(top, 0.8), m=T(0, 1.14, 0.1))  # bolsillo
        for s in (-1, 1):
            f.add(cylinder(0.004, 0.004, 0.12, 4), '#f4f2ee', m=T(s * 0.03, 1.27, 0.11))
    if kind == 'peto':
        f.on('chest')
        f.add(box(0.085, 0.09, 0.01), bottom, m=T(0, 1.27, 0.108))
        for s in (-1, 1):
            f.add(box(0.016, 0.12, 0.01), bottom, m=mul(T(s * 0.07, 1.33, 0.09), RX(-0.2)))
            f.add(sphere(0.012, seg=6, rings=4), '#c9a24a', 'Metal', m=T(s * 0.07, 1.34, 0.112))
    if kind == 'tirantes':  # camiseta de tirantes
        pass
    if kind == 'chaleco':
        f.on('chest')
        for s in (-1, 1):
            f.add(sphere(0.085, 0.14, 0.11, 10, 8), c.get('chaleco', '#3f5a3a'), m=T(s * 0.075, 1.29, 0.01))
        f.on('spine')
        f.add(sphere(0.135 * w, 0.12, 0.1 * w, 12, 8), c.get('chaleco', '#3f5a3a'), m=T(0, 1.13, 0))


def arms(f, c):
    top = c['arriba']
    sleeve = c.get('mangas', 'largas')
    skin = c['piel']
    for s, side in ((1, 'l'), (-1, 'r')):
        sh, el, wr = f.J('shoulder_' + side), f.J('elbow_' + side), f.J('wrist_' + side)
        f.on('chest')
        f.add(sphere(0.06, seg=10, rings=8), top if sleeve != 'sin' else skin, m=T(*sh))
        f.on('shoulder_' + side)
        if sleeve == 'largas':
            f.limb(sh, el, 0.056, 0.046, top)
        elif sleeve == 'cortas':
            mid = lerp3(sh, el, 0.45)
            f.limb(sh, mid, 0.06, 0.054, top)
            f.limb(mid, el, 0.046, 0.042, skin)
        else:
            f.limb(sh, el, 0.048, 0.042, skin)
        f.on('elbow_' + side)
        if sleeve == 'largas':
            f.limb(el, wr, 0.045, 0.037, top)
            f.add(cylinder(0.038, 0.038, 0.02, 10), shade(top, 0.85), m=mul(T(*lerp3(el, wr, 0.92)), basis_y(tuple(wr[k] - el[k] for k in range(3)))))
        else:
            f.limb(el, wr, 0.04, 0.033, skin)
        f.on('wrist_' + side)
        d = tuple(wr[k] - el[k] for k in range(3))
        n = math.sqrt(sum(x * x for x in d))
        hand = tuple(wr[k] + d[k] / n * 0.055 for k in range(3))
        f.add(sphere(0.034, 0.05, 0.022, 10, 6), skin, m=mul(T(*hand), basis_y(d)))
        f.add(sphere(0.012, 0.025, 0.012, 6, 4), skin, m=mul(T(*hand), basis_y(d), T(s * -0.028, -0.005, 0.015)))  # pulgar


def legs(f, c):
    bottom = c['abajo']
    kind = c.get('piernas', 'pantalon')
    skin = c.get('medias', c['piel'])
    shoes = c.get('zapatos', '#2a2622')
    for side in ('l', 'r'):
        hp, kn, an, to = f.J('hip_' + side), f.J('knee_' + side), f.J('ankle_' + side), f.J('toe_' + side)
        f.on('hip_' + side)
        if kind in ('pantalon', 'peto'):
            f.limb(hp, kn, 0.075, 0.058, bottom)
        elif kind == 'shorts':
            mid = lerp3(hp, kn, 0.45)
            f.limb(hp, mid, 0.08, 0.072, bottom)
            f.limb(mid, kn, 0.06, 0.052, skin)
        else:  # bajo una falda o un vestido
            f.limb(hp, kn, 0.066, 0.05, skin)
        f.on('knee_' + side)
        f.limb(kn, an, 0.052 if kind in ('pantalon', 'peto') else 0.047, 0.04, bottom if kind in ('pantalon', 'peto') else skin)
        f.on('ankle_' + side)
        if c.get('descalzo'):
            f.add(sphere(0.04, 0.03, 0.08, 8, 6), c['piel'], m=T(an[0], 0.03, an[2] + 0.05))
            continue
        botas = c.get('botas')
        if botas:
            f.add(cylinder(0.05, 0.048, 0.16, 10), shoes, m=T(an[0], 0.02, an[2]))
        f.add(sphere(0.048, 0.04, 0.1, 10, 6), shoes, m=T(an[0], 0.035, an[2] + 0.05))
        f.add(box(0.048, 0.012, 0.1), shade(shoes, 0.6) if c.get('suela') is None else c['suela'], m=T(an[0], 0.008, an[2] + 0.05))


def skirt(f, c):
    kind = c.get('piernas')
    if kind not in ('falda', 'falda_larga', 'vestido'):
        return
    f.on('pelvis')
    low = 0.12 if kind == 'falda_larga' else 0.5
    hem = 0.25 if kind == 'falda_larga' else 0.22
    # Dos tramos: ceñida en la cadera y con vuelo abajo, con pliegues.
    pleat = lambda q: shade(c['abajo'], 0.88 + 0.12 * (0.5 + 0.5 * math.sin(math.atan2(q[2], q[0]) * 9)))
    mid = low + (1.02 - low) * 0.55
    f.add(cylinder(0.175, 0.15, 1.03 - mid, 16), pleat, m=mul(T(0, mid, 0), S(1, 1, 0.82)))
    f.add(cylinder(hem, 0.175, mid - low, 16, caps=False), pleat, m=mul(T(0, low, 0), S(1, 1, 0.82)))
    if c.get('volante'):
        f.add(cylinder(hem + 0.012, hem + 0.004, 0.05, 16, caps=False), c['volante'], m=mul(T(0, low, 0), S(1, 1, 0.82)))


def build(name, c, skel):
    f = Figure(name, skel)
    face(f, c)
    hair(f, c)
    torso(f, c)
    arms(f, c)
    legs(f, c)
    skirt(f, c)
    for extra in c.get('extras', []):
        extra(f)
    return f


# --- Accesorios ---------------------------------------------------------------------------


def sombrero_aguadeno(f):
    f.on('head')
    f.add(cylinder(0.25, 0.25, 0.012, 20), '#efe6cf', m=T(0, 1.755, -0.005))
    f.add(cylinder(0.115, 0.1, 0.11, 16), '#efe6cf', m=T(0, 1.76, -0.005))
    f.add(cylinder(0.117, 0.115, 0.03, 16), '#1a1a1d', m=T(0, 1.765, -0.005))
    f.add(sphere(0.1, 0.02, 0.1, 12, 4), shade('#efe6cf', 0.9), m=T(0, 1.87, -0.005))


def poncho_hombro(f):
    f.on('chest')
    sh = f.J('shoulder_l')
    f.add(box(0.07, 0.03, 0.12), '#f4f2ee', m=mul(T(sh[0] - 0.02, sh[1] + 0.04, 0), RZ(-0.35)))
    f.add(box(0.06, 0.16, 0.012), '#f4f2ee', m=mul(T(sh[0] + 0.01, sh[1] - 0.12, 0.1), RZ(0.1)))
    f.add(box(0.06, 0.16, 0.012), '#f4f2ee', m=mul(T(sh[0] + 0.01, sh[1] - 0.12, -0.1), RZ(0.1)))
    for k in range(5):  # flecos
        f.add(box(0.004, 0.025, 0.004), '#e8e2d6', m=T(sh[0] - 0.04 + k * 0.025, sh[1] - 0.29, 0.1))


def carriel(f):
    f.on('chest')
    f.beam((-0.13, 1.38, 0.09), (0.15, 1.08, 0.1), 0.025, '#5a3622', d=0.008)
    f.beam((-0.13, 1.38, -0.09), (0.15, 1.08, -0.1), 0.025, '#5a3622', d=0.008)
    f.on('pelvis')
    f.add(box(0.075, 0.07, 0.03), '#6b4428', m=T(0.17, 0.98, 0.0))
    f.add(box(0.077, 0.03, 0.032), '#3a2414', m=T(0.17, 1.02, 0.0))
    f.add(sphere(0.012, seg=6, rings=4), '#c9a24a', 'Metal', m=T(0.17, 0.99, 0.034))


def canasto(f):
    f.on('pelvis')
    f.add(cylinder(0.11, 0.13, 0.14, 12), lambda q: shade('#b88a4a', 0.85 if int(q[1] * 60) % 2 else 1.0), m=T(0.0, 0.84, 0.16))
    for k in range(9):
        a = k * 0.7
        f.add(sphere(0.02, seg=6, rings=4), '#c8102e' if k % 3 else '#e8b60c', m=T(math.cos(a) * 0.07, 0.985, 0.16 + math.sin(a) * 0.07))
    f.beam((-0.12, 1.0, 0.1), (-0.06, 0.92, 0.16), 0.015, '#8a6a3a', d=0.01)
    f.beam((0.12, 1.0, 0.1), (0.06, 0.92, 0.16), 0.015, '#8a6a3a', d=0.01)


def sombrero_paja(f):
    f.on('head')
    f.add(cylinder(0.27, 0.25, 0.012, 20), lambda q: shade('#e2c46a', 0.9 if int((q[0] + q[2]) * 40) % 2 else 1.0), m=T(0, 1.75, -0.01))
    f.add(cylinder(0.115, 0.1, 0.1, 16), '#e2c46a', m=T(0, 1.755, -0.01))
    f.add(cylinder(0.117, 0.116, 0.025, 16), '#c8202f', m=T(0, 1.76, -0.01))


def gafas(f):
    f.on('head')
    for s in (-1, 1):
        p = on_face(s * 0.04, 1.642, 0.012)
        f.add(cylinder(0.026, 0.026, 0.004, 12, caps=False), '#1a1a1d', 'Metal', mul(T(*p), RX(math.pi / 2)))
    f.add(box(0.012, 0.003, 0.003), '#1a1a1d', 'Metal', T(*on_face(0, 1.648, 0.015)))


def maletin(f):
    f.on('wrist_r')
    w = f.J('wrist_r')
    f.add(box(0.18, 0.13, 0.04), '#3a2a20', m=T(w[0] - 0.02, w[1] - 0.2, w[2]))
    f.add(box(0.04, 0.012, 0.012), '#2a1a12', m=T(w[0] - 0.02, w[1] - 0.06, w[2]))


def auriculares(f):
    f.on('head')
    f.add(cylinder(0.122, 0.122, 0.018, 16, caps=False), '#1a1a1d', m=mul(T(0, 1.665, -0.005), RZ(math.pi / 2), T(0, -0.009, 0), S(1, 1, 0.3)))
    for s in (-1, 1):
        f.add(cylinder(0.035, 0.035, 0.025, 12), '#1a1a1d', m=mul(T(s * 0.11, 1.645, 0), RZ(math.pi / 2), T(0, -0.012, 0)))
        f.add(cylinder(0.022, 0.022, 0.006, 12), '#2ee6ff', 'Glow', mul(T(s * 0.124, 1.645, 0), RZ(math.pi / 2), T(0, -0.003, 0)))


def hachimaki(f):
    f.on('head')
    f.add(cylinder(0.116, 0.116, 0.03, 18, caps=False), '#f4f2ee', m=T(0, 1.69, -0.005))
    f.add(sphere(0.012, seg=6, rings=4), '#d8202f', m=T(*on_face(0, 1.705, 0.008)))
    f.add(box(0.02, 0.05, 0.006), '#f4f2ee', m=mul(T(0.02, 1.66, -0.12), RZ(0.4)))


def delantal(f):
    f.on('pelvis')
    f.add(box(0.15, 0.22, 0.01), '#23314a', m=T(0, 0.82, 0.115))
    f.on('chest')
    f.add(box(0.1, 0.1, 0.01), '#23314a', m=T(0, 1.25, 0.112))


def tabla_surf(f):
    f.on('chest')
    f.add(sphere(0.25, 0.95, 0.025, 16, 10), lambda q: '#f2c230' if abs(q[0]) < 0.03 else '#2ab3c9',
          m=mul(T(-0.36, 1.05, -0.12), RY(math.pi / 2 - 0.5), RZ(-0.12)))


def gafas_sol_cabeza(f):
    f.on('head')
    for s in (-1, 1):
        f.add(sphere(0.03, 0.022, 0.008, 10, 6), '#1a1a1d', 'Glass', T(s * 0.04, 1.745, 0.075))
    f.add(box(0.04, 0.004, 0.004), '#c9a24a', 'Metal', T(0, 1.75, 0.08))


def nevera(f):
    f.on('chest')
    f.beam((0.13, 1.4, 0.0), (-0.15, 1.0, 0.08), 0.03, '#2a2a2e', d=0.008)
    f.on('pelvis')
    f.add(box(0.13, 0.1, 0.09), '#f4f2ee', m=T(-0.24, 0.92, 0.04))
    f.add(box(0.135, 0.02, 0.095), '#1b6fbc', m=T(-0.24, 1.02, 0.04))


def flor_pelo(f):
    f.on('head')
    for k in range(5):
        a = k * 1.2566
        f.add(sphere(0.018, 0.018, 0.008, 8, 4), '#ff5a7a', m=T(0.095 + math.cos(a) * 0.016, 1.7 + math.sin(a) * 0.016, 0.06))
    f.add(sphere(0.01, seg=6, rings=4), '#ffd23f', m=T(0.097, 1.7, 0.066))


def horca(f):
    f.on('wrist_r')
    w = f.J('wrist_r')
    f.rod((w[0] - 0.03, w[1] - 0.6, w[2] + 0.06), (w[0] - 0.03, w[1] + 0.85, w[2] + 0.06), 0.014, '#8a6440')
    top = (w[0] - 0.03, w[1] + 0.85, w[2] + 0.06)
    f.add(box(0.07, 0.01, 0.01), '#7a7d84', 'Metal', T(*top))
    for k in range(3):
        x = top[0] - 0.06 + k * 0.06
        f.rod((x, top[1], top[2]), (x, top[1] + 0.22, top[2]), 0.006, '#7a7d84', 'Metal', seg=5)


def gorra_plana(f):
    f.on('head')
    f.add(sphere(0.118, 0.06, 0.125, 14, 6), '#6b5440', m=T(0, 1.735, -0.005))
    f.add(box(0.08, 0.008, 0.05), shade('#6b5440', 0.85), m=mul(T(0, 1.735, 0.115), RX(0.15)))


def baston(f):
    f.on('wrist_r')
    w = f.J('wrist_r')
    f.rod((w[0] - 0.02, 0.02, w[2] + 0.1), (w[0] - 0.02, w[1] - 0.02, w[2] + 0.06), 0.014, '#5a3a24')
    f.add(sphere(0.022, seg=8, rings=6), '#3a2414', m=T(w[0] - 0.02, w[1], w[2] + 0.06))


def pecas(f):
    f.on('head')
    for k in range(8):
        s = -1 if k < 4 else 1
        x = s * (0.045 + (k % 4) * 0.008)
        f.add(sphere(0.003, seg=4, rings=3), '#b8714a', m=T(*on_face(x, 1.615 + (k % 2) * 0.008, 0.002)))


# --- Reparto ------------------------------------------------------------------------------

# El abuelo, el cafetero y la chica neón los rehízo el paquete «Farolas» (Abuelo Candil, el mozo
# de la fonda y Chispa): ya no se generan aquí para no pisarlos.
HABITANTES = {
    # Pueblo cafetero
    'recolectora': dict(piel='#b98560', pelo='#1e1814', peinado='trenzas', lazo='#ffd23f', ojos='#3a2414', mujer=True,
                        mejillas='#d88a7a', arriba='#f4f2ee', mangas='cortas', ropa='blusa', abajo='#c8202f', piernas='falda_larga',
                        volante='#1b6fbc', zapatos='#5a3a24', extras=[sombrero_paja, canasto]),
    # Ciudad neón
    'oficinista': dict(piel='#f0d2b8', pelo='#141418', peinado='corto', ojos='#2a2420', arriba='#232a3c', ropa='traje',
                       abajo='#232a3c', corbata='#b8202f', zapatos='#141418', extras=[gafas, maletin]),
    'cocinero': dict(piel='#e6c3a3', pelo='#141418', peinado='corto', ojos='#2a2420', arriba='#f4f2ee', ropa='chef', cuello='#f4f2ee',
                     abajo='#1a1a1d', zapatos='#2a2622', extras=[hachimaki, delantal]),
    # Ciudad de la bahía
    'surfista': dict(piel='#a8714f', pelo='#2a1a10', peinado='rizado', ojos='#3a2414', arriba='#ffd23f', mangas='sin', ropa='tirantes',
                     abajo='#1fa8a0', piernas='shorts', descalzo=True, extras=[tabla_surf, gafas_sol_cabeza]),
    'vendedora': dict(piel='#7a4e34', pelo='#1a1210', peinado='afro', ojos='#2a1a10', mujer=True, mejillas='#a0584a',
                      arriba='#ff8a2a', mangas='sin', ropa='vestido', abajo='#ff8a2a', piernas='vestido', volante='#ffd23f', zapatos='#8a5a34',
                      extras=[nevera, flor_pelo]),
    # Campo
    'granjera': dict(piel='#f1d0b5', pelo='#c4532a', peinado='trenzas', lazo='#2a7d4f', ojos='#2a7d4f', mujer=True, mejillas='#f0a090',
                     arriba='#c8302f', ropa='peto', abajo='#3d5f8f', piernas='peto', zapatos='#5a3a24', botas=True,
                     extras=[sombrero_paja, horca, pecas]),
}


def main():
    argv = sys.argv[1:]
    only = None
    if '--solo' in argv:
        k = argv.index('--solo')
        only = set(argv[k + 1].split(','))
        del argv[k:k + 2]
    out = argv[0] if argv else os.path.join(ROOT, 'assets', 'models', 'habitantes')
    os.makedirs(out, exist_ok=True)
    skel = Skeleton(os.path.join(ROOT, 'assets', 'models', 'ilse.glb'))
    for name, ficha in HABITANTES.items():
        if only and name not in only:
            continue
        f = build(name, ficha, skel)
        f.write(os.path.join(out, name + '.glb'))
        print('%-14s %6d triángulos' % (name, f.triangles()))


if __name__ == '__main__':
    main()
