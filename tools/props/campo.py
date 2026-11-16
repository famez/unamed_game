"""Piezas del campo abierto: árboles, rocas, hierba, vallas, casas de labranza, molino..."""

import math
import random

from kit import Piece, box, cylinder, sphere, rock, gable, pyramid, mix, shade, hexcol, T, RY, RX, RZ, S, mul

BARK = '#6b4a32'
BARK_DARK = '#4e3524'
LEAF = ['#5f9440', '#6fa64a', '#4f7f35', '#7ab352']
PINE = ['#2f5d3a', '#386b43', '#2a5234']
STONE = '#8d8b85'
WOOD = '#8a6440'
WHITE_WALL = '#ece4d4'
ROOF_TILE = '#b5543a'


def roble(name, seed, size=1.0):
    p = Piece(name)
    rnd = random.Random(seed)
    s = size
    p.add(cylinder(0.38 * s, 0.24 * s, 3.0 * s, 10), lambda q: mix(BARK_DARK, BARK, q[1] / (3 * s)))
    # Ramas que salen del tronco hacia la copa.
    for i in range(4):
        a = i * 1.6 + rnd.random()
        p.rod((0, 2.3 * s, 0), (math.cos(a) * 1.3 * s, 3.6 * s, math.sin(a) * 1.3 * s), 0.12 * s, BARK, r1=0.06 * s)
    # Copa: esferas que se solapan, de varios verdes.
    blobs = [(0, 4.6, 0, 2.2)] + [(math.cos(a) * 1.5, 4.0 + rnd.random() * 0.9, math.sin(a) * 1.5, 1.5 + rnd.random() * 0.4)
                                  for a in [i * 2 * math.pi / 5 + rnd.random() * 0.5 for i in range(5)]]
    for k, (x, y, z, r) in enumerate(blobs):
        c0, c1 = LEAF[k % 3], LEAF[3]
        p.add(sphere(r * s, r * 0.82 * s, r * s, 12, 8), lambda q, c0=c0, c1=c1, r=r: mix(c0, c1, max(0, q[1] / (r * s)) * 0.6),
              m=T(x * s, y * s, z * s))
    p.collider((-0.4 * s, 0, -0.4 * s), (0.4 * s, 3.2 * s, 0.4 * s))
    return p


def pino(name, seed, size=1.0):
    p = Piece(name)
    s = size
    p.add(cylinder(0.28 * s, 0.15 * s, 3.0 * s, 8), BARK_DARK)
    tiers = [(1.6, 2.4, 2.4), (2.9, 1.95, 2.2), (4.1, 1.5, 2.0), (5.2, 1.0, 1.8)]
    for k, (y, r, h) in enumerate(tiers):
        c = PINE[k % 3]
        p.add(cylinder(r * s, 0.0, h * s, 10), lambda q, c=c, h=h: mix(shade(c, 0.8), shade(c, 1.15), q[1] / (h * s)),
              m=T(0, y * s, 0))
    p.collider((-0.3 * s, 0, -0.3 * s), (0.3 * s, 3.0 * s, 0.3 * s))
    return p


def arbusto(name, seed):
    p = Piece(name)
    rnd = random.Random(seed)
    for i in range(4):
        a = i * 1.7 + rnd.random()
        r = 0.55 + rnd.random() * 0.3
        p.add(sphere(r, r * 0.8, r, 10, 6), lambda q, r=r: mix('#4c7d33', '#78ad4c', max(0, q[1] / r) * 0.8),
              m=T(math.cos(a) * 0.45, r * 0.7, math.sin(a) * 0.45))
    if seed % 2:  # con flores
        for i in range(9):
            a, d = rnd.random() * 6.28, 0.3 + rnd.random() * 0.6
            p.add(sphere(0.06, seg=6, rings=4), '#f4f1e8', 'Cloth', T(math.cos(a) * d, 0.6 + rnd.random() * 0.5, math.sin(a) * d))
    return p


