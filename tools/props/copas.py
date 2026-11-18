"""Piezas de la ciudad de las copas: troncos gigantes que bajan a la niebla, copas enormes,
casitas de madera con ventanas encendidas, la posada, el taller de la polea, farolillos,
macetas, barriles y el catalejo del mirador."""

import math
import random

from kit import Piece, box, cylinder, sphere, gable, mix, shade, T, RY, RX, RZ, mul

BARK = '#5a3e2b'
BARK_DARK = '#3d2a1e'
MOSS = '#5d7a3a'
PLANK = '#9a7048'
PLANK_DARK = '#6e4c30'
WARM = '#ffcf7a'  # ventanas encendidas
LEAVES = ['#3f6b35', '#4c7d3c', '#365e2f', '#5b8a44']


def tronco_gigante(name, seed, height=150.0):
    """Tronco de un árbol gigante: de la niebla del fondo hasta la copa (origen en la base)."""
    p = Piece(name)
    rnd = random.Random(seed)
    p.add(cylinder(4.2, 2.7, height, 16), lambda q: mix(BARK_DARK, BARK, min(1.0, q[1] / height * 1.4)))
    for i in range(14):  # costillas de la corteza
        a = i * 2 * math.pi / 14 + rnd.random() * 0.2
        r0, r1 = 4.15, 2.65
        p.beam((math.cos(a) * r0, 0, math.sin(a) * r0), (math.cos(a) * r1, height, math.sin(a) * r1), 0.45, shade(BARK_DARK, 0.9),
               d=0.3)
    for k in range(6):  # musgo en franjas
        y = height * (0.55 + 0.07 * k)
        r = 4.2 + (2.7 - 4.2) * y / height
        a = rnd.random() * 6.28
        p.add(sphere(r * 0.45, 0.6, r * 0.45, seg=8, rings=4), MOSS, m=T(math.cos(a) * r * 0.75, y, math.sin(a) * r * 0.75))
    s = 2.3  # choca en un cuadrado algo menor que el círculo de arriba (donde se anda)
    p.collider((-s, 0, -s), (s, height, s))
    return p


def copa_gigante(name, seed, r=13.0):
    """Copa enorme (origen donde entra el tronco): ramas gruesas y nubes de hojas oscuras."""
    p = Piece(name)
    rnd = random.Random(seed)
    for i in range(6):
        a = i * 2 * math.pi / 6 + rnd.random() * 0.4
        p.rod((0, 0, 0), (math.cos(a) * r * 0.7, 3.5 + rnd.random() * 2, math.sin(a) * r * 0.7), 0.9, BARK, r1=0.35)
    blobs = [(0, 6.5, 0, r * 0.62)]
    for i in range(9):
        a = i * 2 * math.pi / 9 + rnd.random() * 0.5
        d = r * (0.55 + rnd.random() * 0.3)
        blobs.append((math.cos(a) * d, 4.0 + rnd.random() * 4.0, math.sin(a) * d, r * (0.32 + rnd.random() * 0.12)))
    for k, (x, y, z, rr) in enumerate(blobs):
        c = LEAVES[k % len(LEAVES)]
        p.add(sphere(rr, rr * 0.62, rr, seg=12, rings=7), lambda q, c=c, y=y: shade(c, 0.75 + 0.3 * max(0.0, min(1.0, (q[1] - y + 4) / 8))),
              m=T(x, y, z))
    return p


def round_window(p, m, r=0.38):
    p.add(cylinder(r + 0.09, r + 0.09, 0.08, 10), PLANK_DARK, m=mul(m, RX(math.pi / 2), T(0, -0.02, 0)))
    p.add(cylinder(r, r, 0.1, 10), WARM, 'Glow', m=mul(m, RX(math.pi / 2), T(0, -0.04, 0)))
    p.add(box(0.025, r, 0.07), PLANK_DARK, m=mul(m, T(0, 0, 0.05)))
    p.add(box(r, 0.025, 0.07), PLANK_DARK, m=mul(m, T(0, 0, 0.05)))


