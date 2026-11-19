"""Las Cumbres Nevadas (src/game/levels_cumbres.cpp): el refugio, los cobertizos y el monte.

  - refugio: casa de troncos de dos alturas sobre zócalo de piedra, tejado a dos aguas con la
    nieve encima, chimenea de piedra, ventanas encendidas con contraventanas, el porche con su
    banco y la puerta. La fachada mira a +z.
  - cobertizo: abierto por delante, con la leña apilada, herramientas y un trineo; cobertizo_cerrado,
    de tablas, con su puerta.
  - leñera: troncos apilados bajo un tejadillo.
  - abeto_nevado_1..3: abetos con las ramas en pisos y la nieve en cada piso.
  - roca_nevada, roca_nevada_grande: rocas con la nieve encima.
  - trineo, muneco_nieve, cruz_cumbre (la cruz de hierro de la cima con su hito de piedras),
    pasarela (pasarela de madera para el barranco), poste_cuerda (los postes con la cuerda del
    collado).
"""

import math
import random

from kit import Piece, box, cylinder, sphere, rock, gable, mix, shade, T, RY, RX, RZ, mul

LOG, LOG_DARK, SNOW, STONE = '#7a5234', '#5a3a22', '#f2f5fa', '#8a8680'


def snow_cap(p, hx, hz, y, thick=0.18, seed=1):
    """Una capa de nieve blanda: losa con los bordes redondeados."""
    p.box((0, y + thick / 2, 0), (hx, thick / 2, hz), SNOW, 'Snow')
    p.add(cylinder(thick / 2, thick / 2, hx * 2, 8), SNOW, 'Snow', mul(T(-hx, y + thick / 2, hz), RZ(-math.pi / 2)))


def refugio(name):
    p = Piece(name)
    w, d, h = 4.2, 3.6, 5.6  # medio ancho, medio fondo, altura del alero
    # El zócalo de piedra.
    rnd = random.Random(3)
    p.box((0, 0.45, 0), (w + 0.15, 0.45, d + 0.15), STONE, 'Stone')
    for k in range(26):
        x = rnd.uniform(-w, w)
        side = rnd.choice((-1, 1))
        p.add(rock(0.32, 0.2, 0.08, k), shade(STONE, rnd.uniform(0.85, 1.1)), 'Stone', T(x, rnd.uniform(0.15, 0.75), side * (d + 0.17)))
    # Las paredes de troncos (un tronco cada 0,32 m, alternando el tono).
    y = 0.9
    k = 0
    while y < h:
        c = LOG if k % 2 else shade(LOG, 0.9)
        for s in (-1, 1):
            p.add(cylinder(0.17, 0.17, 2 * w + 0.5, 8), c, m=mul(T(-w - 0.25, y + 0.16, s * d), RZ(-math.pi / 2)))
            p.add(cylinder(0.17, 0.17, 2 * d + 0.5, 8), c, m=mul(T(s * w, y + 0.16, -d - 0.25), RX(math.pi / 2)))
        y += 0.32
        k += 1
    p.box((0, (h + 0.9) / 2, 0), (w - 0.1, (h - 0.9) / 2, d - 0.1), shade(LOG, 0.8))  # el relleno
    # El hastial (triángulo de tablas) y el tejado con la nieve.
    p.add(gable(w + 0.6, d + 0.6, 2.6), LOG_DARK, m=T(0, h, 0))
    rise = math.atan2(2.6, w + 0.6)
    run = math.hypot(2.6, w + 0.6)
    for s in (-1, 1):
        m = mul(T(0, h + 1.3, 0), RZ(s * rise), T(s * run / 2 * 0, 0, 0))
        p.add(box(run / 2 + 0.25, 0.09, d + 0.75), '#3a2a20', m=mul(T(s * (w + 0.6) / 2, h + 1.3, 0), RZ(-s * rise)))
        p.add(box(run / 2 + 0.2, 0.12, d + 0.7), SNOW, 'Snow', mul(T(s * (w + 0.6) / 2, h + 1.3, 0), RZ(-s * rise), T(0, 0.18, 0)))
    # La chimenea de piedra, con su humo (lo pone el juego).
    p.box((w - 1.2, h + 1.6, -d + 1.2), (0.45, 2.0, 0.45), STONE, 'Stone')
    p.box((w - 1.2, h + 3.65, -d + 1.2), (0.55, 0.08, 0.55), shade(STONE, 0.8), 'Stone')
    p.box((w - 1.2, h + 3.8, -d + 1.2), (0.5, 0.08, 0.5), SNOW, 'Snow')
    # Ventanas encendidas con contraventanas verdes y la puerta.
    for x, y0 in ((-2.4, 1.7), (2.4, 1.7), (-2.0, 3.9), (2.0, 3.9)):
        p.box((x, y0 + 0.55, d + 0.2), (0.45, 0.55, 0.03), '#f6c67a', 'Glow')
        p.box((x, y0 + 0.55, d + 0.22), (0.03, 0.55, 0.02), LOG_DARK)
        for s in (-1, 1):
            p.box((x + s * 0.72, y0 + 0.55, d + 0.24), (0.24, 0.6, 0.03), '#2f5a40')
        p.box((x, y0 - 0.05, d + 0.3), (0.55, 0.06, 0.12), LOG_DARK)
        p.box((x, y0 + 0.02, d + 0.32), (0.55, 0.05, 0.1), SNOW, 'Snow')
    p.box((0, 1.95, d + 0.21), (0.6, 1.05, 0.05), '#4a2a1a')
    p.box((0.4, 1.9, d + 0.28), (0.05, 0.05, 0.04), '#c8a040', 'Metal')
    # El porche: suelo de tablas, dos postes, el tejadillo con nieve y el banco.
    p.box((0, 0.95, d + 1.1), (2.0, 0.06, 1.0), LOG)
    for s in (-1, 1):
        p.add(cylinder(0.1, 0.1, 2.0, 8), LOG_DARK, m=T(s * 1.9, 1.0, d + 1.95))
    p.box((0, 3.05, d + 1.15), (2.2, 0.08, 1.05), LOG_DARK, m=RX(-0.12))
    p.box((0, 3.18, d + 1.15), (2.2, 0.1, 1.05), SNOW, 'Snow', m=RX(-0.12))
    p.box((-1.2, 1.4, d + 0.55), (0.7, 0.05, 0.22), LOG)
    for x in (-1.75, -0.65):
        p.box((x, 1.18, d + 0.55), (0.05, 0.2, 0.18), LOG_DARK)
    for k2 in range(3):  # los escalones
        p.box((0, 0.15 + k2 * 0.28, d + 2.4 + (2 - k2) * 0.3), (0.8, 0.15, 0.18), STONE, 'Stone')
    p.collider((-w - 0.3, 0, -d - 0.3), (w + 0.3, h + 2.6, d + 0.3))
    p.collider((-2.0, 0, d + 0.3), (2.0, 1.0, d + 2.1))
    return p