def roca(name, seed, rx, ry, rz, moss=True):
    p = Piece(name)
    p.add(rock(rx, ry, rz, seed), lambda q: mix(shade(STONE, 0.85), '#6f8f4a', 0.7) if moss and q[1] > ry * 0.55 else
          shade(STONE, 0.9 + 0.2 * (q[1] / ry)), 'Stone', T(0, ry * 0.25, 0))
    p.collider((-rx * 0.8, 0, -rz * 0.8), (rx * 0.8, ry * 1.1, rz * 0.8))
    return p


def hierba(name, seed):
    p = Piece(name)
    rnd = random.Random(seed)
    for i in range(9):
        a, d = rnd.random() * 6.28, rnd.random() * 0.35
        h = 0.35 + rnd.random() * 0.35
        lean = (rnd.random() - 0.5) * 0.6
        p.add(cylinder(0.035, 0.0, h, 3, caps=False, smooth=False), lambda q, h=h: mix('#3f6e2c', '#a6cf63', q[1] / h),
              'Cloth', mul(T(math.cos(a) * d, 0, math.sin(a) * d), RZ(lean), RX(lean * 0.6)))
    return p


def flores(name, seed, petals):
    p = Piece(name)
    rnd = random.Random(seed)
    for i in range(7):
        a, d = rnd.random() * 6.28, rnd.random() * 0.4
        h = 0.3 + rnd.random() * 0.25
        x, z = math.cos(a) * d, math.sin(a) * d
        p.rod((x, 0, z), (x, h, z), 0.012, '#4f8a35', seg=4)
        p.add(sphere(0.07, 0.04, 0.07, 6, 4), petals, m=T(x, h, z))
        p.add(sphere(0.03, seg=5, rings=3), '#f2c230', m=T(x, h + 0.03, z))
    return p


def valla(name):
    p = Piece(name)
    for x in (-1.5, 0, 1.5):
        p.box((x, 0.55, 0), (0.07, 0.55, 0.07), shade(WOOD, 0.85))
        p.add(pyramid(0.07, 0.07, 0.08), shade(WOOD, 0.85), m=T(x, 1.1, 0))
    for y in (0.45, 0.85):
        p.box((0, y, 0.0), (1.55, 0.05, 0.03), WOOD)
    p.collider((-1.55, 0, -0.12), (1.55, 1.1, 0.12))
    return p


def window(p, center, facing, w, h, frame, glass='#3c4c5e', shutters=None):
    """Ventana en una fachada: marco, cristal con cruz y, si se pide, contraventanas."""
    x, y, z = center
    m = mul(T(x, y, z), RY(facing))
    p.add(box(w / 2 + 0.08, h / 2 + 0.08, 0.04), frame, m=m)
    p.add(box(w / 2, h / 2, 0.05), glass, 'Glass', m=m)
    p.add(box(0.025, h / 2, 0.07), frame, m=m)
    p.add(box(w / 2, 0.025, 0.07), frame, m=m)
    p.add(box(w / 2 + 0.14, 0.04, 0.12), frame, m=mul(m, T(0, -h / 2 - 0.08, 0.05)))  # alféizar
    if shutters:
        for sx in (-1, 1):
            p.add(box(w / 4 + 0.02, h / 2 + 0.04, 0.03), shutters, m=mul(m, T(sx * (w / 2 + w / 4 + 0.12), 0, 0.03)))


def door(p, center, facing, w, h, color, frame):
    x, y, z = center
    m = mul(T(x, y, z), RY(facing))
    p.add(box(w / 2 + 0.1, h / 2 + 0.06, 0.04), frame, m=mul(m, T(0, h / 2, 0)))
    p.add(box(w / 2, h / 2, 0.07), color, m=mul(m, T(0, h / 2 - 0.03, 0)))
    for k in range(3):  # tablas
        p.add(box(0.012, h / 2 - 0.1, 0.08), shade(color, 0.8), m=mul(m, T((k - 1) * w / 3, h / 2, 0)))
    p.add(sphere(0.04), '#c9a24a', 'Metal', mul(m, T(w * 0.33, h * 0.5, 0.09)))