def casa_copa(name, wall, roof, trim, seed):
    """Casita octogonal de tablas con tejado cónico de tejuelas, ventanas redondas encendidas,
    chimenea, jardinera y un farolillo junto a la puerta (que alumbra)."""
    p = Piece(name)
    rnd = random.Random(seed)
    r, h = 2.3, 3.0
    oct_ = RY(math.pi / 8)  # una cara del octógono hacia +z (la puerta)
    p.add(cylinder(r + 0.25, r + 0.25, 0.25, 8, smooth=False), PLANK_DARK, m=oct_)  # porche
    p.add(cylinder(r, r * 0.97, h, 8, smooth=False), lambda q: shade(wall, 0.85 + 0.2 * q[1] / h), m=mul(T(0, 0.25, 0), oct_))
    for i in range(8):  # pilares en las esquinas
        a = (i + 0.5) * math.pi / 4
        p.box((math.cos(a) * r * 0.99, 0.25 + h / 2, math.sin(a) * r * 0.99), (0.1, h / 2, 0.1), trim)
    p.add(cylinder(r + 0.8, 0.15, 2.6, 8, smooth=False), lambda q: shade(roof, 0.8 + 0.3 * q[1] / 2.6), m=mul(T(0, 0.25 + h, 0), oct_))
    p.add(cylinder(r + 0.85, r + 0.85, 0.12, 8, smooth=False), shade(roof, 0.7), m=mul(T(0, 0.2 + h, 0), oct_))  # alero
    for k in range(4):  # musgo en el tejado
        a = rnd.random() * 6.28
        p.add(sphere(0.5, 0.18, 0.4, seg=6, rings=3), MOSS, m=T(math.cos(a) * 1.6, h + 1.0, math.sin(a) * 1.6))
    p.rod((1.0, h + 1.0, -0.8), (1.0, h + 3.2, -0.8), 0.22, '#7a6a5e', 'Stone')  # chimenea
    p.box((1.0, h + 3.25, -0.8), (0.3, 0.08, 0.3), '#5a4c42', 'Stone')
    # Puerta (+z), ventanas redondas a los lados y detrás.
    door_z = r * math.cos(math.pi / 8) + 0.04
    p.box((0, 0.25 + 1.0, door_z), (0.5, 1.0, 0.05), shade(trim, 0.8))
    p.box((0, 0.25 + 2.05, door_z), (0.6, 0.08, 0.07), trim)
    p.add(sphere(0.05), '#c9a24a', 'Metal', T(0.3, 1.25, door_z + 0.06))
    for a in (math.pi / 2, -math.pi / 2, math.pi, math.pi * 0.75, -math.pi * 0.75):
        d = r * math.cos(math.pi / 8) + 0.03
        m = mul(T(math.sin(a) * d, 0.25 + 1.9, math.cos(a) * d), RY(a))
        round_window(p, m)
    # Jardinera bajo una ventana y farolillo junto a la puerta.
    p.box((-1.15, 1.35, door_z - 0.35), (0.45, 0.12, 0.15), PLANK_DARK, m=RY(0.4))
    for k in range(5):
        p.add(sphere(0.1), ['#e05a6a', '#f2e6d0', '#e8a030', '#b48ce0'][k % 4], m=mul(RY(0.4), T(-1.5 + k * 0.18, 1.52, door_z - 0.35)))
    p.box((0.85, 2.35, door_z + 0.15), (0.03, 0.03, 0.18), '#2b2b2e', 'Metal')
    p.add(box(0.11, 0.15, 0.11), WARM, 'Glow', T(0.85, 2.15, door_z + 0.32))
    p.light((0.85, 2.15, door_z + 0.6), '#FFC878')
    p.collider((-r * 0.92, 0, -r * 0.92), (r * 0.92, h + 2.2, r * 0.92))
    return p


