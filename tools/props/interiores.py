"""Interiores: zonas en las que se entra por una puerta (src/game/levels_interiores.cpp).

Cada uno es una sala entera con el origen en el centro del suelo y la puerta de salida en la
pared de +z: paredes, techo y suelo con su caja de choque, y lo de dentro.
  - botequim: el bar del Largo de los Arcos. Barra al fondo con el botellero y el espejo,
    toneles, mesas, el banco donde se duerme y un ventilador de techo.
  - taberna_lapa_dentro: la de la Lapa, más grande. La barra a un lado y, al fondo, el
    escenario de la samba con sus instrumentos; mesas, guirnaldas y carteles.
"""

import math
import random

from kit import Piece, box, cylinder, sphere, T, RY, RX, RZ, mul

BLACK = '#24211f'
WOOD, WOOD_DARK, WOOD_LIGHT = '#6a3f24', '#4a2c18', '#b07a48'
WHITE = '#f4ecd8'


def sala(p, w, d, h, wall, door_w=1.5, door_h=2.5, outside='#ffcf8a'):
    """Las paredes (con zócalo de azulejo), el techo con vigas y el entarimado; la puerta en
    +z, abierta a la luz de la calle (un panel que brilla)."""
    t = 0.25
    hw, hd = w / 2, d / 2
    dw = door_w / 2
    walls = [((-hw - t, 0, -hd - t), (hw + t, h, -hd)),            # fondo
             ((-hw - t, 0, -hd), (-hw, h, hd)),                    # izquierda
             ((hw, 0, -hd), (hw + t, h, hd)),                      # derecha
             ((-hw - t, 0, hd), (-dw, h, hd + t)),                 # delante, a los lados de la puerta
             ((dw, 0, hd), (hw + t, h, hd + t)),
             ((-dw, door_h, hd), (dw, h, hd + t))]                 # el dintel
    for lo, hi in walls:
        c = tuple((lo[k] + hi[k]) / 2 for k in range(3))
        e = tuple((hi[k] - lo[k]) / 2 for k in range(3))
        p.box(c, e, wall, 'Stone')
        p.collider(lo, hi)
    # Zócalo de azulejo azul y blanco por dentro (a cuadros) y la cenefa de encima.
    rnd = random.Random(int(w * 10 + d))
    def tiles(x0, x1, z0, z1, along_x):
        n = int((x1 - x0 if along_x else z1 - z0) / 0.3)
        for k in range(n):
            for row in range(3):
                u = (k + 0.5) / n
                col = '#2f66a8' if (k + row) % 2 else '#e8eef6'
                if along_x:
                    p.box((x0 + (x1 - x0) * u, 0.2 + row * 0.3, z0), ((x1 - x0) / n / 2 - 0.005, 0.145, 0.012), col, 'Stone')
                else:
                    p.box((x0, 0.2 + row * 0.3, z0 + (z1 - z0) * u), (0.012, 0.145, (z1 - z0) / n / 2 - 0.005), col, 'Stone')
    tiles(-hw, hw, -hd + 0.012, None, True)
    tiles(-hw + 0.012, None, -hd, hd, False)
    tiles(hw - 0.012, None, -hd, hd, False)
    for x0, x1 in ((-hw, -dw), (dw, hw)):
        tiles(x0, x1, hd - 0.012, None, True)
    for x0, x1, z0, z1 in ((-hw, hw, -hd + 0.02, -hd + 0.02), (-hw, -dw, hd - 0.02, hd - 0.02), (dw, hw, hd - 0.02, hd - 0.02)):
        p.box(((x0 + x1) / 2, 1.0, z0), ((x1 - x0) / 2, 0.04, 0.02), '#c8a040', 'Stone')
    for s in (-1, 1):
        p.box((s * (hw - 0.02), 1.0, 0), (0.02, 0.04, hd), '#c8a040', 'Stone')
    # Techo y vigas.
    p.box((0, h + 0.1, 0), (hw + t, 0.1, hd + t), '#e8dcc4', 'Stone')
    p.collider((-hw - t, h, -hd - t), (hw + t, h + 0.2, hd + t))
    for k in range(int(w / 1.6) + 1):
        x = -hw + 0.4 + k * (w - 0.8) / int(w / 1.6)
        p.box((x, h - 0.12, 0), (0.1, 0.12, hd), WOOD_DARK)
    # Entarimado.
    p.box((0, 0.015, 0), (hw, 0.015, hd), '#7a5232')
    for k in range(int(w / 0.45)):
        p.box((-hw + (k + 1) * 0.45, 0.031, 0), (0.008, 0.002, hd), '#5a3a22')
    # La puerta: marco, la luz de fuera y una hoja abierta hacia dentro.
    for s in (-1, 1):
        p.box((s * (dw + 0.06), door_h / 2, hd - 0.02), (0.06, door_h / 2, 0.05), WOOD)
    p.box((0, door_h + 0.06, hd - 0.02), (dw + 0.12, 0.06, 0.05), WOOD)
    p.box((0, door_h / 2, hd + t + 0.02), (dw, door_h / 2, 0.02), outside, 'Glow')
    p.add(box(dw * 0.95, door_h / 2 - 0.02, 0.03), '#2a5a3a', m=mul(T(dw, door_h / 2, hd - 0.05), RY(-1.2), T(-dw * 0.95, 0, 0)))
    return hw, hd


