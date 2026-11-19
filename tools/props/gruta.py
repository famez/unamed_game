"""La gruta del campo (src/game/levels_gruta.cpp): lo que adorna sus diez salas y su boca.

  - estalagmita_1..3 y estalactita_1..3: conos de roca con anillos; las estalactitas cuelgan
    (el origen en el techo, la pieza hacia abajo).
  - columna_cueva: estalactita y estalagmita que se han juntado.
  - cristales_cueva: racimo de prismas que brillan (la luz la pone el juego).
  - raices_colgantes: raíces que bajan del techo en la Sala de las Raíces.
  - setas_cueva: un corro de setas que brillan un poco.
  - altar_gruta: el altar del santuario, con las piedras talladas y las velas.
  - boca_gruta: la entrada en la peña del campo, un arco de roca con la oscuridad dentro.
  - grieta_gruta: la salida estrecha por la que se vuelve al campo.
"""

import math
import random

from kit import Piece, box, cylinder, sphere, rock, mix, shade, T, RY, RX, RZ, S, mul

ROCK, ROCK_DARK, ROCK_WET = '#6f6a66', '#4a4643', '#5a5f66'


def cono(p, r, h, seed, hanging=False, at=(0, 0, 0), col=ROCK):
    """Cono de roca con anillos irregulares (de pie o colgando)."""
    rnd = random.Random(seed)
    x, y, z = at
    n = 5
    for k in range(n):
        r0 = r * (1 - k / n) ** 0.9 * rnd.uniform(0.9, 1.1)
        r1 = r * (1 - (k + 1) / n) ** 0.9 * rnd.uniform(0.9, 1.1) + 0.01
        hk = h / n
        c = shade(col, 0.85 + 0.25 * rnd.random())
        if hanging:
            p.add(cylinder(r1, r0, hk, 9, smooth=False), c, 'Stone', T(x, y - (k + 1) * hk, z))
        else:
            p.add(cylinder(r0, r1, hk, 9, smooth=False), c, 'Stone', T(x, y + k * hk, z))


def estalagmita(name, seed, r, h):
    p = Piece(name)
    cono(p, r, h, seed)
    rnd = random.Random(seed + 9)
    for _ in range(2):  # dos pequeñas al pie
        a = rnd.random() * 6.28
        cono(p, r * 0.4, h * 0.35, seed + 3, at=(math.cos(a) * r * 1.1, 0, math.sin(a) * r * 1.1))
    p.collider((-r * 0.7, 0, -r * 0.7), (r * 0.7, h * 0.8, r * 0.7))
    return p


def estalactita(name, seed, r, h):
    p = Piece(name)
    p.add(rock(r * 1.4, 0.3, r * 1.4, seed), ROCK_DARK, 'Stone', T(0, -0.1, 0))  # el arranque en el techo
    cono(p, r, h, seed, hanging=True, col=ROCK_WET)
    rnd = random.Random(seed + 4)
    for _ in range(3):
        a = rnd.random() * 6.28
        cono(p, r * 0.35, h * rnd.uniform(0.3, 0.5), seed + 7, hanging=True, at=(math.cos(a) * r * 0.9, 0, math.sin(a) * r * 0.9),
             col=ROCK_WET)
    return p


def columna(name):
    p = Piece(name)
    cono(p, 1.1, 4.2, 3)
    cono(p, 0.9, 4.2, 5, hanging=True, at=(0, 8.0, 0), col=ROCK_WET)
    p.add(cylinder(0.32, 0.32, 1.0, 9, smooth=False), ROCK, 'Stone', T(0, 3.5, 0))
    p.collider((-0.8, 0, -0.8), (0.8, 7.8, 0.8))
    return p