def casa_campo(name, wall=WHITE_WALL, roof=ROOF_TILE, trim='#5b7f9e'):
    p = Piece(name)
    hx, hz, h = 4.0, 3.0, 3.6
    p.box((0, 0.25, 0), (hx + 0.15, 0.25, hz + 0.15), STONE, 'Stone')  # zócalo de piedra
    p.box((0, 0.5 + h / 2, 0), (hx, h / 2, hz), wall)
    for x in (-hx, hx):  # esquinas de piedra
        for z in (-hz, hz):
            p.box((x, 0.5 + h / 2, z), (0.18, h / 2, 0.18), shade(STONE, 1.1), 'Stone')
    p.add(gable(hx + 0.6, hz + 0.7, 2.2), lambda q: shade(roof, 0.85 + 0.25 * q[1] / 2.2), m=T(0, 0.5 + h, 0))
    p.box((2.2, 0.5 + h + 1.6, -1.0), (0.35, 1.0, 0.35), shade(STONE, 1.05), 'Stone')  # chimenea
    door(p, (0, 0.5, hz + 0.02), 0, 1.1, 2.1, '#6b4a32', trim)
    for x in (-2.4, 2.4):
        window(p, (x, 2.3, hz + 0.02), 0, 0.9, 1.0, trim, shutters=trim)
        p.box((x, 1.55, hz + 0.25), (0.55, 0.12, 0.15), '#7a5236')  # jardinera
        for k in range(5):
            p.add(sphere(0.09), ['#d94060', '#f2f2f2', '#e8a030'][k % 3], m=T(x - 0.4 + k * 0.2, 1.72, hz + 0.27))
    for z in (-1.4, 1.4):
        window(p, (hx + 0.02, 2.3, z), math.pi / 2, 0.9, 1.0, trim, shutters=trim)
    window(p, (0, 2.3, -hz - 0.02), math.pi, 0.9, 1.0, trim, shutters=trim)
    # Farol junto a la puerta.
    p.box((0.95, 2.75, hz + 0.12), (0.03, 0.03, 0.15), '#2b2b2e', 'Metal')
    p.add(box(0.1, 0.14, 0.1), '#ffd58a', 'Glow', T(0.95, 2.62, hz + 0.27))
    p.light((0.95, 2.62, hz + 0.45), '#FFD08A')
    p.collider((-hx - 0.15, 0, -hz - 0.15), (hx + 0.15, 0.5 + h + 1.2, hz + 0.15))
    return p


def granero(name):
    p = Piece(name)
    hx, hz, h = 5.0, 4.0, 4.5
    red = '#9b3b2e'
    p.box((0, h / 2, 0), (hx, h / 2, hz), lambda q: shade(red, 0.9 + 0.15 * (q[1] + h / 2) / h))
    for x in (-hx, hx):
        for z in (-hz, hz):
            p.box((x, h / 2, z), (0.15, h / 2, 0.15), '#efe8da')
    p.add(gable(hx + 0.5, hz + 0.5, 3.0), '#4b4044', m=T(0, h, 0))
    # Portón con la cruz blanca.
    p.box((0, 1.6, hz + 0.03), (1.6, 1.6, 0.05), shade(red, 0.8))
    p.beam((-1.5, 0.1, hz + 0.09), (1.5, 3.1, hz + 0.09), 0.14, '#efe8da')
    p.beam((1.5, 0.1, hz + 0.09), (-1.5, 3.1, hz + 0.09), 0.14, '#efe8da')
    p.box((0, 3.2, hz + 0.08), (1.7, 0.08, 0.06), '#efe8da')
    p.box((0, 0.05, hz + 0.08), (1.7, 0.08, 0.06), '#efe8da')
    for sx in (-1.6, 1.6):
        p.box((sx, 1.6, hz + 0.08), (0.08, 1.6, 0.06), '#efe8da')
    window(p, (0, h + 1.3, hz + 0.02), 0, 0.9, 0.9, '#efe8da')
    p.collider((-hx, 0, -hz), (hx, h + 1.5, hz))
    return p