def ventana(p, x, z, facing_z, w=1.1, h=1.5, y=1.1, light='#f0b070'):
    """Ventana con su reja, que deja ver la tarde (brilla)."""
    if facing_z:
        p.box((x, y + h / 2, z), (w / 2, h / 2, 0.02), light, 'Glow')
        for b in range(5):
            p.box((x - w / 2 + (b + 0.5) * w / 5, y + h / 2, z - 0.03 * (1 if z > 0 else -1)), (0.012, h / 2, 0.012), BLACK, 'Metal')
        p.box((x, y - 0.05, z - 0.08 * (1 if z > 0 else -1)), (w / 2 + 0.1, 0.04, 0.1), WHITE, 'Stone')
    else:
        p.box((x, y + h / 2, z), (0.02, h / 2, w / 2), light, 'Glow')
        for b in range(5):
            p.box((x - 0.03 * (1 if x > 0 else -1), y + h / 2, z - w / 2 + (b + 0.5) * w / 5), (0.012, h / 2, 0.012), BLACK, 'Metal')
        p.box((x - 0.08 * (1 if x > 0 else -1), y - 0.05, z), (0.1, 0.04, w / 2 + 0.1), WHITE, 'Stone')


def barra(p, x0, x1, z, depth=0.6, along_x=True):
    """Barra de madera con la tapa clara y cuarterones (de cara a +z o a +x)."""
    c, e = ((x0 + x1) / 2, 0.52, z), ((x1 - x0) / 2, 0.52, depth / 2)
    if not along_x:
        c, e = (z, 0.52, (x0 + x1) / 2), (depth / 2, 0.52, (x1 - x0) / 2)
    p.box(c, e, WOOD)
    p.box((c[0], 1.07, c[2]), (e[0] + 0.05, 0.035, e[2] + 0.05), WOOD_LIGHT)
    n = int((x1 - x0) / 0.8)
    for k in range(n):
        u = x0 + (k + 0.5) * (x1 - x0) / n
        if along_x:
            p.box((u, 0.5, z + depth / 2 + 0.005), (0.3, 0.38, 0.01), '#5a3420')
        else:
            p.box((z + depth / 2 + 0.005, 0.5, u), (0.01, 0.38, 0.3), '#5a3420')
    p.collider((c[0] - e[0], 0, c[2] - e[2]), (c[0] + e[0], 1.12, c[2] + e[2]))


def botellero(p, x0, x1, zb, rnd, along_x=True, levels=(1.35, 1.8, 2.25)):
    for y in levels:
        if along_x:
            p.box(((x0 + x1) / 2, y, zb), ((x1 - x0) / 2, 0.025, 0.13), '#5a3622')
        else:
            p.box((zb, y, (x0 + x1) / 2), (0.13, 0.025, (x1 - x0) / 2), '#5a3622')
        u = x0 + 0.15
        while u < x1 - 0.1:
            tall = rnd.uniform(0.18, 0.32)
            col = rnd.choice(['#2a7d4f', '#6b2a1a', '#c8a040', '#d8e0e8', '#3a2a5a', '#8a3a20'])
            at = (u, y + 0.025, zb) if along_x else (zb, y + 0.025, u)
            p.add(cylinder(0.045, 0.04, tall, 8), col, 'Glass', T(*at))
            p.add(cylinder(0.018, 0.018, 0.08, 6), col, 'Glass', T(at[0], at[1] + tall, at[2]))
            u += rnd.uniform(0.14, 0.22)