def cobertizo(name, closed=False):
    p = Piece(name)
    w, d, h = 2.6, 1.8, 2.6
    rnd = random.Random(7 if closed else 5)
    for x in (-w, w):
        for z in (-d, d):
            p.add(cylinder(0.11, 0.11, h + (0.4 if z < 0 else 0), 6), LOG_DARK, m=T(x, 0, z))
    # Las tablas: detrás y a los lados (y delante, si está cerrado, con su puerta).
    for k in range(int(2 * w / 0.3)):
        x = -w + 0.15 + k * 0.3
        p.box((x, (h + 0.4) / 2, -d), (0.14, (h + 0.4) / 2, 0.04), shade(LOG, rnd.uniform(0.8, 1.05)))
        if closed and abs(x) > 0.55:
            p.box((x, h / 2, d), (0.14, h / 2, 0.04), shade(LOG, rnd.uniform(0.8, 1.05)))
    for s in (-1, 1):
        for k in range(int(2 * d / 0.3)):
            z = -d + 0.15 + k * 0.3
            p.box((s * w, h / 2 + 0.1, z), (0.04, h / 2 + 0.1, 0.14), shade(LOG, rnd.uniform(0.8, 1.05)))
    if closed:
        p.box((0, 1.0, d + 0.02), (0.55, 1.0, 0.04), '#4a2a1a')
        p.box((0, 1.0, d + 0.06), (0.5, 0.04, 0.02), '#3a3d46', 'Metal')
    # El tejado a un agua con su nieve.
    p.box((0, h + 0.25, 0), (w + 0.4, 0.07, d + 0.5), '#3a2a20', m=RX(0.2))
    p.box((0, h + 0.4, 0), (w + 0.35, 0.13, d + 0.45), SNOW, 'Snow', m=RX(0.2))
    if not closed:  # dentro: la leña, el hacha en el tajo y el trineo
        for row in range(4):
            for k in range(8):
                p.add(cylinder(0.12, 0.12, 1.4, 7), mix('#8a5a34', '#c09060', (k + row) % 3 / 3), m=mul(T(-2.2 + k * 0.28 + (row % 2) * 0.14, 0.12 + row * 0.23, -d + 0.4), RX(math.pi / 2), T(0, -0.0, 0)))
        p.add(cylinder(0.25, 0.25, 0.45, 10), '#8a5a34', m=T(1.6, 0, 0.6))
        p.rod((1.6, 0.45, 0.6), (1.85, 1.0, 0.75), 0.03, LOG_DARK)
        p.box((1.92, 1.05, 0.8), (0.12, 0.08, 0.02), '#9aa0a8', 'Metal')
    p.collider((-w - 0.1, 0, -d - 0.1), (w + 0.1, h + 0.5, -d + 0.9))
    for s in (-1, 1):
        p.collider((s * w - 0.1, 0, -d), (s * w + 0.1, h, d))
    if closed:
        p.collider((-w, 0, d - 0.1), (w, h, d + 0.1))
    return p