def molino(name):
    p = Piece(name)
    p.add(cylinder(2.3, 1.7, 9.0, 16), lambda q: shade('#efe9dc', 0.92 + 0.1 * q[1] / 9))
    p.add(cylinder(1.9, 0.0, 2.4, 16), '#6e4b35', m=T(0, 9.0, 0))
    door(p, (0, 0, 2.2), 0, 1.0, 2.0, '#5c3f2a', '#efe9dc')
    window(p, (0, 5.0, 2.0), 0, 0.6, 0.8, '#5c3f2a')
    hub = (0, 8.4, 2.1)
    p.add(cylinder(0.3, 0.25, 0.6, 10), '#4b3526', m=mul(T(*hub), RX(math.pi / 2)))
    for k in range(4):
        a = k * math.pi / 2 + 0.3
        tip = (math.cos(a) * 6.0, 8.4 + math.sin(a) * 6.0, 2.5)
        p.beam((hub[0], hub[1], 2.5), tip, 0.18, '#5c3f2a')
        # Vela: rectángulo de lona a un lado de la aspa.
        side = (-math.sin(a), math.cos(a))
        cx, cy = math.cos(a) * 3.6 + side[0] * 0.55, 8.4 + math.sin(a) * 3.6 + side[1] * 0.55
        p.add(box(0.5, 2.3, 0.03), '#e9dfc8', m=mul(T(cx, cy, 2.55), RZ(a - math.pi / 2)))
    p.collider((-2.0, 0, -2.0), (2.0, 9.0, 2.0))
    return p


def pozo(name):
    p = Piece(name)
    p.add(cylinder(0.95, 0.95, 0.85, 14), STONE, 'Stone')
    p.add(cylinder(0.7, 0.7, 0.02, 14), '#2c4a5a', 'Water', T(0, 0.8, 0))
    for x in (-0.8, 0.8):
        p.box((x, 1.3, 0), (0.07, 1.3, 0.07), WOOD)
    p.add(gable(1.1, 0.75, 0.5), ROOF_TILE, m=T(0, 2.55, 0))
    p.rod((-0.8, 1.9, 0), (0.8, 1.9, 0), 0.06, BARK, seg=8)
    p.rod((0, 1.9, 0), (0, 1.2, 0), 0.01, '#d8c9a0', seg=4)
    p.add(cylinder(0.14, 0.12, 0.22, 8), '#6b4a32', m=T(0, 1.0, 0))
    p.collider((-0.95, 0, -0.95), (0.95, 0.85, 0.95))
    return p


def farol_camino(name):
    p = Piece(name)
    p.box((0, 1.4, 0), (0.08, 1.4, 0.08), shade(WOOD, 0.8))
    p.box((0, 2.75, 0.3), (0.05, 0.05, 0.35), shade(WOOD, 0.8))
    p.rod((0, 2.7, 0.6), (0, 2.5, 0.6), 0.01, '#2b2b2e', seg=4)
    p.add(box(0.12, 0.03, 0.12), '#2b2b2e', 'Metal', T(0, 2.48, 0.6))
    p.add(box(0.09, 0.14, 0.09), '#ffd58a', 'Glow', T(0, 2.33, 0.6))
    p.add(box(0.11, 0.02, 0.11), '#2b2b2e', 'Metal', T(0, 2.18, 0.6))
    p.light((0, 2.33, 0.6), '#FFD08A')
    p.collider((-0.12, 0, -0.12), (0.12, 2.8, 0.12))
    return p