def tonel(p, x, z, y=0.0, lying=False):
    if lying:
        p.add(cylinder(0.33, 0.33, 0.85, 12), '#8a5a34', m=mul(T(x, y + 0.33, z), RZ(math.pi / 2), T(0, -0.42, 0)))
        return
    p.add(cylinder(0.33, 0.33, 0.85, 12), '#8a5a34', m=T(x, y, z))
    for yy in (0.15, 0.7):
        p.add(cylinder(0.345, 0.345, 0.05, 12), '#3a3d46', 'Metal', T(x, y + yy, z))


def mesa(p, x, z, rnd, chairs=4, round_top=False):
    if round_top:
        p.add(cylinder(0.5, 0.5, 0.05, 14), '#a8764a', m=T(x, 0.74, z))
        p.add(cylinder(0.05, 0.05, 0.74, 6), BLACK, 'Metal', T(x, 0, z))
        p.add(cylinder(0.25, 0.25, 0.03, 10), BLACK, 'Metal', T(x, 0, z))
    else:
        p.box((x, 0.74, z), (0.45, 0.025, 0.45), '#a8764a')
        for sx in (-1, 1):
            for sz in (-1, 1):
                p.box((x + sx * 0.38, 0.37, z + sz * 0.38), (0.03, 0.37, 0.03), '#5a3622')
    p.add(cylinder(0.04, 0.04, 0.12, 6), rnd.choice(['#2a7d4f', '#c8a040']), 'Glass', T(x + 0.12, 0.765, z))
    p.add(cylinder(0.05, 0.05, 0.03, 8), '#f4e8c8', 'Glow', T(x - 0.12, 0.765, z + 0.1))  # una vela
    for k in range(chairs):
        a = k * 2 * math.pi / chairs + 0.3
        cx, cz = x + math.cos(a) * 0.78, z + math.sin(a) * 0.78
        p.box((cx, 0.45, cz), (0.2, 0.03, 0.2), '#8a5a34')
        for sx in (-1, 1):
            for sz in (-1, 1):
                p.box((cx + sx * 0.16, 0.22, cz + sz * 0.16), (0.02, 0.22, 0.02), '#5a3622')
        bx, bz = cx + math.cos(a) * 0.18, cz + math.sin(a) * 0.18  # el respaldo, hacia fuera
        p.add(box(0.2, 0.25, 0.02), '#8a5a34', m=mul(T(bx, 0.72, bz), RY(-a + math.pi / 2)))
    p.collider((x - 0.55, 0, z - 0.55), (x + 0.55, 0.78, z + 0.55))


def farol_techo(p, x, z, h, drop=0.75):
    p.rod((x, h, z), (x, h - drop, z), 0.01, BLACK, 'Metal', seg=4)
    p.add(cylinder(0.14, 0.07, 0.1, 8), BLACK, 'Metal', T(x, h - drop, z))
    p.add(sphere(0.1, 0.13, 0.1, 8, 5), '#ffd890', 'Glow', T(x, h - drop - 0.12, z))


def cartel(p, x, y, z, w, h, color, along_x=True, rnd=None):
    """Cartel enmarcado en la pared (de cara a +z si `along_x`, si no a ±x según el lado)."""
    if along_x:
        p.box((x, y, z), (w / 2 + 0.04, h / 2 + 0.04, 0.015), WOOD_DARK)
        p.box((x, y, z + 0.012 * (1 if z < 0 else -1)), (w / 2, h / 2, 0.01), color)
        if rnd:
            p.box((x, y + h * 0.2, z + 0.025 * (1 if z < 0 else -1)), (w * 0.35, h * 0.08, 0.005), rnd.choice(['#f0b040', WHITE, '#c8202f']))
    else:
        s = 1 if x < 0 else -1
        p.box((x, y, z), (0.015, h / 2 + 0.04, w / 2 + 0.04), WOOD_DARK)
        p.box((x + 0.012 * s, y, z), (0.01, h / 2, w / 2), color)
        if rnd:
            p.box((x + 0.025 * s, y + h * 0.2, z), (0.005, h * 0.08, w * 0.35), rnd.choice(['#f0b040', WHITE, '#c8202f']))


