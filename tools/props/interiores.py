"""Interiores: zonas en las que se entra por una puerta (src/game/levels_interiores.cpp).

Cada uno es una sala entera con el origen en el centro del suelo y la puerta de salida en la
pared de +z: paredes, techo y suelo con su caja de choque, y lo de dentro.
  - botequim: el bar del Largo de los Arcos, de dos alturas. La barra al fondo bajo la
    galería, la chimenea con sus butacas y la estantería, la escalera a la galería de arriba
    con tres cuartos (uno abierto, con su cama) y la rueda de velas en medio.
  - taberna_lapa_dentro: la de la Lapa, más grande. La barra a un lado bajo la galería, el
    escenario de la samba al fondo, la chimenea, la estantería y los cuartos de arriba.
Las dos, en tonos oscuros: lo que alumbra es el fuego, las velas y los apliques.
"""

import math
import random

from kit import Piece, I, S, apply, box, cylinder, shade, sphere, T, RY, RX, RZ, mul

BLACK = '#24211f'
WOOD, WOOD_DARK, WOOD_LIGHT = '#6a3f24', '#4a2c18', '#b07a48'
WHITE = '#f4ecd8'


class Frame:
    """Marco local girado (y, con `mirror`, reflejado en x) y desplazado: lo que se añade en él acaba en `parent` (una pieza u otro
    marco). Así una chimenea, una escalera o una habitación se modelan una vez, con la pared en
    z = 0 y la sala hacia +z, y se ponen contra cualquier pared."""

    def __init__(self, parent, yaw, off=(0, 0, 0), mirror=False):
        self.parent = parent
        self.F = mul(T(*off), RY(yaw), S(-1, 1, 1) if mirror else I)

    def add(self, prim, color, mat='Cloth', m=I):
        self.parent.add(prim, color, mat, mul(self.F, m))
        return self

    box, beam, rod = Piece.box, Piece.beam, Piece.rod

    def pt(self, q):
        return tuple(apply(self.F, q))

    def collider(self, lo, hi):
        cs = [self.pt((x, y, z)) for x in (lo[0], hi[0]) for y in (lo[1], hi[1]) for z in (lo[2], hi[2])]
        self.parent.collider(tuple(min(c[k] for c in cs) for k in range(3)), tuple(max(c[k] for c in cs) for k in range(3)))
        return self

    def light(self, pos, color='#FFD9A0'):
        self.parent.light(self.pt(pos), color)
        return self


def pared(p, x0, x1, y0, y1, z0, z1, color, holes=(), along_x=True):
    """Pared maciza de x0 a x1 (o de z0 a z1 si no va a lo largo de x) con huecos
    (desde, hasta, abajo, arriba), cada uno con su caja de choque."""
    a0, a1 = (x0, x1) if along_x else (z0, z1)
    pieces, cur = [], a0
    for h0, h1, hy0, hy1 in sorted(holes):
        pieces.append((cur, h0, y0, y1))
        if hy0 > y0:
            pieces.append((h0, h1, y0, hy0))
        if hy1 < y1:
            pieces.append((h0, h1, hy1, y1))
        cur = h1
    pieces.append((cur, a1, y0, y1))
    for u0, u1, v0, v1 in pieces:
        if u1 - u0 < 1e-3 or v1 - v0 < 1e-3:
            continue
        lo, hi = ((u0, v0, z0), (u1, v1, z1)) if along_x else ((x0, v0, u0), (x1, v1, u1))
        p.box(tuple((lo[k] + hi[k]) / 2 for k in range(3)), tuple((hi[k] - lo[k]) / 2 for k in range(3)), color, 'Stone')
        p.collider(lo, hi)


def sala(p, w, d, h, wall, door_w=1.5, door_h=2.5, outside='#ffcf8a', holes=None, ceiling='#e8dcc4', floor='#7a5232',
         tile=('#2f66a8', '#e8eef6'), trim='#c8a040', beams=WOOD_DARK):
    """Las paredes (con zócalo de azulejo), el techo con vigas y el entarimado; la puerta en
    +z, abierta a la luz de la calle (un panel que brilla). `holes`: huecos por pared ('back',
    'left', 'right'), como en `pared`."""
    holes = holes or {}
    t = 0.25
    hw, hd = w / 2, d / 2
    dw = door_w / 2
    pared(p, -hw - t, hw + t, 0, h, -hd - t, -hd, wall, holes.get('back', ()))
    pared(p, -hw - t, -hw, 0, h, -hd, hd, wall, holes.get('left', ()), along_x=False)
    pared(p, hw, hw + t, 0, h, -hd, hd, wall, holes.get('right', ()), along_x=False)
    pared(p, -hw - t, hw + t, 0, h, hd, hd + t, wall, [(-dw, dw, 0, door_h)])
    p.collider((-dw, 0, hd), (dw, door_h, hd + t))  # por la puerta se sale con F: el hueco no deja pasar (ni a la cámara)
    # Zócalo de azulejo por dentro (a cuadros) y la cenefa de encima.
    def tiles(x0, x1, z0, z1, along_x):
        n = int((x1 - x0 if along_x else z1 - z0) / 0.3)
        for k in range(n):
            for row in range(3):
                u = (k + 0.5) / n
                col = tile[0] if (k + row) % 2 else tile[1]
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
        p.box(((x0 + x1) / 2, 1.0, z0), ((x1 - x0) / 2, 0.04, 0.02), trim, 'Stone')
    for s in (-1, 1):
        p.box((s * (hw - 0.02), 1.0, 0), (0.02, 0.04, hd), trim, 'Stone')
    # Techo y vigas.
    p.box((0, h + 0.1, 0), (hw + t, 0.1, hd + t), ceiling, 'Stone')
    p.collider((-hw - t, h, -hd - t), (hw + t, h + 0.2, hd + t))
    for k in range(int(w / 1.6) + 1):
        x = -hw + 0.4 + k * (w - 0.8) / int(w / 1.6)
        p.box((x, h - 0.12, 0), (0.1, 0.12, hd), beams)
    # Entarimado.
    p.box((0, 0.015, 0), (hw, 0.015, hd), floor)
    for k in range(int(w / 0.45)):
        p.box((-hw + (k + 1) * 0.45, 0.031, 0), (0.008, 0.002, hd), shade(floor, 0.72))
    # La puerta: marco, la luz de fuera y una hoja abierta hacia dentro.
    for s in (-1, 1):
        p.box((s * (dw + 0.06), door_h / 2, hd - 0.02), (0.06, door_h / 2, 0.05), WOOD)
    p.box((0, door_h + 0.06, hd - 0.02), (dw + 0.12, 0.06, 0.05), WOOD)
    p.box((0, door_h / 2, hd + t + 0.02), (dw, door_h / 2, 0.02), outside, 'Glow')
    # La hoja, abierta del todo contra la pared (hacia dentro, a medias, tapaba la vista al entrar).
    p.add(box(dw * 0.95, door_h / 2 - 0.02, 0.03), '#2a5a3a', m=mul(T(dw + 0.04, door_h / 2, hd - 0.05), RY(-2.92), T(-dw * 0.95, 0, 0)))
    return hw, hd


