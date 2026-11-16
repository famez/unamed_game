"""Piezas del pueblo cafetero (inspirado en Salento y Filandia, Eje Cafetero): casas de
bahareque blancas con puertas, ventanas y balcones de madera de colores, la iglesia, el
kiosco de la plaza, palmas de cera, plataneras, cafetos, guaduales, heliconias, el yipao
(Willys) cargado y faroles coloniales."""

import math
import random

from kit import Piece, box, cylinder, sphere, gable, pyramid, mix, shade, T, RY, RX, RZ, S, mul

WALL = '#f4efe4'
TILE = '#9c4a2c'
RAFTER = '#5a3a24'
FLOWERS = ['#e0245e', '#ff6f91', '#c13fd6', '#ff9f1c', '#f7f7f2', '#e83a3a']
PALETTES = {  # (zócalo y balcón, puertas y contraventanas)
    'rojo': ('#c8202f', '#1f6fb2'), 'azul': ('#1b6fbc', '#e3a61b'), 'verde': ('#21915a', '#c8202f'),
    'amarillo': ('#e8a90c', '#2a7d4f'), 'naranja': ('#e8641a', '#1b6fbc'), 'morado': ('#7b3fa0', '#21a39a'),
    'turquesa': ('#1fa8a0', '#c8202f'),
}


def tiles(base):
    """Teja: filas alternas más claras y oscuras (según la altura en el faldón)."""
    return lambda q: shade(base, 0.78 if int(q[1] * 6) % 2 else 1.0)


def shutter_window(p, m, w, h, frame, shutter, open_=True, bars=True):
    """Ventana de madera con contraventanas abiertas y, si se pide, barrotes torneados."""
    p.add(box(w / 2 + 0.1, h / 2 + 0.1, 0.05), frame, m=m)
    p.add(box(w / 2, h / 2, 0.06), '#2b2622', m=m)
    if bars:
        n = int(w / 0.14)
        for k in range(n + 1):
            p.add(cylinder(0.018, 0.018, h, 5, caps=False), frame, m=mul(m, T(-w / 2 + k * w / n, -h / 2, 0.09)))
    for sx in (-1, 1):
        sm = mul(m, T(sx * (w / 2 + w / 4 + 0.1), 0, 0.06)) if open_ else mul(m, T(sx * w / 4, 0, 0.08))
        p.add(box(w / 4, h / 2, 0.03), shutter, m=sm)
        p.add(box(w / 4 - 0.07, h / 2 - 0.12, 0.035), shade(shutter, 0.82), m=sm)  # cuarterones
    p.add(box(w / 2 + 0.2, 0.05, 0.12), frame, m=mul(m, T(0, h / 2 + 0.15, 0.05)))  # guardapolvo


def wood_door(p, m, w, h, frame, color):
    p.add(box(w / 2 + 0.12, h / 2 + 0.08, 0.05), frame, m=mul(m, T(0, h / 2, 0)))
    for sx in (-1, 1):  # puerta de dos hojas con cuarterones
        hm = mul(m, T(sx * w / 4, h / 2, 0.03))
        p.add(box(w / 4 - 0.01, h / 2 - 0.02, 0.04), color, m=hm)
        for y in (-h / 4, h / 4):
            p.add(box(w / 4 - 0.1, h / 4 - 0.12, 0.045), shade(color, 0.8), m=mul(hm, T(0, y, 0)))
    p.add(box(w / 2 + 0.25, 0.06, 0.14), frame, m=mul(m, T(0, h + 0.18, 0.05)))


def maceta(p, at, seed):
    rnd = random.Random(seed)
    x, y, z = at
    p.rod((x, y + 0.45, z), (x, y + 0.15, z), 0.008, '#222222', seg=4)
    p.add(cylinder(0.11, 0.14, 0.18, 8), '#b0643a', m=T(x, y - 0.05, z))
    for k in range(4):
        a = k * 1.6 + rnd.random()
        p.add(sphere(0.07, seg=6, rings=4), rnd.choice(FLOWERS), m=T(x + math.cos(a) * 0.1, y + 0.17, z + math.sin(a) * 0.1))
    for k in range(3):  # hojas que cuelgan
        a = k * 2.1 + rnd.random()
        p.add(sphere(0.06, 0.16, 0.06, 6, 4), '#3f8f3a', m=T(x + math.cos(a) * 0.12, y - 0.12, z + math.sin(a) * 0.12))