def posada_copa(name):
    """La posada: dos plantas de madera contra el tronco, tejado a dos aguas con musgo, muchas
    ventanas encendidas, un letrero colgado y dos faroles."""
    p = Piece(name)
    hx, hz, h1, h2 = 3.8, 2.6, 3.0, 2.6
    p.box((0, h1 / 2, 0), (hx, h1 / 2, hz), lambda q: shade('#b88a5a', 0.85 + 0.15 * (q[1] + h1 / 2) / h1))
    p.box((0, h1 + h2 / 2, 0), (hx + 0.35, h2 / 2, hz + 0.35), lambda q: shade('#d8c3a0', 0.9 + 0.1 * (q[1] + h2 / 2) / h2))
    for x in (-hx - 0.35, hx + 0.35):  # entramado
        for z in (-hz - 0.35, hz + 0.35):
            p.box((x, h1 + h2 / 2, z), (0.12, h2 / 2, 0.12), PLANK_DARK)
    p.box((0, h1 + 0.05, 0), (hx + 0.45, 0.1, hz + 0.45), PLANK_DARK)
    p.add(gable(hx + 0.9, hz + 1.0, 2.4), lambda q: shade('#6e3b2c', 0.8 + 0.3 * q[1] / 2.4), m=T(0, h1 + h2, 0))
    for x in (-2.5, 0.5, 2.6):
        p.add(sphere(0.7, 0.2, 0.5, seg=6, rings=3), MOSS, m=T(x, h1 + h2 + 1.1, 1.3))
    p.rod((-2.6, h1 + h2, -1.0), (-2.6, h1 + h2 + 3.0, -1.0), 0.28, '#7a6a5e', 'Stone')
    # Puerta doble y ventanas encendidas en las dos plantas.
    p.box((0, 1.15, hz + 0.04), (0.8, 1.15, 0.05), '#5a3a26')
    p.box((0, 2.4, hz + 0.06), (0.95, 0.1, 0.07), PLANK_DARK)
    for x in (-2.4, 2.4):
        p.box((x, 1.7, hz + 0.03), (0.6, 0.55, 0.05), WARM, 'Glow')
        p.box((x, 1.7, hz + 0.07), (0.03, 0.55, 0.04), PLANK_DARK)
    for x in (-2.6, -0.9, 0.9, 2.6):
        p.box((x, h1 + 1.3, hz + 0.38), (0.45, 0.5, 0.05), WARM, 'Glow')
        p.box((x, h1 + 0.7, hz + 0.5), (0.55, 0.1, 0.15), PLANK_DARK)  # jardinera
        for k in range(4):
            p.add(sphere(0.1), ['#e05a6a', '#f2e6d0', '#e8a030'][k % 3], m=T(x - 0.3 + k * 0.2, h1 + 0.86, hz + 0.52))
    for z in (-1.2, 1.2):
        p.box((hx + 0.03, 1.7, z), (0.05, 0.55, 0.5), WARM, 'Glow')
    # Letrero colgado de una vara: una taza humeante pintada.
    p.box((hx - 0.4, 2.9, hz + 0.9), (0.04, 0.04, 0.9), '#2b2b2e', 'Metal')
    p.box((hx - 0.4, 2.35, hz + 1.4), (0.05, 0.4, 0.45), '#7a4a2c')
    p.box((hx - 0.34, 2.3, hz + 1.4), (0.02, 0.16, 0.16), '#f2e6d0')
    for x in (-1.3, 1.3):
        p.box((x, 2.6, hz + 0.2), (0.03, 0.03, 0.2), '#2b2b2e', 'Metal')
        p.add(box(0.13, 0.17, 0.13), WARM, 'Glow', T(x, 2.4, hz + 0.4))
        p.light((x, 2.4, hz + 0.7), '#FFC070')
    for k, (x, z) in enumerate(((-hx - 0.6, 1.6), (-hx - 0.6, 0.6), (-hx - 1.3, 1.2))):  # barriles
        p.add(cylinder(0.38, 0.38, 0.9, 10), '#7a5236', m=T(x, 0, z))
        p.add(cylinder(0.4, 0.4, 0.06, 10), '#3a3a3e', 'Metal', m=T(x, 0.2, z))
        p.add(cylinder(0.4, 0.4, 0.06, 10), '#3a3a3e', 'Metal', m=T(x, 0.7, z))
    p.collider((-hx - 0.35, 0, -hz - 0.35), (hx + 0.35, h1 + h2 + 1.5, hz + 0.35))
    return p


def taller_copa(name):
    """El taller de la polea: cobertizo abierto con banco de trabajo, una grúa de madera que
    asoma al vacío con su cuerda y una cesta, y un farol."""
    p = Piece(name)
    hx, hz, h = 2.6, 2.0, 2.8
    for x in (-hx, hx):
        for z in (-hz, hz):
            p.box((x, h / 2, z), (0.14, h / 2, 0.14), PLANK_DARK)
    p.box((0, h / 2, -hz), (hx, h / 2, 0.08), '#a07a52')  # pared del fondo
    p.add(gable(hx + 0.5, hz + 0.5, 1.4), lambda q: shade('#4e6a5a', 0.8 + 0.3 * q[1] / 1.4), m=T(0, h, 0))
    p.box((0, 0.9, -hz + 0.6), (1.6, 0.06, 0.45), PLANK)  # banco
    for x in (-1.4, 1.4):
        p.box((x, 0.45, -hz + 0.6), (0.06, 0.45, 0.4), PLANK_DARK)
    for k in range(4):  # herramientas y engranajes
        p.add(cylinder(0.18, 0.18, 0.05, 10), '#b08a4a', 'Metal', m=mul(T(-1.0 + k * 0.6, 1.4 + 0.2 * (k % 2), -hz + 0.1), RX(math.pi / 2)))
    # La grúa: mástil, pluma hacia +x, cuerda que baja y cesta.
    p.box((hx + 0.6, 2.2, hz - 0.4), (0.13, 2.2, 0.13), PLANK_DARK)
    p.beam((hx + 0.6, 4.3, hz - 0.4), (hx + 3.4, 4.3, hz - 0.4), 0.18, PLANK_DARK)
    p.beam((hx + 0.6, 3.2, hz - 0.4), (hx + 2.4, 4.3, hz - 0.4), 0.1, PLANK_DARK)
    p.add(cylinder(0.25, 0.25, 0.12, 10), '#8a8d96', 'Metal', m=mul(T(hx + 3.3, 4.15, hz - 0.46), RX(math.pi / 2)))
    p.rod((hx + 3.3, 4.1, hz - 0.4), (hx + 3.3, 1.0, hz - 0.4), 0.02, '#c8b48a')
    p.add(cylinder(0.45, 0.55, 0.6, 10), '#a07a40', m=T(hx + 3.3, 0.4, hz - 0.4))
    p.add(box(0.11, 0.15, 0.11), WARM, 'Glow', T(-hx + 0.3, 2.3, hz))
    p.light((-hx + 0.3, 2.3, hz + 0.3), '#FFC878')
    p.collider((-hx - 0.15, 0, -hz - 0.15), (hx + 0.15, 0.95, -hz + 1.1))
    p.collider((-hx - 0.15, 0, -hz - 0.15), (hx + 0.15, h, -hz + 0.1))
    p.collider((hx + 0.45, 0, hz - 0.55), (hx + 0.75, 4.4, hz - 0.25))
    return p