# --- Lo que hace acogedora una sala ------------------------------------------------------------
# Todo en un marco local: la pared en z = 0 y la sala hacia +z.

STONE, STONE_DARK, SOOT = '#6e6258', '#4f463f', '#1b1410'


def chimenea(f, ceiling, rnd):
    """Chimenea de piedra con su repisa, los troncos, las ascuas, los morillos, la leña al lado
    y el atizador. El fuego que baila y alumbra lo pone el juego (Game::hearths_), en
    FUEGO_CHIMENEA respecto al marco."""
    f.box((0, (1.45 + ceiling) / 2, 0.27), (1.0, (ceiling - 1.45) / 2, 0.27), STONE)  # el tiro
    for s in (-1, 1):
        f.box((s * 0.81, 0.575, 0.3), (0.19, 0.575, 0.3), STONE)  # las jambas
        for k in range(4):  # sillares marcados
            f.box((s * 0.81, 0.14 + k * 0.29, 0.605), (0.18, 0.005, 0.005), STONE_DARK, 'Stone')
    f.box((0, 0.575, 0.04), (0.62, 0.575, 0.04), SOOT, 'Stone')  # el fondo, tiznado
    for s in (-1, 1):
        f.box((s * 0.6, 0.575, 0.3), (0.02, 0.575, 0.28), '#2a211c', 'Stone')
    f.box((0, 1.25, 0.32), (1.12, 0.1, 0.34), WOOD_DARK)  # la viga
    f.box((0, 1.38, 0.36), (1.22, 0.035, 0.4), WOOD)       # la repisa
    f.box((0, 0.06, 0.55), (1.3, 0.06, 0.55), STONE_DARK, 'Stone')  # el hogar, que se pisa
    f.box((0, 0.125, 0.3), (0.6, 0.005, 0.26), '#2a1c14')
    # Los morillos y los troncos; debajo, las ascuas.
    for s in (-1, 1):
        f.box((s * 0.32, 0.2, 0.42), (0.02, 0.08, 0.16), BLACK, 'Metal')
        f.box((s * 0.32, 0.32, 0.56), (0.025, 0.025, 0.025), BLACK, 'Metal')
    for x, z, a in ((-0.05, 0.3, 0.15), (0.08, 0.42, -0.2)):
        f.add(cylinder(0.075, 0.07, 0.8, 9), '#4a3020', m=mul(T(x, 0.26, z), RY(a), RZ(math.pi / 2), T(0, -0.4, 0)))
    f.add(cylinder(0.06, 0.06, 0.55, 8), '#3a2418', m=mul(T(0.0, 0.38, 0.36), RY(0.9), RZ(math.pi / 2), T(0, -0.27, 0)))
    for k in range(9):
        f.box((rnd.uniform(-0.4, 0.4), 0.14, rnd.uniform(0.18, 0.5)), (0.06, 0.02, 0.05), rnd.choice(['#ff6a20', '#ff8a30', '#d84a10']),
              'Glow')
    # Sobre la repisa: dos velas, el reloj parado, tarros y un cuadro encima.
    for x in (-0.95, 0.95):
        f.add(cylinder(0.035, 0.035, 0.22, 8), '#efe6d0', m=T(x, 1.415, 0.4))
        f.add(sphere(0.02, 0.035, 0.02, 6, 4), '#ffd890', 'Glow', T(x, 1.67, 0.4))
    f.box((0, 1.56, 0.32), (0.16, 0.14, 0.07), WOOD_DARK)
    f.add(cylinder(0.11, 0.11, 0.02, 14), '#efe6d0', 'Stone', mul(T(0, 1.58, 0.4), RX(math.pi / 2)))
    for x, c in ((-0.5, '#7a8a6a'), (-0.35, '#a0522d'), (0.45, '#c8a040')):
        f.add(cylinder(0.05, 0.045, 0.16, 8), c, 'Stone', T(x, 1.415, 0.36))
    f.box((0, 2.25, 0.555), (0.5, 0.36, 0.02), WOOD_LIGHT)
    f.box((0, 2.25, 0.57), (0.44, 0.3, 0.01), rnd.choice(['#5a6a4a', '#4a5a6a', '#6a4a3a']))
    # La leña, el cesto y el atizador.
    for k in range(7):
        f.add(cylinder(0.06, 0.06, 0.5, 7), rnd.choice(['#6a4a30', '#5a3a24', '#7a5a3a']),
              m=mul(T(1.45 + (k % 3) * 0.13 - 0.13, 0.07 + (k // 3) * 0.11, 0.42), RX(math.pi / 2), T(0, -0.25, 0)))
    f.collider((1.2, 0, 0.1), (1.7, 0.4, 0.75))
    f.rod((-1.4, 0.0, 0.5), (-1.42, 0.8, 0.46), 0.015, BLACK, 'Metal', seg=5)
    f.rod((-1.48, 0.0, 0.5), (-1.5, 0.75, 0.44), 0.015, BLACK, 'Metal', seg=5)
    f.collider((-1.0, 0, 0), (1.0, ceiling, 0.62))  # el fuego no se pisa


FUEGO_CHIMENEA = (0.0, 0.28, 0.36)


def estanteria(f, width, height, rnd, depth=0.34):
    """Estantería de libros contra la pared: lomos de colores, alguno tumbado o caído, y algún
    cacharro entre medias."""
    hw = width / 2
    for s in (-1, 1):
        f.box((s * (hw - 0.03), height / 2, depth / 2), (0.03, height / 2, depth / 2), WOOD_DARK)
    f.box((0, height / 2, 0.015), (hw, height / 2, 0.015), '#3a2414')
    f.box((0, height - 0.03, depth / 2), (hw, 0.03, depth / 2 + 0.02), WOOD_DARK)
    shelves = max(3, int(height / 0.42))
    cols = ['#6b2a1a', '#2a4a6b', '#3a5a2a', '#8a6a2a', '#5a2a4a', '#2a2a2a', '#7a3a20', '#a08050', '#40506a']
    for k in range(shelves):
        y = 0.08 + k * (height - 0.1) / shelves
        f.box((0, y, depth / 2), (hw - 0.06, 0.018, depth / 2), WOOD)
        x = -hw + 0.08
        while x < hw - 0.12:
            r = rnd.random()
            if r < 0.08:  # un hueco con un cacharro
                f.add(cylinder(0.05, 0.04, 0.14, 8), rnd.choice(['#a0522d', '#c8a040', '#7a8a9a']), 'Stone', T(x + 0.07, y + 0.018, depth * 0.5))
                x += 0.18
                continue
            if r < 0.16:  # libros tumbados, en pila
                for j in range(rnd.randint(2, 4)):
                    f.box((x + 0.12, y + 0.04 + j * 0.045, depth * 0.5), (0.11, 0.022, 0.13), rnd.choice(cols))
                x += 0.26
                continue
            tw = rnd.uniform(0.022, 0.045)
            th = rnd.uniform(0.2, 0.3)
            col = rnd.choice(cols)
            f.box((x + tw, y + 0.018 + th / 2, depth * 0.52), (tw, th / 2, rnd.uniform(0.1, 0.14)), col)
            if rnd.random() < 0.4:  # el título en el lomo
                f.box((x + tw, y + 0.018 + th * 0.7, depth * 0.52 + 0.142), (tw * 0.7, 0.012, 0.003), '#c8a040', 'Metal')
            x += tw * 2 + 0.004
    f.collider((-hw, 0, 0), (hw, height, depth))


def sillon(f, x, z, yaw, color='#7a2a20'):
    """Butaca de orejas, mirando hacia `yaw` (0: hacia +z del marco)."""
    g = Frame(f, yaw, (x, 0, z))
    g.box((0, 0.22, 0), (0.42, 0.22, 0.4), shade(color, 0.85))
    g.box((0, 0.48, 0.03), (0.34, 0.06, 0.33), color)  # el cojín
    g.box((0, 0.85, -0.33), (0.42, 0.45, 0.09), color)  # el respaldo
    for s in (-1, 1):
        g.box((s * 0.37, 0.6, -0.02), (0.07, 0.14, 0.36), shade(color, 0.92))  # los brazos
        g.box((s * 0.36, 1.12, -0.25), (0.08, 0.2, 0.08), color)               # las orejas
        for zz in (-0.32, 0.32):
            g.box((s * 0.34, 0.04, zz), (0.03, 0.04, 0.03), WOOD_DARK)
    g.collider((-0.44, 0, -0.42), (0.44, 0.7, 0.42))


def alfombra(p, x, z, hx, hz, rnd, base='#7a2a24', border='#c8a040'):
    p.box((x, 0.034, z), (hx, 0.004, hz), border)
    p.box((x, 0.037, z), (hx - 0.12, 0.004, hz - 0.12), base)
    p.box((x, 0.04, z), (hx * 0.5, 0.004, hz * 0.5), rnd.choice(['#2a4a6b', '#c8a040', '#3a5a2a']))


def escalera(f, run, width, rise_total, steps):
    """Escalera de madera pegada a la pared (z de 0 a `width`) que sube hacia +x a lo largo de
    `run` metros, con la barandilla del lado de la sala. Cada peldaño es una caja de choque
    maciza (por debajo no se pasa)."""
    rise, tread = rise_total / steps, run / steps
    for i in range(steps):
        x0, x1, top = i * tread, (i + 1) * tread, (i + 1) * rise
        f.box(((x0 + x1) / 2, top - 0.03, width / 2), ((x1 - x0) / 2 + 0.02, 0.03, width / 2), WOOD_LIGHT)  # la huella
        f.box(((x0 + x1) / 2, top / 2, width / 2), ((x1 - x0) / 2, top / 2 - 0.03, width / 2 - 0.02), '#5a3622')
        f.collider((x0, 0, 0), (x1, top, width))
    f.beam((0, 0.0, width + 0.03), (run, rise_total, width + 0.03), 0.06, WOOD_DARK, d=0.22)  # la zanca
    for i in range(0, steps + 1, 3):  # balaustres y pasamanos
        x = min(run - 0.05, i * tread + 0.05)
        y = (i + 1) * rise if i < steps else rise_total
        f.rod((x, y, width - 0.05), (x, y + 0.92, width - 0.05), 0.025, WOOD_DARK, seg=6)
    f.beam((0.05, rise + 0.92, width - 0.05), (run - 0.05, rise_total + 0.92, width - 0.05), 0.06, WOOD, d=0.07)
    f.rod((0.05, 0, width - 0.05), (0.05, rise + 1.0, width - 0.05), 0.05, WOOD_DARK, seg=8)  # el poste de arranque
    f.add(sphere(0.07, 0.07, 0.07, 8, 5), WOOD, m=T(0.05, rise + 1.04, width - 0.05))


def galeria(f, x0, x1, depth, y, gap=None, posts=()):
    """Piso de arriba a lo largo de la pared (en z = 0), de `depth` metros, con el canto
    moldurado, las vigas de debajo, pies derechos y la barandilla (`gap`: el tramo sin ella,
    donde llega la escalera)."""
    f.box(((x0 + x1) / 2, y - 0.11, depth / 2), ((x1 - x0) / 2, 0.11, depth / 2), '#5a3a22')
    f.collider((x0, y - 0.22, 0), (x1, y, depth))
    f.box(((x0 + x1) / 2, y + 0.005, depth / 2), ((x1 - x0) / 2, 0.006, depth / 2), '#7a5232')
    for k in range(int((x1 - x0) / 0.45)):
        f.box((x0 + (k + 1) * 0.45, y + 0.012, depth / 2), (0.008, 0.002, depth / 2), '#5a3a22')
    f.box(((x0 + x1) / 2, y - 0.14, depth + 0.03), ((x1 - x0) / 2, 0.14, 0.04), WOOD_DARK)  # el canto
    for k in range(int((x1 - x0) / 1.2) + 1):
        x = x0 + 0.1 + k * (x1 - x0 - 0.2) / int((x1 - x0) / 1.2)
        f.box((x, y - 0.32, depth / 2), (0.08, 0.1, depth / 2), WOOD_DARK)  # vigas
    for x in posts:
        f.box((x, (y - 0.22) / 2, depth - 0.12), (0.1, (y - 0.22) / 2, 0.1), WOOD_DARK)
        f.box((x, y - 0.4, depth - 0.12), (0.16, 0.06, 0.16), WOOD)
        f.collider((x - 0.1, 0, depth - 0.22), (x + 0.1, y - 0.22, depth - 0.02))
    spans = [(x0, x1)] if gap is None else [(x0, gap[0]), (gap[1], x1)]
    for a, b in spans:
        if b - a < 0.1:
            continue
        f.box(((a + b) / 2, y + 0.95, depth - 0.04), ((b - a) / 2, 0.035, 0.05), WOOD)
        f.box(((a + b) / 2, y + 0.06, depth - 0.04), ((b - a) / 2, 0.04, 0.04), WOOD_DARK)
        n = int((b - a) / 0.16)
        for k in range(n + 1):
            f.box((a + k * (b - a) / max(1, n), y + 0.5, depth - 0.04), (0.018, 0.42, 0.018), WOOD_DARK)
        f.collider((a, y, depth - 0.1), (b, y + 1.0, depth + 0.02))


def puerta_cuarto(f, x, y, number, rnd, open_=False):
    """Puerta de una habitación en la pared (z = 0): marco, hoja con cuarterones, el pomo y el
    número; abierta, la hoja girada hacia dentro del cuarto."""
    w, h = 0.95, 2.15
    for s in (-1, 1):
        f.box((x + s * (w / 2 + 0.05), y + h / 2, 0.02), (0.05, h / 2, 0.04), WOOD_DARK)
    f.box((x, y + h + 0.05, 0.02), (w / 2 + 0.1, 0.05, 0.04), WOOD_DARK)
    if open_:
        f.add(box(w / 2, h / 2, 0.025), '#5a3a22', m=mul(T(x - w / 2, y + h / 2, -0.02), RY(1.35), T(w / 2, 0, 0)))
        return
    f.box((x, y + h / 2, 0.012), (w / 2, h / 2, 0.012), '#5a3a22')
    for yy in (0.55, 1.5):
        f.box((x, y + yy, 0.03), (w / 2 - 0.12, 0.32, 0.008), '#4a2c18')
    f.add(sphere(0.035, 0.035, 0.035, 8, 5), '#c8a040', 'Metal', T(x + w / 2 - 0.12, y + 1.0, 0.05))
    f.box((x, y + 1.85, 0.03), (0.09, 0.06, 0.008), '#c8a040', 'Metal')
    for k in range(number):  # el número, en rayas
        f.box((x - 0.04 * (number - 1) / 2 + k * 0.04, y + 1.85, 0.04), (0.008, 0.035, 0.004), BLACK, 'Metal')


def cuarto(f, x0, x1, depth, y, h, rnd, wall='#7a5a44'):
    """Habitación detrás de la pared (z = 0 es el hueco de su puerta, el cuarto hacia -z): suelo,
    paredes, techo, la cama hecha, la mesilla con su vela, un baúl, la silla con la ropa y la
    ventana con la tarde detenida."""
    t = 0.2
    f.box(((x0 + x1) / 2, y - 0.11, -depth / 2), ((x1 - x0) / 2 + t, 0.11, depth / 2 + t), '#5a3a22')
    f.collider((x0 - t, y - 0.22, -depth - t), (x1 + t, y, 0))
    f.box(((x0 + x1) / 2, y + 0.006, -depth / 2), ((x1 - x0) / 2, 0.006, depth / 2), '#7a5232')
    for lo, hi in (((x0 - t, y, -depth - t), (x1 + t, y + h, -depth)), ((x0 - t, y, -depth), (x0, y + h, 0)),
                   ((x1, y, -depth), (x1 + t, y + h, 0))):
        f.box(tuple((lo[k] + hi[k]) / 2 for k in range(3)), tuple((hi[k] - lo[k]) / 2 for k in range(3)), wall, 'Stone')
        f.collider(lo, hi)
    f.box(((x0 + x1) / 2, y + h + 0.1, -depth / 2), ((x1 - x0) / 2 + t, 0.1, depth / 2 + t), '#4a3020', 'Stone')
    f.collider((x0 - t, y + h, -depth - t), (x1 + t, y + h + 0.2, 0))
    f.box(((x0 + x1) / 2, y + 0.45, -depth + 0.012), ((x1 - x0) / 2, 0.45, 0.012), '#4a2c18')  # arrimadero
    # La cama contra el fondo.
    bx = x0 + 1.0
    f.box((bx, 0.2 + y, -depth + 1.05), (0.55, 0.2, 1.0), WOOD_DARK)
    f.box((bx, 0.45 + y, -depth + 1.05), (0.5, 0.08, 0.95), '#efe6d0')
    f.box((bx, 0.55 + y, -depth + 1.3), (0.52, 0.04, 0.75), rnd.choice(['#7a2a24', '#2a4a6b', '#5a6a3a']))
    f.box((bx, 0.58 + y, -depth + 0.3), (0.38, 0.07, 0.16), '#f4ecd8')
    f.box((bx, 0.65 + y, -depth + 0.06), (0.58, 0.65, 0.05), WOOD_DARK)  # cabecero
    f.collider((bx - 0.56, y, -depth), (bx + 0.56, y + 0.6, -depth + 2.06))
    # La mesilla con la vela, el baúl a los pies y la silla.
    mx = bx + 0.85
    f.box((mx, y + 0.3, -depth + 0.3), (0.22, 0.3, 0.2), WOOD)
    f.add(cylinder(0.03, 0.03, 0.12, 8), '#efe6d0', m=T(mx, y + 0.6, -depth + 0.3))
    f.add(sphere(0.02, 0.035, 0.02, 6, 4), '#ffd890', 'Glow', T(mx, y + 0.76, -depth + 0.3))
    f.light((mx, y + 1.1, -depth + 0.5), '#FFB070')
    f.box((bx, y + 0.24, -depth + 2.35), (0.45, 0.24, 0.24), '#6a4428')
    f.box((bx, y + 0.49, -depth + 2.35), (0.47, 0.02, 0.26), '#3a3d46', 'Metal')
    f.collider((bx - 0.47, y, -depth + 2.1), (bx + 0.47, y + 0.5, -depth + 2.6))
    cx = x1 - 0.45
    f.box((cx, y + 0.45, -depth + 1.0), (0.2, 0.03, 0.2), '#8a5a34')
    f.box((cx + 0.17, y + 0.75, -depth + 1.0), (0.02, 0.3, 0.2), '#8a5a34')
    f.box((cx, y + 0.5, -depth + 1.0), (0.18, 0.04, 0.22), rnd.choice(['#3d78c4', '#c8202f']))
    # La ventana del fondo, con la tarde.
    f.box((x1 - 0.7, y + 1.5, -depth + 0.015), (0.35, 0.45, 0.01), '#f0a060', 'Glow')
    f.box((x1 - 0.7, y + 1.5, -depth + 0.03), (0.02, 0.45, 0.01), WOOD_DARK)
    f.box((x1 - 0.7, y + 1.5, -depth + 0.03), (0.35, 0.02, 0.01), WOOD_DARK)


def aplique(f, x, y):
    """Aplique de pared con su tulipa encendida."""
    f.box((x, y, 0.03), (0.05, 0.09, 0.03), BLACK, 'Metal')
    f.rod((x, y, 0.05), (x, y + 0.08, 0.2), 0.012, BLACK, 'Metal', seg=4)
    f.add(sphere(0.07, 0.09, 0.07, 8, 5), '#ffcf88', 'Glow', T(x, y + 0.14, 0.22))


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


def lustre(p, x, z, y, r=0.7):
    """Rueda de madera colgada con sus velas, en medio de la sala de dos alturas."""
    p.rod((x, y + 2.5, z), (x, y + 0.1, z), 0.012, BLACK, 'Metal', seg=4)
    for k in range(3):
        a = k * 2 * math.pi / 3
        p.rod((x + math.cos(a) * r, y + 0.03, z + math.sin(a) * r), (x, y + 0.75, z), 0.008, BLACK, 'Metal', seg=3)
    p.add(cylinder(r + 0.05, r + 0.05, 0.08, 20, caps=True), WOOD_DARK, m=T(x, y - 0.04, z))
    p.add(cylinder(r - 0.07, r - 0.07, 0.085, 20, caps=True), '#3a2414', m=T(x, y - 0.042, z))
    for k in range(8):
        a = k * 2 * math.pi / 8
        cx, cz = x + math.cos(a) * r, z + math.sin(a) * r
        p.add(cylinder(0.03, 0.03, 0.16, 6), '#efe6d0', m=T(cx, y + 0.04, cz))
        p.add(sphere(0.022, 0.04, 0.022, 6, 4), '#ffd890', 'Glow', T(cx, y + 0.24, cz))
    p.light((x, y - 0.3, z), '#FFB878')


def botequim(name):
    """El Botequim do Ademar como posada: la barra bajo la galería, la chimenea en la pared de
    la derecha con sus butacas y la estantería, la escalera por la izquierda a la galería de
    arriba, con tres cuartos (el tercero, abierto)."""
    p = Piece(name)
    rnd = random.Random(11)
    w, d, h, G = 11.0, 10.0, 6.6, 3.2
    hw, hd = w / 2, d / 2
    room = 3.6  # la puerta del cuarto abierto, en la pared del fondo
    sala(p, w, d, h, '#7a5440', holes={'back': [(room - 0.48, room + 0.48, G, G + 2.15)]}, ceiling='#3a2818', floor='#5e3e26',
         tile=('#23497a', '#cbc3ad'), trim='#9a7a3a', beams='#2e1e12')
    for x in (-3.2, 3.2):
        ventana(p, x, hd - 0.01, True, light='#d88850')
        ventana(p, x, hd - 0.01, True, w=0.9, h=1.2, y=G + 0.8, light='#c87848')
    # La barra al fondo, bajo la galería; detrás, el botellero, el espejo y la pizarra.
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
    p.box(((bx0 + bx1) / 2, 2.0, -hd + 0.02), (0.6, 0.4, 0.015), '#8a9aa4', 'Glass')  # el espejo
    p.box(((bx0 + bx1) / 2, 2.0, -hd + 0.01), (0.66, 0.46, 0.012), WOOD_LIGHT)
    botellero(p, bx0, (bx0 + bx1) / 2 - 0.75, zb, rnd)
    botellero(p, (bx0 + bx1) / 2 + 0.75, bx1, zb, rnd)
    p.box((-1.0, 2.6, -hd + 0.03), (0.55, 0.22, 0.02), '#2c332e')  # la pizarra de los precios
    for k in range(4):
        p.box((-1.0, 2.72 - k * 0.08, -hd + 0.055), (0.38 - (k % 2) * 0.12, 0.008, 0.004), '#e8e4d8')
    # Toneles en el rincón del fondo, tras el paso.
    for x, z, y in ((4.85, -4.4, 0), (4.85, -3.7, 0), (4.85, -4.05, 0.86), (4.1, -4.5, 0)):
        tonel(p, x, z, y)
    p.collider((3.7, 0, -hd), (hw, 1.75, -3.3))
    # La escalera, por la izquierda, sube hacia el fondo hasta la galería.
    escalera(Frame(p, math.pi / 2, (-hw, 0, 2.4)), 4.8, 1.2, G, 16)
    galeria(Frame(p, 0, (0, 0, -hd)), -hw, hw, 2.6, G, gap=(-hw, -hw + 1.25), posts=(-3.9, 2.6))
    # Arriba: dos cuartos cerrados, el tercero abierto, una estantería pequeña y un banco.
    back = Frame(p, 0, (0, 0, -hd))
    puerta_cuarto(back, -3.0, G, 1, rnd)
    puerta_cuarto(back, -0.4, G, 2, rnd)
    puerta_cuarto(back, room, G, 3, rnd, open_=True)
    cuarto(back, room - 2.0, room + 1.4, 3.4, G, 2.7, rnd)
    estanteria(Frame(p, 0, (1.35, G, -hd)), 1.1, 1.7, rnd)
    for x in (-4.3, 1.35 - 1.3, room - 1.0):
        aplique(Frame(p, 0, (0, 0, -hd)), x, G + 1.9)
    p.light((-1.7, G + 2.0, -hd + 1.2), '#FFB070')
    p.box((-1.7, G + 0.42, -hd + 0.3), (0.6, 0.03, 0.18), WOOD)  # el banco del pasillo
    for x in (-2.2, -1.2):
        p.box((x, G + 0.2, -hd + 0.3), (0.03, 0.2, 0.15), WOOD_DARK)
    p.collider((-2.3, G, -hd), (-1.1, G + 0.45, -hd + 0.5))
    # La chimenea en la pared de la derecha, con las butacas, la alfombra y la estantería.
    fire = Frame(p, -math.pi / 2, (hw, 0, 0.4))
    chimenea(fire, h, rnd)
    sillon(fire, -0.95, 2.3, math.pi - 0.45, '#6a2a22')
    sillon(fire, 0.95, 2.3, math.pi + 0.45, '#3a4a2a')
    fire.box((0, 0.3, 1.75), (0.28, 0.02, 0.28), WOOD)  # la mesita entre las butacas
    fire.box((0, 0.15, 1.75), (0.04, 0.15, 0.04), WOOD_DARK)
    fire.add(cylinder(0.04, 0.035, 0.09, 8), '#efe6d0', 'Stone', T(0.1, 0.32, 1.7))
    fire.box((-0.08, 0.335, 1.82), (0.09, 0.015, 0.12), '#6b2a1a')  # un libro abierto
    fire.collider((-0.3, 0, 1.45), (0.3, 0.35, 2.05))
    alfombra(p, hw - 1.75, 0.4, 1.0, 1.6, rnd)
    estanteria(Frame(p, -math.pi / 2, (hw, 0, 3.6)), 1.6, 2.4, rnd)
    # Las mesas.
    for x, z in ((-2.9, 0.4), (-1.6, 2.7), (1.0, 0.5), (-3.0, 3.6)):
        mesa(p, x, z, rnd, 3 if x > 0 else 4)
    # Carteles: el del equipo de fútbol, el santo y un calendario.
    cartel(p, -hw + 0.02, 1.9, 3.7, 0.8, 0.6, '#8a2020', False, rnd)
    cartel(p, hw - 0.02, 2.2, -1.9, 0.5, 0.7, '#c8bca4', False, rnd)
    cartel(p, 2.9, 2.1, -hd + 0.02, 0.5, 0.6, '#2a5d3f', True, rnd)
    # La luz: la rueda de velas en medio, un farol sobre la barra y los de la sala.
    lustre(p, 0.4, 1.2, 4.3)
    farol_techo(p, -1.0, bz, G - 0.22, 0.45)
    p.light((-1.0, 2.2, bz + 0.6), '#FFC080')
    farol_techo(p, -2.4, 2.6, h, 2.1)
    return p


def taberna_lapa_dentro(name):
    """La Taberna da Lapa: la barra a la izquierda bajo la galería, el escenario de la samba al
    fondo a la derecha (a doble altura), la chimenea en la pared de la derecha con sus butacas y
    la estantería, y la escalera por el fondo a la galería, con tres cuartos (uno abierto)."""
    p = Piece(name)
    rnd = random.Random(23)
    w, d, h, G = 13.0, 11.0, 7.0, 3.4
    hw, hd = w / 2, d / 2
    room = 1.8  # la puerta del cuarto abierto (en el marco de la pared izquierda: z del mundo = -1.8)
    sala(p, w, d, h, '#6e2e28', holes={'left': [(-room - 0.48, -room + 0.48, G, G + 2.15)]}, ceiling='#2e1c14', floor='#5a3a24',
         tile=('#7a3a20', '#c8b89a'), trim='#9a7a3a', beams='#2a1a10')
    for x in (-3.3, 3.3):
        ventana(p, x, hd - 0.01, True, light='#d87850')
        ventana(p, x, hd - 0.01, True, w=0.9, h=1.2, y=G + 0.9, light='#c86848')
    # La barra, a lo largo de la pared izquierda (de cara a +x); quien atiende, detrás.
    bz0, bz1, bx = -3.2, 1.8, -hw + 1.5
    barra(p, bz0, bz1, bx, along_x=False)
    for z in (-2.6, -1.7, 0.7, 1.4):
        p.add(cylinder(0.18, 0.18, 0.05, 10), '#8a5a34', m=T(bx + 0.85, 0.72, z))
        p.add(cylinder(0.035, 0.035, 0.72, 6), BLACK, 'Metal', T(bx + 0.85, 0, z))
    for z, c in ((-0.2, '#f0b040'), (0.0, '#f0b040'), (1.2, '#2a7d4f')):
        p.add(cylinder(0.04, 0.035, 0.13, 8), c, 'Glass', T(bx + 0.1, 1.1, z))
    botellero(p, bz0, bz1, -hw + 0.16, rnd, along_x=False)
    cartel(p, -hw + 0.02, 2.75, -0.7, 1.6, 0.4, '#2c332e', False)  # la pizarra
    # El escenario de la samba, en el rincón del fondo a la derecha.
    sx0, sx1, sz0, sz1 = 1.2, hw, -hd, -hd + 2.8
    p.box(((sx0 + sx1) / 2, 0.15, (sz0 + sz1) / 2), ((sx1 - sx0) / 2, 0.15, (sz1 - sz0) / 2), WOOD_DARK)
    p.box(((sx0 + sx1) / 2, 0.305, (sz0 + sz1) / 2), ((sx1 - sx0) / 2 - 0.02, 0.005, (sz1 - sz0) / 2 - 0.02), '#6a2216')
    p.collider((sx0, 0, sz0), (sx1, 0.3, sz1))
    cx = (sx0 + sx1) / 2
    p.add(cylinder(0.3, 0.3, 0.6, 14), '#e8e4d8', m=T(cx - 1.2, 0.3, sz0 + 1.2))  # el surdo
    p.add(cylinder(0.31, 0.31, 0.05, 14), '#c8a040', 'Metal', T(cx - 1.2, 0.85, sz0 + 1.2))
    p.add(cylinder(0.31, 0.31, 0.05, 14), '#c8a040', 'Metal', T(cx - 1.2, 0.3, sz0 + 1.2))
    p.add(cylinder(0.16, 0.16, 0.05, 12), '#c8a040', 'Metal', mul(T(cx + 0.2, 1.1, sz0 + 1.0), RX(1.3)))  # el pandeiro
    p.add(cylinder(0.2, 0.2, 0.45, 10), '#8a5a34', m=T(cx + 1.3, 0.3, sz0 + 1.4))  # el cavaquinho, en un taburete
    p.add(sphere(0.18, 0.22, 0.06, 10, 6), '#c07a38', m=mul(T(cx + 1.3, 1.0, sz0 + 1.25), RX(-0.3)))
    p.add(box(0.03, 0.25, 0.015), WOOD_DARK, m=mul(T(cx + 1.3, 1.35, sz0 + 1.16), RX(-0.3)))
    p.rod((cx, 0.3, sz0 + 1.9), (cx, 1.6, sz0 + 1.9), 0.015, BLACK, 'Metal', seg=5)  # el micro
    p.add(sphere(0.05, 0.07, 0.05, 8, 5), '#3a3d46', 'Metal', T(cx, 1.66, sz0 + 1.9))
    for x in (cx - 0.6, cx + 0.6):  # dos sillas de músico
        p.box((x, 0.75, sz0 + 1.1), (0.2, 0.03, 0.2), '#8a5a34')
        for a in (-1, 1):
            for b in (-1, 1):
                p.box((x + a * 0.16, 0.52, sz0 + 1.1 + b * 0.16), (0.02, 0.22, 0.02), '#5a3622')
    cols = ['#f0b040', '#2a9d55', '#3d78c4', '#c8202f', WHITE]
    for k in range(9):  # banderines sobre el escenario
        u = k / 8
        x, y = sx0 + 0.2 + (sx1 - sx0 - 0.4) * u, 3.4 - 0.25 * math.sin(u * math.pi)
        p.add(box(0.12, 0.14, 0.005), cols[k % 5], m=mul(T(x, y - 0.12, sz1 - 0.05), RZ(math.pi / 4)))
    p.light((cx, 2.6, sz0 + 1.6), '#FF9A70')
    # La escalera, por el fondo, sube hacia la izquierda hasta la galería.
    run = 4.76
    escalera(Frame(p, 0, (-hw + 2.6 + run, 0, -hd), mirror=True), run, 1.2, G, 17)
    left = Frame(p, math.pi / 2, (-hw, 0, 0))  # x del marco: hacia el fondo; z: hacia la sala
    galeria(left, -hd, hd, 2.6, G, gap=(hd - 1.25, hd), posts=(-3.0, 1.2))  # sin tapar la barra
    puerta_cuarto(left, -3.4, G, 1, rnd)
    puerta_cuarto(left, -0.8, G, 2, rnd)
    puerta_cuarto(left, room, G, 3, rnd, open_=True)
    cuarto(left, room - 2.0, room + 1.4, 3.4, G, 2.7, rnd, wall='#6a4434')
    estanteria(Frame(left, 0, (-2.1, G, 0)), 1.0, 1.6, rnd)
    for x in (-4.6, -2.1 + 0.0, 0.5):
        aplique(left, x, G + 2.2 if x == -2.1 else G + 1.9)
    p.light((-hw + 1.2, G + 2.0, 0.0), '#FFB070')
    # La guirnalda de bombillas cruza la sala de lado a lado, por encima de las mesas.
    for z in (-1.0, 2.0):
        a, b = (-hw + 2.7, G + 1.4, z), (hw - 0.1, G + 1.4, z)
        p.rod(a, b, 0.006, BLACK, 'Metal', seg=3)
        for k in range(1, 12):
            u = k / 12
            p.add(sphere(0.05, 0.065, 0.05, 6, 4), '#ffe2a0', 'Glow', T(a[0] + (b[0] - a[0]) * u, G + 1.4 - 0.4 * math.sin(u * math.pi), z))
    # La chimenea en la pared de la derecha, con las butacas, la alfombra y la estantería.
    fire = Frame(p, -math.pi / 2, (hw, 0, 1.0))
    chimenea(fire, h, rnd)
    sillon(fire, -0.95, 2.3, math.pi - 0.45, '#2a3a5a')
    sillon(fire, 0.95, 2.3, math.pi + 0.45, '#6a2a22')
    fire.box((0, 0.3, 1.75), (0.28, 0.02, 0.28), WOOD)
    fire.box((0, 0.15, 1.75), (0.04, 0.15, 0.04), WOOD_DARK)
    fire.add(cylinder(0.05, 0.04, 0.2, 8), '#2a7d4f', 'Glass', T(0.1, 0.32, 1.7))
    fire.collider((-0.3, 0, 1.45), (0.3, 0.35, 2.05))
    alfombra(p, hw - 1.75, 1.0, 1.0, 1.6, rnd, base='#5a2a3a')
    estanteria(Frame(p, -math.pi / 2, (hw, 0, 3.9)), 1.8, 2.4, rnd)
    # Mesas redondas.
    for x, z in ((-1.8, -1.6), (0.4, 0.4), (-1.6, 2.6), (2.2, 2.9), (2.3, -1.3), (-2.2, -3.4)):
        mesa(p, x, z, rnd, 3, round_top=True)
    # Carteles de samba y una planta en cada rincón de delante.
    for z, c in ((-2.2, '#c89030'), (-4.4, '#2a7d4a')):
        cartel(p, hw - 0.02, 2.2, z, 0.7, 0.9, c, False, rnd)
    for x in (-hw + 0.5, hw - 0.5):
        p.add(cylinder(0.25, 0.2, 0.45, 10), '#a0522d', 'Stone', T(x, 0, hd - 0.5))
        for k in range(6):
            a = k * 1.05
            p.add(box(0.08, 0.4, 0.015), '#2a7d4f', m=mul(T(x, 0.6, hd - 0.5), RY(a), RX(0.5), T(0, 0.3, 0)))
        p.collider((x - 0.3, 0, hd - 0.8), (x + 0.3, 0.9, hd - 0.2))
    lustre(p, 0.6, 0.6, 4.6)
    for x, z in ((-1.4, -1.6), (-1.6, 2.6)):
        farol_techo(p, x, z, h, 2.4)
    return p


def piezas():
    return [botequim('botequim'), taberna_lapa_dentro('taberna_lapa_dentro')]