def botequim(name):
    p = Piece(name)
    rnd = random.Random(11)
    w, d, h = 9.0, 8.0, 3.4
    hw, hd = sala(p, w, d, h, '#e9c9a0')
    for x in (-2.9, 2.9):
        ventana(p, x, hd - 0.01, True)
    ventana(p, -hw + 0.01, 0.8, False)
    # La barra al fondo, el paso a la derecha; detrás, el botellero, el espejo y la pizarra.
    bx0, bx1, bz = -3.6, 1.6, -hd + 1.7
    barra(p, bx0, bx1, bz)
    p.add(cylinder(0.05, 0.05, 0.28, 8), '#c8b070', 'Metal', T(-0.6, 1.1, bz))  # el grifo
    p.box((1.0, 1.25, bz), (0.2, 0.14, 0.17), '#3a3d46', 'Metal')  # la caja registradora
    p.box((1.0, 1.4, bz - 0.05), (0.16, 0.04, 0.08), '#c8a040', 'Metal')
    for x, c, tall in ((0.0, '#f0b040', 0.13), (0.2, '#f0b040', 0.13), (-2.3, '#2a7d4f', 0.3), (-3.1, '#e8e4d8', 0.1)):
        p.add(cylinder(0.04, 0.035, tall, 8), c, 'Glass', T(x, 1.1, bz + 0.1))
    for x in (-3.0, -2.1, -1.3, 0.3):  # taburetes (sitio en medio, delante de quien atiende)
        p.add(cylinder(0.18, 0.18, 0.05, 10), '#8a5a34', m=T(x, 0.72, bz + 0.8))
        p.add(cylinder(0.035, 0.035, 0.72, 6), BLACK, 'Metal', T(x, 0, bz + 0.8))
        p.add(cylinder(0.16, 0.16, 0.02, 10), BLACK, 'Metal', T(x, 0, bz + 0.8))
    zb = -hd + 0.16
    p.box(((bx0 + bx1) / 2, 2.0, -hd + 0.02), (0.6, 0.4, 0.015), '#9fb4c0', 'Glass')  # el espejo
    p.box(((bx0 + bx1) / 2, 2.0, -hd + 0.01), (0.66, 0.46, 0.012), WOOD_LIGHT)
    botellero(p, bx0, (bx0 + bx1) / 2 - 0.75, zb, rnd)
    botellero(p, (bx0 + bx1) / 2 + 0.75, bx1, zb, rnd)
    p.box((-1.0, 2.85, -hd + 0.03), (0.55, 0.25, 0.02), '#2c332e')  # la pizarra de los precios
    for k in range(4):
        p.box((-1.0, 3.0 - k * 0.1, -hd + 0.055), (0.38 - (k % 2) * 0.12, 0.01, 0.004), '#e8e4d8')
    # Toneles en el rincón del fondo, tras el paso.
    for x, z, y in ((3.85, -3.4, 0), (3.85, -2.7, 0), (3.85, -3.05, 0.86), (3.1, -3.5, 0)):
        tonel(p, x, z, y)
    p.collider((2.7, 0, -hd), (hw, 1.75, -2.3))
    # El banco del fondo, a lo largo de la pared derecha, con su cojín y una manta.
    bxs = hw - 0.28
    p.box((bxs, 0.45, 0.9), (0.28, 0.04, 1.6), '#8a5a34')
    p.box((hw - 0.05, 0.85, 0.9), (0.03, 0.36, 1.6), '#8a5a34')
    for z in (-0.6, 0.9, 2.4):
        p.box((bxs, 0.22, z), (0.22, 0.22, 0.04), '#5a3622')
    p.box((bxs, 0.53, -0.4), (0.2, 0.07, 0.28), '#c8202f')
    p.box((bxs, 0.5, 1.6), (0.26, 0.03, 0.6), '#3d78c4')
    p.collider((hw - 0.56, 0, -0.7), (hw, 0.5, 2.5))
    # Las mesas.
    for x, z in ((-3.0, 0.6), (-1.0, 2.2), (1.6, 0.4), (-3.2, 2.7)):
        mesa(p, x, z, rnd, 3 if x > 0 else 4)
    # Carteles: el del equipo de fútbol, el santo y un calendario.
    cartel(p, -hw + 0.02, 2.1, -1.4, 0.8, 0.6, '#c8202f', False, rnd)
    cartel(p, hw - 0.02, 2.2, -1.6, 0.5, 0.7, '#e8dcc4', False, rnd)
    cartel(p, 2.9, 2.1, -hd + 0.02, 0.5, 0.6, '#2a7d4f', True, rnd)
    # El ventilador de techo y los faroles.
    p.rod((0, h, 0.6), (0, h - 0.45, 0.6), 0.03, BLACK, 'Metal', seg=6)
    p.add(cylinder(0.14, 0.14, 0.12, 10), BLACK, 'Metal', T(0, h - 0.57, 0.6))
    for k in range(4):
        p.add(box(0.55, 0.012, 0.09), WOOD_DARK, m=mul(T(0, h - 0.5, 0.6), RY(k * math.pi / 2 + 0.3), T(0.62, 0, 0)))
    for x, z in ((-2.8, 1.4), (2.0, 1.4), (-1.0, bz)):
        farol_techo(p, x, z, h)
    p.light((-2.0, 2.4, 1.4), '#FFC888')
    p.light((1.2, 2.4, -1.5), '#FFC888')
    return p