def cristales(name, color, seed):
    p = Piece(name)
    rnd = random.Random(seed)
    p.add(rock(0.7, 0.35, 0.6, seed), ROCK_DARK, 'Stone')
    for k in range(7):
        a, d = rnd.random() * 6.28, rnd.uniform(0, 0.4)
        h = rnd.uniform(0.5, 1.3)
        lean_x, lean_z = rnd.uniform(-0.45, 0.45), rnd.uniform(-0.45, 0.45)
        m = mul(T(math.cos(a) * d, 0.15, math.sin(a) * d), RZ(lean_x), RX(lean_z))
        p.add(cylinder(0.11, 0.11, h * 0.8, 6, smooth=False), mix(color, '#ffffff', 0.15), 'Glow', m)
        p.add(cylinder(0.11, 0.0, h * 0.25, 6, smooth=False), mix(color, '#ffffff', 0.4), 'Glow', mul(m, T(0, h * 0.8, 0)))
    p.collider((-0.5, 0, -0.5), (0.5, 0.9, 0.5))
    return p


def raices(name, seed):
    """Raíces que bajan del techo (el origen arriba), retorciéndose."""
    p = Piece(name)
    rnd = random.Random(seed)
    for k in range(9):
        x, z = rnd.uniform(-1.4, 1.4), rnd.uniform(-1.4, 1.4)
        y, r = 0.0, rnd.uniform(0.05, 0.11)
        length = rnd.uniform(2.0, 4.5)
        steps = 5
        for s in range(steps):
            nx, nz = x + rnd.uniform(-0.3, 0.3), z + rnd.uniform(-0.3, 0.3)
            ny = y - length / steps
            p.rod((x, y, z), (nx, ny, nz), r * (1 - s / steps) + 0.015, mix('#5a3d28', '#3a2a1c', rnd.random()), seg=5)
            x, y, z = nx, ny, nz
        if rnd.random() < 0.5:  # algunas acaban en un musgo que brilla
            p.add(sphere(0.08, 0.1, 0.08, 6, 4), '#7fe0a0', 'Glow', T(x, y, z))
    return p


def setas(name, seed):
    p = Piece(name)
    rnd = random.Random(seed)
    for k in range(7):
        a, d = rnd.random() * 6.28, rnd.uniform(0.1, 0.7)
        x, z = math.cos(a) * d, math.sin(a) * d
        h = rnd.uniform(0.12, 0.35)
        p.add(cylinder(0.025, 0.02, h, 6), '#e8e2d0', 'Cloth', T(x, 0, z))
        c = rnd.choice(['#5fe0c8', '#9fe070', '#b890ff'])
        p.add(sphere(h * 0.45, h * 0.2, h * 0.45, 8, 4), c, 'Glow', T(x, h, z))
    return p


def altar(name):
    """El altar del santuario: losa sobre dos piedras, con marcas talladas que brillan, velas
    gastadas y dos estelas de piedra a los lados."""
    p = Piece(name)
    stone = '#8a8278'
    p.add(rock(0.5, 0.5, 0.5, 2, 0.12), stone, 'Stone', T(-0.8, 0.4, 0))
    p.add(rock(0.5, 0.5, 0.5, 4, 0.12), stone, 'Stone', T(0.8, 0.4, 0))
    p.box((0, 0.95, 0), (1.4, 0.12, 0.6), shade(stone, 1.1), 'Stone')
    for k in range(5):
        p.box((-1.0 + k * 0.5, 0.95, 0.605), (0.12, 0.05, 0.005), '#ffd27a', 'Glow')
    rnd = random.Random(7)
    for k in range(6):
        x, z, h = rnd.uniform(-1.2, 1.2), rnd.uniform(-0.4, 0.4), rnd.uniform(0.08, 0.22)
        p.add(cylinder(0.04, 0.04, h, 8), '#f0e8d0', m=T(x, 1.07, z))
        p.add(sphere(0.025, 0.05, 0.025, 6, 4), '#ffc060', 'Glow', T(x, 1.07 + h + 0.03, z))
    for s in (-1, 1):
        p.box((s * 2.2, 1.3, -0.3), (0.35, 1.3, 0.14), shade(stone, 0.9), 'Stone')
        for k in range(4):
            p.box((s * 2.2, 0.7 + k * 0.45, -0.155), (0.22 - (k % 2) * 0.08, 0.04, 0.005), '#ffd27a', 'Glow')
        p.collider((s * 2.2 - 0.35, 0, -0.45), (s * 2.2 + 0.35, 2.6, -0.15))
    p.collider((-1.4, 0, -0.6), (1.4, 1.1, 0.6))
    return p