def tronco(name):
    p = Piece(name)
    p.add(cylinder(0.38, 0.32, 4.0, 10), BARK, m=mul(T(-2.0, 0.36, 0), RZ(-math.pi / 2)))
    for x in (-2.02, 1.99):  # anillos del corte
        p.add(cylinder(0.3, 0.3, 0.03, 10), '#c79a64', m=mul(T(x, 0.36, 0), RZ(-math.pi / 2)))
    p.add(sphere(0.25, 0.15, 0.3), '#6fa04a', m=T(0.6, 0.72, 0.05))
    p.collider((-2.0, 0, -0.38), (2.0, 0.74, 0.38))
    return p


def tocon(name):
    p = Piece(name)
    p.add(cylinder(0.5, 0.4, 0.6, 10), BARK)
    p.add(cylinder(0.38, 0.38, 0.02, 10), '#c79a64', m=T(0, 0.6, 0))
    for k in range(4):
        a = k * 1.57 + 0.4
        p.rod((0, 0.1, 0), (math.cos(a) * 0.75, 0.0, math.sin(a) * 0.75), 0.1, BARK_DARK, r1=0.04)
    p.collider((-0.45, 0, -0.45), (0.45, 0.6, 0.45))
    return p


def fardo(name):
    p = Piece(name)
    p.add(cylinder(0.7, 0.7, 1.3, 14), lambda q: mix('#c9a54a', '#e2c56a', 0.5 + 0.5 * math.sin(q[1] * 14)),
          m=mul(T(-0.65, 0.7, 0), RZ(-math.pi / 2)))
    p.collider((-0.65, 0, -0.7), (0.65, 1.4, 0.7))
    return p


def senal(name):
    p = Piece(name)
    p.box((0, 1.2, 0), (0.06, 1.2, 0.06), shade(WOOD, 0.8))
    for y, a in ((2.05, 0.5), (1.7, -0.9)):
        m = mul(T(0, y, 0), RY(a))
        p.add(box(0.5, 0.12, 0.03), WOOD, m=mul(m, T(0.35, 0, 0)))
        p.add(pyramid(0.12, 0.03, 0.15), WOOD, m=mul(m, T(0.85, 0, 0), RZ(-math.pi / 2)))
    p.collider((-0.1, 0, -0.1), (0.1, 2.3, 0.1))
    return p


def puente(name):
    p = Piece(name)
    for z in [i * 0.5 - 4.75 for i in range(20)]:
        p.box((0, 0.55, z), (1.5, 0.08, 0.22), shade(WOOD, 0.9 + 0.1 * ((int(z * 2) % 3) / 2)))
    for x in (-1.6, 1.6):
        p.box((x, 0.45, 0), (0.1, 0.12, 5.0), shade(WOOD, 0.75))
        for z in (-4.8, -1.6, 1.6, 4.8):
            p.box((x, 0.9, z), (0.07, 0.6, 0.07), shade(WOOD, 0.75))
        p.box((x, 1.45, 0), (0.05, 0.05, 5.0), WOOD)
    p.collider((-1.6, 0, -5.0), (1.6, 0.63, 5.0))
    for x in (-1.6, 1.6):
        p.collider((x - 0.08, 0.63, -5.0), (x + 0.08, 1.5, 5.0))
    return p


def piezas():
    out = [roble('roble', 1), roble('roble_grande', 4, 1.35), pino('pino', 2), pino('pino_alto', 3, 1.4), arbusto('arbusto', 2),
           arbusto('arbusto_flores', 1), roca('roca', 3, 1.4, 1.0, 1.2), roca('roca_grande', 7, 3.0, 2.2, 2.5),
           roca('piedra', 11, 0.5, 0.35, 0.45, moss=False), hierba('hierba', 5), flores('flores_amarillas', 6, '#f2d33a'),
           flores('flores_lila', 8, '#b48ce0'), valla('valla'), casa_campo('casa_campo'),
           casa_campo('casa_campo_verde', '#e8dfc6', '#7a4a3a', '#4f7d4a'), granero('granero'), molino('molino'), pozo('pozo'),
           farol_camino('farol_camino'), tronco('tronco_caido'), tocon('tocon'), fardo('fardo_heno'), senal('senal_camino'),
           puente('puente_madera')]
    return out