def lenera(name):
    p = Piece(name)
    for row in range(5):
        for k in range(9 - row % 2):
            p.add(cylinder(0.13, 0.13, 0.9, 7), mix('#8a5a34', '#c8a070', ((k * 7 + row) % 5) / 5), m=mul(T(-1.1 + k * 0.27 + (row % 2) * 0.13, 0.13 + row * 0.24, 0), RX(math.pi / 2), T(0, -0.45, 0)))
    for x in (-1.3, 1.3):
        p.add(cylinder(0.06, 0.06, 1.6, 6), LOG_DARK, m=T(x, 0, -0.5))
    p.box((0, 1.65, 0), (1.5, 0.05, 0.75), '#3a2a20', m=RX(0.15))
    p.box((0, 1.75, 0), (1.45, 0.08, 0.7), SNOW, 'Snow', m=RX(0.15))
    p.collider((-1.3, 0, -0.5), (1.3, 1.3, 0.5))
    return p


def abeto(name, seed, h):
    """Abeto: tronco y pisos de ramas (conos) cada vez más pequeños, con la nieve encima."""
    p = Piece(name)
    rnd = random.Random(seed)
    p.add(cylinder(0.18 * h / 8, 0.1, h * 0.3, 7), '#4a3020', m=T(0, 0, 0))
    tiers = 5
    for k in range(tiers):
        u = k / tiers
        r = (1.0 - u * 0.75) * h * 0.24
        y = h * (0.18 + u * 0.66)
        th = h * 0.26
        col = mix('#1f4a34', '#2d5e40', rnd.random())
        p.add(cylinder(r, r * 0.12, th, 9, smooth=False), col, m=mul(T(0, y, 0), RY(rnd.random())))
        p.add(cylinder(r * 0.86, r * 0.2, th * 0.45, 9, smooth=False), SNOW, 'Snow', mul(T(0, y + th * 0.3, 0), RY(rnd.random())))
    p.add(cylinder(0.12, 0.0, h * 0.14, 6), SNOW, 'Snow', T(0, h * 0.95, 0))
    p.collider((-0.4, 0, -0.4), (0.4, h * 0.6, 0.4))
    return p


def roca_nevada(name, seed, rx, ry, rz):
    p = Piece(name)
    p.add(rock(rx, ry, rz, seed), lambda q: SNOW if q[1] > ry * 0.35 else shade(STONE, 0.85 + 0.2 * (q[1] / ry)), 'Stone', T(0, ry * 0.3, 0))
    p.collider((-rx * 0.8, 0, -rz * 0.8), (rx * 0.8, ry * 1.1, rz * 0.8))
    return p


def trineo(name):
    p = Piece(name)
    for s in (-1, 1):
        p.box((s * 0.3, 0.06, 0), (0.03, 0.03, 0.75), '#3a3d46', 'Metal')
        p.rod((s * 0.3, 0.06, 0.75), (s * 0.3, 0.3, 0.95), 0.03, '#3a3d46', 'Metal', seg=5)
        for z in (-0.5, 0.0, 0.5):
            p.box((s * 0.3, 0.2, z), (0.025, 0.12, 0.025), '#8a5a34')
    for k in range(6):
        p.box((0, 0.34, -0.6 + k * 0.24), (0.36, 0.025, 0.09), '#b06a3a')
    p.collider((-0.4, 0, -0.8), (0.4, 0.4, 0.9))
    return p