def casa(name, color, seed, floors=2, width=7.0):
    accent, shutter = PALETTES[color]
    p = Piece(name)
    rnd = random.Random(seed)
    w, d = width, 9.0
    fh = 3.2
    h = fh * floors
    fz = d / 2
    p.box((0, h / 2, 0), (w / 2, h / 2, d / 2), WALL)
    p.box((0, 0.45, 0), (w / 2 + 0.02, 0.45, d / 2 + 0.02), accent)  # zócalo pintado
    p.box((0, 0.93, 0), (w / 2 + 0.04, 0.03, d / 2 + 0.04), shade(accent, 0.75))
    for x in (-w / 2, w / 2):  # esquinas de madera
        p.box((x, h / 2, fz), (0.1, h / 2, 0.1), shade(accent, 0.9))
    # Planta baja: puerta y ventanas.
    door_x = -w / 2 + 1.6 if floors == 2 else 0.0
    wood_door(p, T(door_x, 0.0, fz + 0.02), 1.3, 2.3, accent, shutter)
    xs = [x for x in (-w / 2 + 1.6, 0.6, w / 2 - 1.3) if abs(x - door_x) > 1.2] if floors == 2 else [-w / 3, w / 3]
    for x in xs:
        shutter_window(p, T(x, 1.75, fz + 0.02), 1.0, 1.15, accent, shutter)
    for z in (-2.2, 1.6):  # costado
        shutter_window(p, mul(T(w / 2 + 0.02, 1.75, z), RY(math.pi / 2)), 0.9, 1.1, accent, shutter, bars=False)
    if floors == 2:
        # Balcón corrido de madera: suelo, ménsulas, baranda de balaústres y macetas.
        by, depth = fh + 0.05, 1.0
        p.box((0, by, fz + depth / 2), (w / 2 + 0.1, 0.08, depth / 2), shade(accent, 0.8))
        for x in [-w / 2 + 0.3 + k * (w - 0.6) / 4 for k in range(5)]:
            p.beam((x, by - 0.65, fz), (x, by - 0.05, fz + depth * 0.9), 0.12, shade(accent, 0.7))
        p.box((0, by + 0.95, fz + depth), (w / 2 + 0.1, 0.05, 0.06), accent)
        p.box((0, by + 0.12, fz + depth), (w / 2 + 0.1, 0.04, 0.06), accent)
        for k in range(int(w / 0.16) + 1):
            x = -w / 2 + k * 0.16
            p.add(cylinder(0.025, 0.025, 0.8, 5, caps=False), WALL if k % 2 else accent, m=T(x, by + 0.12, fz + depth))
        for side in (-1, 1):
            p.box((side * (w / 2 + 0.07), by + 0.5, fz + depth / 2), (0.05, 0.45, depth / 2), accent)
        # Puertas balconeras arriba.
        for x in (-w / 3, 0.0, w / 3):
            p.add(box(0.5, 1.05, 0.05), accent, m=T(x, fh + 1.15, fz + 0.02))
            for sx in (-1, 1):
                p.add(box(0.24, 0.98, 0.04), shutter, m=T(x + sx * 0.25, fh + 1.15, fz + 0.06))
                p.add(box(0.17, 0.4, 0.045), shade(shutter, 0.8), m=T(x + sx * 0.25, fh + 1.45, fz + 0.06))
        for k, x in enumerate([-w / 2 + 0.6 + i * (w - 1.2) / 5 for i in range(6)]):
            maceta(p, (x, by + 0.55, fz + depth + 0.12), seed * 10 + k)
    else:
        for k, x in enumerate((-w / 3 - 0.35, -w / 3 + 0.35, w / 3 - 0.35, w / 3 + 0.35)):
            maceta(p, (x, 2.75, fz + 0.25), seed * 10 + k)
    # Tejado de barro con aleros muy anchos (cubren el balcón) y canecillos de madera.
    eave = 1.5 if floors == 2 else 1.0
    p.add(gable(w / 2 + 0.4, d / 2 + eave, 2.4), tiles(TILE), m=T(0, h, 0))
    for x in [-w / 2 + 0.2 + k * (w - 0.4) / 7 for k in range(8)]:
        p.beam((x, h - 0.05, fz - 0.2), (x, h + 0.05 - 0.1, fz + eave - 0.1), 0.09, RAFTER)
    p.box((0, h - 0.06, fz + eave - 0.1), (w / 2 + 0.4, 0.08, 0.06), RAFTER)
    p.collider((-w / 2, 0, -d / 2), (w / 2, h + 1.0, d / 2))
    return p