def farolillo_copa(name):
    """Poste de madera curvado con un farol de cristal colgando."""
    p = Piece(name)
    p.box((0, 1.2, 0), (0.07, 1.2, 0.07), PLANK_DARK)
    p.beam((0, 2.35, 0), (0, 2.45, 0.55), 0.08, PLANK_DARK)
    p.rod((0, 2.45, 0.55), (0, 2.2, 0.55), 0.01, '#2b2b2e', 'Metal')
    p.add(cylinder(0.14, 0.11, 0.3, 6, smooth=False), WARM, 'Glow', T(0, 1.9, 0.55))
    p.add(cylinder(0.04, 0.16, 0.08, 6, smooth=False), '#2b2b2e', 'Metal', T(0, 2.2, 0.55))
    p.light((0, 2.05, 0.55), '#FFC878')
    p.collider((-0.1, 0, -0.1), (0.1, 2.4, 0.1))
    return p


def maceta(name, flower):
    p = Piece(name)
    p.add(cylinder(0.32, 0.4, 0.5, 10), '#b0603e', 'Stone')
    p.add(cylinder(0.3, 0.3, 0.04, 10), '#4a3626', m=T(0, 0.46, 0))
    rnd = random.Random(len(name))
    for k in range(7):
        a = k * 0.9
        x, z = math.cos(a) * 0.18, math.sin(a) * 0.18
        p.rod((x * 0.3, 0.48, z * 0.3), (x, 0.8 + rnd.random() * 0.2, z), 0.02, '#4f7f35')
        p.add(sphere(0.09, seg=6, rings=4), flower, m=T(x, 0.85 + rnd.random() * 0.2, z))
    p.add(sphere(0.25, 0.15, 0.25, seg=8, rings=4), '#4c7d3c', m=T(0, 0.6, 0))
    p.collider((-0.35, 0, -0.35), (0.35, 0.6, 0.35))
    return p


def barril(name):
    p = Piece(name)
    p.add(cylinder(0.38, 0.38, 0.95, 10), '#7a5236')
    for y in (0.2, 0.75):
        p.add(cylinder(0.4, 0.4, 0.06, 10), '#3a3a3e', 'Metal', m=T(0, y, 0))
    p.collider((-0.38, 0, -0.38), (0.38, 0.95, 0.38))
    return p


def catalejo(name):
    """El catalejo del mirador, en su trípode, apuntando al horizonte."""
    p = Piece(name)
    for a in (0, 2.1, 4.2):
        p.rod((0, 1.15, 0), (math.cos(a) * 0.45, 0, math.sin(a) * 0.45), 0.03, PLANK_DARK)
    p.rod((0, 1.15, -0.35), (0, 1.45, 0.75), 0.07, '#b08a4a', 'Metal', r1=0.1)
    p.add(cylinder(0.11, 0.11, 0.02, 10), '#9fc8d8', 'Glass', m=mul(T(0, 1.45, 0.75), RX(math.pi / 2 - 0.27)))
    p.collider((-0.3, 0, -0.3), (0.3, 1.3, 0.3))
    return p


def piezas():
    return [tronco_gigante('tronco_gigante', 1), copa_gigante('copa_gigante', 2), copa_gigante('copa_gigante_b', 5, 10.0),
            casa_copa('casa_copa_miel', '#c89a62', '#6e3b2c', '#5a3a26', 1),
            casa_copa('casa_copa_salvia', '#a8b48a', '#4e4038', '#4a5a3a', 2),
            casa_copa('casa_copa_rosa', '#d8a898', '#5a4a6e', '#6a4038', 3), posada_copa('posada_copa'), taller_copa('taller_copa'),
            farolillo_copa('farolillo_copa'), maceta('maceta_roja', '#e05a6a'), maceta('maceta_lila', '#b48ce0'), barril('barril'),
            catalejo('catalejo')]