def boca(name):
    """La boca de la gruta en la peña: rocas que hacen arco y, dentro, la oscuridad. Mira a +z;
    se entra por el hueco de 3 m de ancho."""
    p = Piece(name)
    rnd = random.Random(31)
    for k in range(13):  # el arco, de un pie al otro
        a = math.pi * k / 12
        x, y = math.cos(a) * 2.6, math.sin(a) * 3.4
        s = rnd.uniform(0.9, 1.4)
        p.add(rock(s, s * 0.9, s * 1.1, 40 + k), lambda q: mix(shade(ROCK, 0.9), '#6f8f4a', 0.6) if q[1] > 0.4 else ROCK,
              'Stone', T(x * 1.15, max(0.0, y - 0.3), 0.3))
    for s in (-1, 1):
        p.collider((s * 2.0 - 1.2 if s < 0 else 2.0, 0, -0.8), (-2.0 if s < 0 else 3.2, 4.0, 1.4))
        p.add(rock(1.8, 1.6, 1.8, 60 + s), ROCK, 'Stone', T(s * 3.6, 0.6, -0.6))
    p.collider((-3.2, 3.2, -0.8), (3.2, 5.0, 1.4))
    p.box((0, 1.8, -1.2), (2.2, 1.9, 0.05), '#050405', 'Cloth')  # lo oscuro de dentro
    p.add(cylinder(2.1, 2.1, 0.1, 16), '#050405', 'Cloth', mul(T(0, 3.6, -1.25), RX(math.pi / 2)))
    for k in range(5):  # hiedra que cuelga del arco
        x = -1.6 + k * 0.8
        p.rod((x, 3.6, 0.9), (x + rnd.uniform(-0.2, 0.2), 2.4 - rnd.random() * 0.6, 1.0), 0.04, '#3f6e2c', seg=4)
    return p


def grieta(name):
    """La salida estrecha: una raja vertical en la roca, con luz de fuera. Mira a +z."""
    p = Piece(name)
    for s in (-1, 1):
        for k in range(4):
            p.add(rock(1.2, 1.1, 1.0, 70 + k + (s > 0) * 10), ROCK, 'Stone', T(s * 1.5, k * 1.4, 0))
        p.collider((s * 0.75 - (1.5 if s < 0 else 0), 0, -1.0), (s * 0.75 + (0 if s < 0 else 1.5), 5.5, 1.0))
    p.box((0, 2.0, -0.6), (0.7, 2.0, 0.05), '#ffe2b0', 'Glow')
    return p


def piezas():
    out = [estalagmita('estalagmita_1', 1, 0.6, 2.2), estalagmita('estalagmita_2', 2, 0.9, 3.4), estalagmita('estalagmita_3', 3, 0.4, 1.3),
           estalactita('estalactita_1', 4, 0.5, 2.0), estalactita('estalactita_2', 5, 0.8, 3.2), estalactita('estalactita_3', 6, 0.35, 1.2),
           columna('columna_cueva'), cristales('cristales_cian', '#5fe8ff', 1), cristales('cristales_violeta', '#b890ff', 2),
           cristales('cristales_verde', '#7fffa0', 3), raices('raices_colgantes', 1), setas('setas_cueva', 1), altar('altar_gruta'),
           boca('boca_gruta'), grieta('grieta_gruta')]
    return out