def iglesia(name):
    p = Piece(name)
    w, d, h = 12.0, 22.0, 10.0
    p.box((0, h / 2, 0), (w / 2, h / 2, d / 2), WALL)
    p.add(gable(d / 2 + 0.6, w / 2 + 0.6, 4.0), tiles(TILE), m=mul(T(0, h, 0), RY(math.pi / 2)))
    fz = d / 2
    # Fachada: frontón más alto, pilastras, puerta en arco y rosetón.
    p.box((0, h + 2.0, fz + 0.15), (w / 2 + 0.2, 2.0, 0.3), WALL)
    p.add(gable(w / 2 + 0.3, 0.35, 2.2), WALL, m=T(0, h + 4.0, fz + 0.15))
    for x in (-w / 2, -w / 6, w / 6, w / 2):
        p.box((x, (h + 4) / 2, fz + 0.4), (0.35, (h + 4) / 2, 0.15), '#e6dccb')
    p.box((0, h + 0.1, fz + 0.5), (w / 2 + 0.3, 0.15, 0.2), '#3d6fa8')  # cornisa azul
    p.box((0, 2.0, fz + 0.32), (1.1, 2.0, 0.05), '#5a3622')
    p.add(cylinder(1.1, 1.1, 0.1, 16), '#5a3622', m=mul(T(0, 4.0, fz + 0.28), RX(math.pi / 2)))
    p.add(cylinder(1.35, 1.35, 0.08, 16), '#3d6fa8', m=mul(T(0, 4.0, fz + 0.24), RX(math.pi / 2)))
    p.add(cylinder(1.0, 1.0, 0.06, 16), '#a8743a', 'Glow', mul(T(0, 8.0, fz + 0.32), RX(math.pi / 2)))  # rosetón
    for x in (-w / 3, w / 3):
        p.add(box(0.5, 1.2, 0.05), '#a8743a', 'Glow', T(x, 6.5, fz + 0.3))
    for z in (-6, 0, 6):
        for x in (-w / 2 - 0.02, w / 2 + 0.02):
            p.add(box(0.05, 1.4, 0.6), '#a8743a', 'Glow', T(x, 5.5, z))
    # Torre con campanario y chapitel.
    tw = 3.6
    p.box((0, h + 5.0, fz - 1.8), (tw / 2, 5.0, tw / 2), WALL)
    for a in range(4):
        m = mul(T(0, h + 7.6, fz - 1.8), RY(a * math.pi / 2), T(0, 0, tw / 2 + 0.01))
        p.add(box(0.6, 1.1, 0.05), '#2a2622', m=m)
        p.add(sphere(0.35, seg=8, rings=6), '#b08d3a', 'Metal', mul(m, T(0, 0.1, -0.3)))
    p.box((0, h + 10.1, fz - 1.8), (tw / 2 + 0.25, 0.15, tw / 2 + 0.25), '#3d6fa8')
    p.add(pyramid(tw / 2, tw / 2, 4.0), '#3d6fa8', m=T(0, h + 10.25, fz - 1.8))
    p.box((0, h + 15.0, fz - 1.8), (0.06, 0.7, 0.06), '#d9c27a', 'Metal')
    p.box((0, h + 15.2, fz - 1.8), (0.35, 0.06, 0.06), '#d9c27a', 'Metal')
    p.light((0, 2.5, fz + 1.5), '#FFD08A')
    p.collider((-w / 2, 0, -d / 2), (w / 2, h + 4, d / 2 + 0.5))
    return p


