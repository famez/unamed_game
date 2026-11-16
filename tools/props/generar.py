#!/usr/bin/env python3
"""Genera las piezas del escenario en assets/props/*.glb.

    python3 tools/props/generar.py [CARPETA] [--solo nombre1,nombre2]

Cada pieza es un .glb normal: se puede abrir en Blender, retocar y volver a exportar con el
mismo nombre (manteniendo las convenciones COL_ y LUZ_, ver src/game/props.h). Ojo: volver a
generar sobrescribe los ficheros; las piezas rehechas a mano conviene sacarlas de aquí
(--solo) o guardarlas con otro nombre.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import campo  # noqa: E402

GRUPOS = [campo]
for opt in ('ciudad', 'cafetero'):
    try:
        GRUPOS.append(__import__(opt))
    except ImportError:
        pass


def main():
    argv = sys.argv[1:]
    only = None
    if '--solo' in argv:
        k = argv.index('--solo')
        only = set(argv[k + 1].split(',')) if k + 1 < len(argv) else set()
        del argv[k:k + 2]
    args = [a for a in argv if not a.startswith('--')]
    out = args[0] if args else os.path.join(os.path.dirname(__file__), '..', '..', 'assets', 'props')
    os.makedirs(out, exist_ok=True)
    total = 0
    for g in GRUPOS:
        for piece in g.piezas():
            if only and piece.name not in only:
                continue
            piece.write(os.path.join(out, piece.name + '.glb'))
            total += 1
            print('%-24s %6d triángulos, %d colisiones, %d luces' % (piece.name, piece.triangles(), len(piece.colliders),
                                                                   len(piece.lights)))
    print('%d piezas en %s' % (total, os.path.normpath(out)))


if __name__ == '__main__':
    main()