def taberna_lapa_dentro(name):
    p = Piece(name)
    rnd = random.Random(23)
    w, d, h = 11.0, 9.5, 3.8
    hw, hd = sala(p, w, d, h, '#b8443a')
    for x in (-3.3, 3.3):
        ventana(p, x, hd - 0.01, True, light='#f09060')
    # La barra, a lo largo de la pared izquierda (de cara a +x); quien atiende, detrás.
    bz0, bz1, bx = -3.2, 1.8, -hw + 1.5
    barra(p, bz0, bz1, bx, along_x=False)
    for z in (-2.6, -1.7, 0.7, 1.4):
        p.add(cylinder(0.18, 0.18, 0.05, 10), '#8a5a34', m=T(bx + 0.85, 0.72, z))
        p.add(cylinder(0.035, 0.035, 0.72, 6), BLACK, 'Metal', T(bx + 0.85, 0, z))
    for z, c in ((-0.2, '#f0b040'), (0.0, '#f0b040'), (1.2, '#2a7d4f')):
        p.add(cylinder(0.04, 0.035, 0.13, 8), c, 'Glass', T(bx + 0.1, 1.1, z))
    botellero(p, bz0, bz1, -hw + 0.16, rnd, along_x=False)
    cartel(p, -hw + 0.02, 2.95, -0.7, 1.6, 0.4, '#2c332e', False)  # la pizarra
    # El escenario de la samba, en el rincón del fondo a la derecha.
    sx0, sx1, sz0, sz1 = 1.2, hw, -hd, -hd + 2.8
    p.box(((sx0 + sx1) / 2, 0.15, (sz0 + sz1) / 2), ((sx1 - sx0) / 2, 0.15, (sz1 - sz0) / 2), WOOD_DARK)
    p.box(((sx0 + sx1) / 2, 0.305, (sz0 + sz1) / 2), ((sx1 - sx0) / 2 - 0.02, 0.005, (sz1 - sz0) / 2 - 0.02), '#7a2a1a')
    p.collider((sx0, 0, sz0), (sx1, 0.3, sz1))
    cx = (sx0 + sx1) / 2
    p.add(cylinder(0.3, 0.3, 0.6, 14), '#e8e4d8', m=T(cx - 1.2, 0.3, sz0 + 1.2))  # el surdo
    p.add(cylinder(0.31, 0.31, 0.05, 14), '#c8a040', 'Metal', T(cx - 1.2, 0.85, sz0 + 1.2))
    p.add(cylinder(0.31, 0.31, 0.05, 14), '#c8a040', 'Metal', T(cx - 1.2, 0.3, sz0 + 1.2))
    p.add(cylinder(0.16, 0.16, 0.05, 12), '#c8a040', 'Metal', mul(T(cx + 0.2, 1.1, sz0 + 1.0), RX(1.3)))  # el pandeiro
    # El cavaquinho, apoyado en un taburete.
    p.add(cylinder(0.2, 0.2, 0.45, 10), '#8a5a34', m=T(cx + 1.3, 0.3, sz0 + 1.4))
    p.add(sphere(0.18, 0.22, 0.06, 10, 6), '#c07a38', m=mul(T(cx + 1.3, 1.0, sz0 + 1.25), RX(-0.3)))
    p.add(box(0.03, 0.25, 0.015), WOOD_DARK, m=mul(T(cx + 1.3, 1.35, sz0 + 1.16), RX(-0.3)))
    p.rod((cx, 0.3, sz0 + 1.9), (cx, 1.6, sz0 + 1.9), 0.015, BLACK, 'Metal', seg=5)  # el micro
    p.add(sphere(0.05, 0.07, 0.05, 8, 5), '#3a3d46', 'Metal', T(cx, 1.66, sz0 + 1.9))
    for x in (cx - 0.6, cx + 0.6):  # dos sillas de músico
        p.box((x, 0.75, sz0 + 1.1), (0.2, 0.03, 0.2), '#8a5a34')
        for a in (-1, 1):
            for b in (-1, 1):
                p.box((x + a * 0.16, 0.52, sz0 + 1.1 + b * 0.16), (0.02, 0.22, 0.02), '#5a3622')
    # Banderines de colores sobre el escenario y la guirnalda de bombillas por la sala.
    cols = ['#f0b040', '#2a9d55', '#3d78c4', '#c8202f', WHITE]
    for k in range(9):
        u = k / 8
        x, y = sx0 + 0.2 + (sx1 - sx0 - 0.4) * u, h - 0.35 - 0.25 * math.sin(u * math.pi)
        p.add(box(0.12, 0.14, 0.005), cols[k % 5], m=mul(T(x, y - 0.12, sz1 - 0.05), RZ(math.pi / 4)))
    for z in (-1.0, 2.0):
        a, b = (-hw + 0.1, h - 0.2, z), (hw - 0.1, h - 0.2, z)
        p.rod(a, b, 0.006, BLACK, 'Metal', seg=3)
        for k in range(1, 14):
            u = k / 14
            p.add(sphere(0.05, 0.065, 0.05, 6, 4), '#ffe2a0', 'Glow', T(-hw + 0.1 + (w - 0.2) * u, h - 0.2 - 0.35 * math.sin(u * math.pi), z))
    # Mesas redondas.
    for x, z in ((-1.4, -1.6), (0.6, 0.6), (-1.2, 2.6), (2.6, 2.4), (3.6, 0.0), (1.4, -1.8)):
        mesa(p, x, z, rnd, 3, round_top=True)
    # Carteles de samba y una planta en cada rincón de delante.
    for z, c in ((-2.5, '#f0b040'), (0.5, '#2a9d55'), (2.8, '#3d78c4')):
        cartel(p, hw - 0.02, 2.2, z, 0.7, 0.9, c, False, rnd)
    for x in (-hw + 0.5, hw - 0.5):
        p.add(cylinder(0.25, 0.2, 0.45, 10), '#a0522d', 'Stone', T(x, 0, hd - 0.5))
        for k in range(6):
            a = k * 1.05
            p.add(box(0.08, 0.4, 0.015), '#2a7d4f', m=mul(T(x, 0.6, hd - 0.5), RY(a), RX(0.5), T(0, 0.3, 0)))
        p.collider((x - 0.3, 0, hd - 0.8), (x + 0.3, 0.9, hd - 0.2))
    for x, z in ((-1.0, -1.0), (2.2, 1.6), (-1.0, 2.0)):
        farol_techo(p, x, z, h, 0.9)
    p.light((-1.5, 2.6, 0.5), '#FFB878')
    p.light((cx, 2.6, sz0 + 1.6), '#FF9A70')
    return p


def piezas():
    return [botequim('botequim'), taberna_lapa_dentro('taberna_lapa_dentro')]