def kiosco(name):
    p = Piece(name)
    r = 3.6
    p.add(cylinder(r, r, 0.28, 8, smooth=False), '#d9d2c3', 'Stone')
    p.add(cylinder(r + 0.15, r + 0.15, 0.08, 8, smooth=False), '#b8ad98', 'Stone')
    for k in range(8):
        a = k * math.pi / 4 + math.pi / 8
        x, z = math.cos(a) * (r - 0.3), math.sin(a) * (r - 0.3)
        p.add(cylinder(0.11, 0.1, 3.0, 8), '#f4efe4', m=T(x, 0.28, z))
        if k != 2:  # baranda (salvo por la entrada)
            b = (k + 1) * math.pi / 4 + math.pi / 8
            x2, z2 = math.cos(b) * (r - 0.3), math.sin(b) * (r - 0.3)
            p.beam((x, 1.1, z), (x2, 1.1, z2), 0.06, '#21915a')
            p.beam((x, 0.45, z), (x2, 0.45, z2), 0.05, '#21915a')
    p.add(cylinder(r + 0.5, 0.0, 2.2, 8, smooth=False), lambda q: '#c8202f' if int(math.atan2(q[2], q[0]) / (math.pi / 4) + 8) % 2 else '#f4efe4',
          m=T(0, 3.28, 0))
    p.add(sphere(0.18, seg=8, rings=6), '#d9c27a', 'Metal', T(0, 5.55, 0))
    p.add(cylinder(0.25, 0.2, 0.3, 8), '#2a2a2a', 'Metal', T(0, 2.9, 0))
    p.add(sphere(0.18, seg=8, rings=6), '#ffd08a', 'Glow', T(0, 2.8, 0))
    p.light((0, 2.8, 0), '#FFD08A')
    p.collider((-r, 0, -r), (r, 0.28, r))
    return p


def palma_cera(name, height=24.0, seed=1):
    p = Piece(name)
    rnd = random.Random(seed)
    p.add(cylinder(0.34, 0.2, height, 10), lambda q: shade('#d8d3c6', 0.82 if int(q[1] * 2.2) % 3 == 0 else 1.0))
    top = (0, height, 0)
    p.add(sphere(0.32, 0.6, 0.32, 8, 6), '#6d8a3a', m=T(0, height + 0.1, 0))
    for k in range(11):
        a = k * 2 * math.pi / 11 + rnd.random() * 0.3
        rise = rnd.uniform(0.2, 0.9)
        prev = top
        for s in range(4):  # la hoja se arquea hacia abajo
            t = (s + 1) / 4
            ln = 1.0
            ang = rise - t * 1.6
            nxt = (prev[0] + math.cos(a) * ln * math.cos(ang), prev[1] + math.sin(ang) * ln, prev[2] + math.sin(a) * ln * math.cos(ang))
            p.beam(prev, nxt, 0.5 - 0.1 * s, mix('#3f6f2e', '#7fae5a', t), d=0.04)
            prev = nxt
    p.collider((-0.35, 0, -0.35), (0.35, height, 0.35))
    return p


def platanera(name, seed=1):
    p = Piece(name)
    rnd = random.Random(seed)
    p.add(cylinder(0.17, 0.12, 2.6, 8), lambda q: mix('#6d7f3a', '#9bb24f', q[1] / 2.6))
    for k in range(7):
        a = k * 2 * math.pi / 7 + rnd.random() * 0.4
        base = (0, 2.5, 0)
        mid = (math.cos(a) * 0.9, 3.4 + rnd.random() * 0.3, math.sin(a) * 0.9)
        tip = (math.cos(a) * 2.0, 2.9 + rnd.random() * 0.4, math.sin(a) * 2.0)
        p.beam(base, mid, 0.55, '#4f9a37', d=0.03)
        p.beam(mid, tip, 0.5, '#5aaa3e', d=0.03)
    for k in range(6):  # racimo
        y = 2.0 - k * 0.12
        p.add(sphere(0.2 - k * 0.015, 0.08, 0.2 - k * 0.015, 8, 4), '#8db33a', m=T(0.25, y, 0))
    p.add(sphere(0.1, 0.16, 0.1, 8, 6), '#6a2244', m=T(0.25, 1.15, 0))
    p.collider((-0.18, 0, -0.18), (0.18, 2.4, 0.18))
    return p


def cafeto(name, seed=1):
    p = Piece(name)
    rnd = random.Random(seed)
    for k in range(5):
        y = 0.4 + k * 0.32
        r = 0.7 - k * 0.11
        p.add(sphere(r, 0.32, r, 10, 6), '#2f6b33' if k % 2 else '#3a7d3a', m=T(0, y, 0))
    for k in range(26):
        a, y = rnd.random() * 6.28, 0.35 + rnd.random() * 1.3
        r = 0.66 - (y - 0.35) * 0.4
        p.add(sphere(0.045, seg=5, rings=3), '#c8102e' if rnd.random() < 0.75 else '#e8b60c',
              m=T(math.cos(a) * r, y, math.sin(a) * r))
    return p