def muneco(name):
    p = Piece(name)
    p.add(sphere(0.6, 0.55, 0.6, 12, 8), SNOW, 'Snow', T(0, 0.5, 0))
    p.add(sphere(0.42, 0.4, 0.42, 12, 8), SNOW, 'Snow', T(0, 1.35, 0))
    p.add(sphere(0.3, 0.3, 0.3, 12, 8), SNOW, 'Snow', T(0, 1.98, 0))
    p.add(cylinder(0.05, 0.0, 0.3, 6), '#e8702a', m=mul(T(0, 2.0, 0.28), RX(math.pi / 2)))
    for x in (-0.1, 0.1):
        p.add(sphere(0.035, seg=6, rings=4), '#1a1a1a', m=T(x, 2.1, 0.27))
    for k in range(3):
        p.add(sphere(0.04, seg=6, rings=4), '#1a1a1a', m=T(0, 1.2 + k * 0.17, 0.41))
    p.add(cylinder(0.32, 0.32, 0.06, 12), '#2a2a2e', m=T(0, 2.22, 0))  # el sombrero
    p.add(cylinder(0.2, 0.2, 0.3, 12), '#2a2a2e', m=T(0, 2.28, 0))
    p.add(box(0.36, 0.06, 0.06), '#c8202f', m=T(0, 1.68, 0))  # la bufanda
    for s in (-1, 1):
        p.rod((s * 0.35, 1.4, 0), (s * 0.85, 1.75, 0.05), 0.025, '#4a3020', seg=4)
    p.collider((-0.5, 0, -0.5), (0.5, 2.2, 0.5))
    return p


def cruz_cumbre(name):
    p = Piece(name)
    rnd = random.Random(9)
    for k in range(14):  # el hito de piedras
        a, d = rnd.random() * 6.28, rnd.uniform(0, 0.9)
        p.add(rock(0.45, 0.3, 0.4, k), shade(STONE, rnd.uniform(0.8, 1.05)), 'Stone', T(math.cos(a) * d, 0.2 + (0.9 - d) * 0.6, math.sin(a) * d))
    p.box((0, 2.9, 0), (0.07, 2.2, 0.07), '#3a3d46', 'Metal')
    p.box((0, 4.0, 0), (0.75, 0.07, 0.07), '#3a3d46', 'Metal')
    p.box((0, 3.4, 0.08), (0.18, 0.12, 0.01), '#c8a040', 'Metal')  # la placa
    p.box((0, 5.15, 0), (0.12, 0.05, 0.12), SNOW, 'Snow')
    p.collider((-0.9, 0, -0.9), (0.9, 1.2, 0.9))
    return p


def pasarela(name, length=14.0):
    """Pasarela de madera para cruzar el barranco: tablas sobre dos vigas, barandilla de cuerda."""
    p = Piece(name)
    half = length / 2
    for s in (-1, 1):
        p.box((s * 0.7, -0.1, 0), (0.08, 0.12, half), LOG_DARK)
        for z in (-half, -half / 2, 0, half / 2, half):
            p.add(cylinder(0.06, 0.06, 1.1, 6), LOG_DARK, m=T(s * 0.75, 0, z))
        p.rod((s * 0.75, 1.05, -half), (s * 0.75, 1.05, half), 0.025, '#c8b890', seg=4)
    for k in range(int(length / 0.3)):
        z = -half + 0.15 + k * 0.3
        p.box((0, 0.03, z), (0.8, 0.03, 0.13), mix(LOG, '#a07850', (k * 37 % 10) / 10))
        if k % 3 == 0:
            p.box((0, 0.075, z), (0.7, 0.015, 0.1), SNOW, 'Snow')
    p.collider((-0.85, -0.25, -half), (0.85, 0.06, half))
    for s in (-1, 1):
        p.collider((s * 0.75 - 0.05, 0, -half), (s * 0.75 + 0.05, 1.1, half))
    return p


def piezas():
    return [refugio('refugio'), cobertizo('cobertizo'), cobertizo('cobertizo_cerrado', True), lenera('lenera'),
            abeto('abeto_nevado_1', 1, 7.0), abeto('abeto_nevado_2', 2, 10.0), abeto('abeto_nevado_3', 3, 4.5),
            roca_nevada('roca_nevada', 4, 1.3, 0.9, 1.1), roca_nevada('roca_nevada_grande', 5, 3.2, 2.2, 2.6), trineo('trineo'),
            muneco('muneco_nieve'), cruz_cumbre('cruz_cumbre'), pasarela('pasarela')]