def guadua(name, seed=1):
    p = Piece(name)
    rnd = random.Random(seed)
    for k in range(9):
        a = k * 2.4 + rnd.random()
        lean = rnd.uniform(0.05, 0.25)
        h = rnd.uniform(8.0, 11.5)
        base = (math.cos(a) * 0.35, 0, math.sin(a) * 0.35)
        top = (base[0] + math.cos(a) * h * lean, h, base[2] + math.sin(a) * h * lean)
        p.rod(base, top, 0.08, lambda q: '#c8c070' if int(q[1] * 2) % 5 == 0 else '#6f9e34', r1=0.05)
        for j in range(4):  # follaje arriba, colgando
            t = 0.6 + j * 0.12
            q = (base[0] + (top[0] - base[0]) * t, h * t, base[2] + (top[2] - base[2]) * t)
            p.add(sphere(0.55, 0.28, 0.55, 8, 5), '#7fae4a' if j % 2 else '#6a9a3e', m=T(q[0] + math.cos(a) * 0.4, q[1] - 0.2, q[2] + math.sin(a) * 0.4))
    p.collider((-0.5, 0, -0.5), (0.5, 3.0, 0.5))
    return p


def heliconia(name, seed=1):
    p = Piece(name)
    rnd = random.Random(seed)
    for k in range(5):
        a, dd = rnd.random() * 6.28, rnd.random() * 0.35
        x, z = math.cos(a) * dd, math.sin(a) * dd
        h = rnd.uniform(1.0, 1.5)
        p.rod((x, 0, z), (x, h, z), 0.02, '#4f8a35', seg=5)
        for j in range(5):  # brácteas en zigzag, rojas con la punta amarilla
            side = 1 if j % 2 else -1
            y = h - 0.1 - j * 0.12
            p.add(pyramid(0.04, 0.03, 0.18), '#d81e2c', m=mul(T(x, y, z), RZ(side * 1.0)))
            p.add(sphere(0.025, seg=5, rings=3), '#f2c230', m=T(x + side * 0.15, y + 0.1, z))
        p.beam((x, 0.3, z), (x + math.cos(a + 1) * 0.6, 1.0, z + math.sin(a + 1) * 0.6), 0.25, '#3f8a35', d=0.02)
    return p


def yipao(name, color):
    """Jeep Willys («yipao»): pintado, con la carga de racimos de plátano y bultos de café."""
    p = Piece(name)
    body = color
    p.box((0, 0.75, 1.05), (0.72, 0.3, 0.75), body)  # capó
    p.box((0, 0.7, -0.6), (0.78, 0.35, 0.95), body)  # caja
    for x in (-0.8, 0.8):  # guardabarros
        for z in (-0.95, 1.25):
            p.box((x, 0.75, z), (0.12, 0.06, 0.45), shade(body, 0.8))
    p.box((0, 0.75, 1.82), (0.6, 0.28, 0.04), '#2a2a2a', 'Metal')  # parrilla
    for k in range(7):
        p.box((-0.42 + k * 0.14, 0.75, 1.85), (0.02, 0.22, 0.02), '#c9ccd2', 'Metal')
    for x in (-0.55, 0.55):
        p.add(cylinder(0.11, 0.11, 0.05, 10), '#fff1c8', 'Glow', mul(T(x, 0.88, 1.84), RX(math.pi / 2)))
    p.beam((-0.72, 1.05, 0.3), (-0.72, 1.75, 0.25), 0.05, '#2a2a2a', 'Metal')  # parabrisas
    p.beam((0.72, 1.05, 0.3), (0.72, 1.75, 0.25), 0.05, '#2a2a2a', 'Metal')
    p.box((0, 1.75, 0.25), (0.74, 0.03, 0.03), '#2a2a2a', 'Metal')
    p.add(box(0.7, 0.33, 0.02), '#2a3a4a', 'Glass', T(0, 1.4, 0.28))
    p.box((0, 1.0, -0.1), (0.6, 0.06, 0.25), '#3a2a20')  # asientos
    p.box((0, 1.3, -0.3), (0.6, 0.25, 0.05), '#3a2a20')
    for x in (-0.8, 0.8):
        for z in (-0.95, 1.25):
            p.add(cylinder(0.36, 0.36, 0.26, 12), '#1a1a1d', m=mul(T(x + (0.13 if x > 0 else -0.13), 0.36, z), RZ(math.pi / 2), T(0, -0.13, 0)))
            p.add(cylinder(0.16, 0.16, 0.28, 8), '#9a9da6', 'Metal', mul(T(x + (0.14 if x > 0 else -0.14), 0.36, z), RZ(math.pi / 2), T(0, -0.14, 0)))
    p.add(cylinder(0.34, 0.34, 0.22, 12), '#1a1a1d', m=mul(T(0, 0.85, -1.62), RX(math.pi / 2)))  # repuesto
    rnd = random.Random(len(name))
    for k in range(4):  # bultos de café
        p.box((-0.4 + (k % 2) * 0.8, 1.25 + (k // 2) * 0.35, -0.9 + (k % 2) * 0.1), (0.3, 0.17, 0.4), '#c9b48a')
        p.box((-0.4 + (k % 2) * 0.8, 1.25 + (k // 2) * 0.35, -0.9 + (k % 2) * 0.1), (0.31, 0.04, 0.41), '#7a5a3a')
    for k in range(3):  # racimos de plátano
        x = -0.45 + k * 0.45
        for j in range(5):
            p.add(sphere(0.17 - j * 0.015, 0.07, 0.17 - j * 0.015, 8, 4), '#8db33a', m=T(x, 1.95 - j * 0.09, -0.5))
    p.collider((-0.9, 0, -1.7), (0.9, 1.8, 1.85))
    return p


def farol_colonial(name):
    p = Piece(name)
    iron = '#1e1f24'
    p.add(cylinder(0.22, 0.18, 0.4, 8), iron, 'Metal')
    p.add(cylinder(0.08, 0.06, 2.9, 8), iron, 'Metal', T(0, 0.4, 0))
    p.add(cylinder(0.14, 0.1, 0.12, 8), iron, 'Metal', T(0, 3.25, 0))
    p.add(cylinder(0.12, 0.22, 0.08, 4, smooth=False), iron, 'Metal', T(0, 3.37, 0))
    p.add(cylinder(0.17, 0.2, 0.48, 4, smooth=False), '#ffcf7a', 'Glow', T(0, 3.45, 0))
    for k in range(4):
        a = k * math.pi / 2 + math.pi / 4
        p.rod((math.cos(a) * 0.2, 3.45, math.sin(a) * 0.2), (math.cos(a) * 0.23, 3.93, math.sin(a) * 0.23), 0.015, iron, 'Metal', seg=4)
    p.add(pyramid(0.27, 0.27, 0.3), iron, 'Metal', mul(T(0, 3.93, 0), RY(math.pi / 4)))
    p.add(sphere(0.05, seg=6, rings=4), iron, 'Metal', T(0, 4.26, 0))
    p.light((0, 3.7, 0), '#FFCF7A')
    p.collider((-0.15, 0, -0.15), (0.15, 3.9, 0.15))
    return p


def banca(name):
    p = Piece(name)
    green, wood = '#1f5f3a', '#8a5a34'
    for x in (-0.8, 0.8):
        p.box((x, 0.22, 0), (0.04, 0.22, 0.25), green, 'Metal')
        p.beam((x, 0.44, -0.22), (x, 0.9, -0.3), 0.06, green, 'Metal')
    for k in range(3):
        p.box((0, 0.46, -0.18 + k * 0.14), (0.95, 0.025, 0.055), wood)
    for k in range(2):
        p.box((0, 0.62 + k * 0.17, -0.26), (0.95, 0.05, 0.02), wood)
    p.collider((-0.95, 0, -0.3), (0.95, 0.48, 0.3))
    return p


def piezas():
    out = []
    for i, c in enumerate(PALETTES):
        out.append(casa('casa_cafetera_' + c, c, i + 1))
    out += [casa('casa_cafetera_baja_verde', 'verde', 31, floors=1, width=8.0),
            casa('casa_cafetera_baja_rojo', 'rojo', 32, floors=1, width=8.0),
            casa('casa_cafetera_ancha_azul', 'azul', 33, floors=2, width=10.0),
            iglesia('iglesia'), kiosco('kiosco'), palma_cera('palma_de_cera'), palma_cera('palma_de_cera_alta', 32.0, 2),
            platanera('platanera'), cafeto('cafeto'), guadua('guadual'), heliconia('heliconia'),
            yipao('yipao_rojo', '#c8202f'), yipao('yipao_azul', '#1b6fbc'), farol_colonial('farol_colonial'), banca('banca_parque')]
    return out
