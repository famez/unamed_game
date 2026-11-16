"""Kit para modelar piezas del escenario por código y escribirlas en .glb.

Una pieza (`Piece`) junta primitivas (cajas, cilindros, esferas, rocas, tejados, vigas) con
su color y su material, más cajas de colisión (`COL_…`) y puntos de luz (`LUZ_RRGGBB`).
Origen en el centro de la base, Y hacia arriba, metros. Sin dependencias: solo la
biblioteca estándar de Python.
"""

import json
import math
import random
import struct
from array import array

# --- Colores ---------------------------------------------------------------------------


def hexcol(h):
    """'#RRGGBB' → (r, g, b) en sRGB 0..1."""
    h = h.lstrip('#')
    return tuple(int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4))


def mix(a, b, t):
    a, b = hexcol(a) if isinstance(a, str) else a, hexcol(b) if isinstance(b, str) else b
    return tuple(x + (y - x) * t for x, y in zip(a, b))


def shade(c, k):
    c = hexcol(c) if isinstance(c, str) else c
    return tuple(min(1.0, x * k) for x in c)


def to_linear(c):
    return tuple(x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in c)


# --- Matrices (4x4, filas) --------------------------------------------------------------

I = [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]


def mul(*ms):
    r = I
    for m in ms:
        r = [[sum(r[i][k] * m[k][j] for k in range(4)) for j in range(4)] for i in range(4)]
    return r


def T(x, y, z):
    return [[1, 0, 0, x], [0, 1, 0, y], [0, 0, 1, z], [0, 0, 0, 1]]


def S(x, y=None, z=None):
    y = x if y is None else y
    z = x if z is None else z
    return [[x, 0, 0, 0], [0, y, 0, 0], [0, 0, z, 0], [0, 0, 0, 1]]


def RX(a):
    c, s = math.cos(a), math.sin(a)
    return [[1, 0, 0, 0], [0, c, -s, 0], [0, s, c, 0], [0, 0, 0, 1]]


def RY(a):
    c, s = math.cos(a), math.sin(a)
    return [[c, 0, s, 0], [0, 1, 0, 0], [-s, 0, c, 0], [0, 0, 0, 1]]


def RZ(a):
    c, s = math.cos(a), math.sin(a)
    return [[c, -s, 0, 0], [s, c, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]


def norm(v):
    l = math.sqrt(sum(x * x for x in v)) or 1.0
    return tuple(x / l for x in v)


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def basis_y(d):
    """Rotación que lleva el eje Y a la dirección `d`."""
    y = norm(d)
    ref = (0, 1, 0) if abs(y[1]) < 0.9 else (1, 0, 0)
    x = norm(cross(ref, y))
    z = cross(x, y)
    return [[x[0], y[0], z[0], 0], [x[1], y[1], z[1], 0], [x[2], y[2], z[2], 0], [0, 0, 0, 1]]


def apply(m, p):
    return tuple(m[i][0] * p[0] + m[i][1] * p[1] + m[i][2] * p[2] + m[i][3] for i in range(3))


def normal_matrix(m):
    a = [[m[i][j] for j in range(3)] for i in range(3)]
    det = (a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) +
           a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]))
    if abs(det) < 1e-12:
        return a
    inv = [[0] * 3 for _ in range(3)]
    for i in range(3):
        for j in range(3):
            r = [k for k in range(3) if k != j]
            c = [k for k in range(3) if k != i]
            minor = a[r[0]][c[0]] * a[r[1]][c[1]] - a[r[0]][c[1]] * a[r[1]][c[0]]
            inv[i][j] = (-1) ** (i + j) * minor / det
    return [[inv[j][i] for j in range(3)] for i in range(3)]  # transpuesta de la inversa


# --- Primitivas: (vértices [(posición, normal)], triángulos) en espacio local ------------


def box(hx, hy, hz):
    """Caja centrada en el origen (caras planas)."""
    v, t = [], []
    faces = [((1, 0, 0), (0, 0, -1), (0, 1, 0)), ((-1, 0, 0), (0, 0, 1), (0, 1, 0)), ((0, 1, 0), (1, 0, 0), (0, 0, -1)),
             ((0, -1, 0), (1, 0, 0), (0, 0, 1)), ((0, 0, 1), (1, 0, 0), (0, 1, 0)), ((0, 0, -1), (-1, 0, 0), (0, 1, 0))]
    h = (hx, hy, hz)
    for n, u, w in faces:
        base = len(v)
        for su, sw in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            p = tuple((n[k] + u[k] * su + w[k] * sw) * h[k] for k in range(3))
            v.append((p, n))
        t += [(base, base + 1, base + 2), (base, base + 2, base + 3)]
    return v, t


def cylinder(r0, r1, h, seg=12, caps=True, smooth=True):
    """Cilindro (o cono) de y = 0 a y = h, radios r0 abajo y r1 arriba."""
    v, t = [], []
    slope = (r0 - r1) / h if h else 0
    for i in range(seg + 1):
        a = 2 * math.pi * i / seg
        c, s = math.cos(a), math.sin(a)
        n = norm((c, slope, s))
        v.append(((r0 * c, 0, r0 * s), n))
        v.append(((r1 * c, h, r1 * s), n))
    for i in range(seg):
        a, b = 2 * i, 2 * i + 2
        t += [(a, a + 1, b + 1), (a, b + 1, b)]
    if not smooth:  # caras planas: se duplican por cara
        fv, ft = [], []
        for a, b, c in t:
            pa, pb, pc = v[a][0], v[b][0], v[c][0]
            cr = cross(tuple(pb[k] - pa[k] for k in range(3)), tuple(pc[k] - pa[k] for k in range(3)))
            if sum(x * x for x in cr) < 1e-14:  # en la punta de un cono el triángulo se aplana
                continue
            n = norm(cr)
            base = len(fv)
            fv += [(pa, n), (pb, n), (pc, n)]
            ft.append((base, base + 1, base + 2))
        v, t = fv, ft
    if caps:
        for y, r, n in ((0, r0, (0, -1, 0)), (h, r1, (0, 1, 0))):
            if r <= 0:
                continue
            c0 = len(v)
            v.append(((0, y, 0), n))
            for i in range(seg + 1):
                a = 2 * math.pi * i / seg
                v.append(((r * math.cos(a), y, r * math.sin(a)), n))
            for i in range(seg):
                if n[1] > 0:
                    t.append((c0, c0 + 2 + i, c0 + 1 + i))
                else:
                    t.append((c0, c0 + 1 + i, c0 + 2 + i))
    return v, t


def sphere(rx, ry=None, rz=None, seg=12, rings=8):
    ry = rx if ry is None else ry
    rz = rx if rz is None else rz
    v, t = [], []
    for j in range(rings + 1):
        phi = math.pi * j / rings
        for i in range(seg + 1):
            th = 2 * math.pi * i / seg
            d = (math.sin(phi) * math.cos(th), math.cos(phi), math.sin(phi) * math.sin(th))
            p = (d[0] * rx, d[1] * ry, d[2] * rz)
            v.append((p, norm((d[0] / rx, d[1] / ry, d[2] / rz))))
    for j in range(rings):
        for i in range(seg):
            a = j * (seg + 1) + i
            b = a + seg + 1
            t += [(a, b + 1, b), (a, a + 1, b + 1)]
    return v, t


def rock(rx, ry, rz, seed=1, rough=0.22):
    """Roca: icosaedro subdividido con los vértices desplazados (caras planas)."""
    rnd = random.Random(seed)
    g = (1 + 5 ** 0.5) / 2
    pts = [norm(p) for p in [(-1, g, 0), (1, g, 0), (-1, -g, 0), (1, -g, 0), (0, -1, g), (0, 1, g), (0, -1, -g), (0, 1, -g),
                             (g, 0, -1), (g, 0, 1), (-g, 0, -1), (-g, 0, 1)]]
    tris = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6),
            (7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7),
            (9, 8, 1)]
    cache = {}

    def mid(a, b):
        k = (min(a, b), max(a, b))
        if k not in cache:
            pts.append(norm(tuple((pts[a][i] + pts[b][i]) / 2 for i in range(3))))
            cache[k] = len(pts) - 1
        return cache[k]

    nt = []
    for a, b, c in tris:
        ab, bc, ca = mid(a, b), mid(b, c), mid(c, a)
        nt += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
    scale = [1 + rough * (rnd.random() * 2 - 1) for _ in pts]
    pos = [(p[0] * rx * s, max(p[1], -0.35) * ry * s, p[2] * rz * s) for p, s in zip(pts, scale)]
    v, t = [], []
    for a, b, c in nt:
        pa, pb, pc = pos[a], pos[b], pos[c]
        n = norm(cross(tuple(pb[k] - pa[k] for k in range(3)), tuple(pc[k] - pa[k] for k in range(3))))
        base = len(v)
        v += [(pa, n), (pb, n), (pc, n)]
        t.append((base, base + 1, base + 2))
    return v, t


def gable(hx, hz, h):
    """Tejado a dos aguas: base 2hx × 2hz en y = 0, cumbrera a lo largo de X a altura h."""
    v, t = [], []

    def quad(a, b, c, d):
        n = norm(cross(tuple(b[k] - a[k] for k in range(3)), tuple(c[k] - a[k] for k in range(3))))
        base = len(v)
        v.extend([(a, n), (b, n), (c, n), (d, n)])
        t.extend([(base, base + 1, base + 2), (base, base + 2, base + 3)])

    def tri(a, b, c):
        n = norm(cross(tuple(b[k] - a[k] for k in range(3)), tuple(c[k] - a[k] for k in range(3))))
        base = len(v)
        v.extend([(a, n), (b, n), (c, n)])
        t.append((base, base + 1, base + 2))

    quad((-hx, 0, hz), (hx, 0, hz), (hx, h, 0), (-hx, h, 0))
    quad((hx, 0, -hz), (-hx, 0, -hz), (-hx, h, 0), (hx, h, 0))
    tri((hx, 0, hz), (hx, 0, -hz), (hx, h, 0))
    tri((-hx, 0, -hz), (-hx, 0, hz), (-hx, h, 0))
    quad((-hx, 0, -hz), (hx, 0, -hz), (hx, 0, hz), (-hx, 0, hz))  # por debajo (aleros)
    return v, t


def pyramid(hx, hz, h):
    v, t = [], []
    corners = [(-hx, 0, -hz), (hx, 0, -hz), (hx, 0, hz), (-hx, 0, hz)]
    top = (0, h, 0)
    for i in range(4):
        a, b = corners[(i + 1) % 4], corners[i]
        n = norm(cross(tuple(b[k] - a[k] for k in range(3)), tuple(top[k] - a[k] for k in range(3))))
        base = len(v)
        v += [(a, n), (b, n), (top, n)]
        t.append((base, base + 1, base + 2))
    base = len(v)
    for c in corners:
        v.append((c, (0, -1, 0)))
    t += [(base, base + 1, base + 2), (base, base + 2, base + 3)]
    return v, t


# --- Pieza ------------------------------------------------------------------------------

MATERIALS = ['Cloth', 'Stone', 'Metal', 'Glow', 'Grass', 'Water', 'Glass']


class Piece:
    def __init__(self, name):
        self.name = name
        self.groups = {}
        self.colliders = []
        self.lights = []

    def add(self, prim, color, mat='Cloth', m=I):
        """Añade una primitiva. `color`: '#RRGGBB', (r, g, b) o función (posición local) → color."""
        verts, tris = prim
        g = self.groups.setdefault(mat, {'p': [], 'n': [], 'c': [], 'i': []})
        nm = normal_matrix(m)
        base = len(g['p']) // 3
        for p, n in verts:
            c = color(p) if callable(color) else color
            c = hexcol(c) if isinstance(c, str) else c
            w = apply(m, p)
            nn = norm(tuple(nm[i][0] * n[0] + nm[i][1] * n[1] + nm[i][2] * n[2] for i in range(3)))
            g['p'].extend(w)
            g['n'].extend(nn)
            g['c'].extend(to_linear(c) + (1.0,))
        # Con un reflejo (escala negativa) se invierte el sentido de los triángulos.
        det = (m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
               m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]))
        for a, b, c in tris:
            g['i'].extend((base + a, base + c, base + b) if det < 0 else (base + a, base + b, base + c))
        return self

    # Atajos frecuentes.
    def box(self, center, half, color, mat='Cloth', m=I):
        return self.add(box(*half), color, mat, mul(m, T(*center)))

    def beam(self, a, b, w, color, mat='Cloth', d=None, m=I):
        """Viga de sección w × d de `a` a `b`."""
        d = w if d is None else d
        ln = math.dist(a, b)
        mid = tuple((a[k] + b[k]) / 2 for k in range(3))
        return self.add(box(w / 2, ln / 2, d / 2), color, mat, mul(m, T(*mid), basis_y(tuple(b[k] - a[k] for k in range(3)))))

    def rod(self, a, b, r, color, mat='Cloth', seg=8, r1=None, m=I):
        """Cilindro de `a` a `b` (ramas, postes, cables)."""
        ln = math.dist(a, b)
        return self.add(cylinder(r, r if r1 is None else r1, ln, seg), color, mat,
                        mul(m, T(*a), basis_y(tuple(b[k] - a[k] for k in range(3)))))

    def collider(self, lo, hi):
        self.colliders.append((tuple(lo), tuple(hi)))
        return self

    def collider_box(self, center, half):
        return self.collider(tuple(center[k] - half[k] for k in range(3)), tuple(center[k] + half[k] for k in range(3)))

    def light(self, pos, color='#FFD9A0'):
        self.lights.append((tuple(pos), color.lstrip('#').upper()))
        return self

    def triangles(self):
        return sum(len(g['i']) for g in self.groups.values()) // 3

    # --- Escritura ----------------------------------------------------------------------

    def write(self, path):
        bin_ = bytearray()
        views, accessors = [], []

        def view(data, target):
            while len(bin_) % 4:
                bin_.append(0)
            off = len(bin_)
            bin_.extend(data)
            v = {'buffer': 0, 'byteOffset': off, 'byteLength': len(data)}
            if target:
                v['target'] = target
            views.append(v)
            return len(views) - 1

        def acc(values, ctype, count, typ, minmax=False, target=34962):
            a = array('f' if ctype == 5126 else 'I', values)
            acc_ = {'bufferView': view(a.tobytes(), target), 'componentType': ctype, 'count': count, 'type': typ}
            if minmax:
                xs, ys, zs = a[0::3], a[1::3], a[2::3]
                acc_['min'] = [min(xs), min(ys), min(zs)]
                acc_['max'] = [max(xs), max(ys), max(zs)]
            accessors.append(acc_)
            return len(accessors) - 1

        def prim(g, material=None):
            n = len(g['p']) // 3
            p = {'attributes': {'POSITION': acc(g['p'], 5126, n, 'VEC3', True), 'NORMAL': acc(g['n'], 5126, n, 'VEC3')},
                 'indices': acc(g['i'], 5125, len(g['i']), 'SCALAR', target=34963)}
            if 'c' in g:
                p['attributes']['COLOR_0'] = acc(g['c'], 5126, n, 'VEC4')
            if material is not None:
                p['material'] = material
            return p

        materials, prims = [], []
        for name in MATERIALS:
            g = self.groups.get(name)
            if not g or not g['i']:
                continue
            mat = {'name': name, 'pbrMetallicRoughness': {'baseColorFactor': [1, 1, 1, 1],
                                                          'metallicFactor': 0.7 if name in ('Metal', 'Glass') else 0.0,
                                                          'roughnessFactor': 0.3 if name in ('Metal', 'Glass', 'Water') else 0.85}}
            if name == 'Glow':
                mat['emissiveFactor'] = [1, 1, 1]
            materials.append(mat)
            prims.append(prim(g, len(materials) - 1))
        meshes = [{'name': self.name, 'primitives': prims}]
        nodes = [{'name': self.name, 'children': []}, {'name': self.name + '_Malla', 'mesh': 0}]
        nodes[0]['children'].append(1)
        for i, (lo, hi) in enumerate(self.colliders):
            v, t = box((hi[0] - lo[0]) / 2, (hi[1] - lo[1]) / 2, (hi[2] - lo[2]) / 2)
            g = {'p': [], 'n': [], 'i': []}
            for p, n in v:
                g['p'].extend(p)
                g['n'].extend(n)
            for tri in t:
                g['i'].extend(tri)
            meshes.append({'name': 'COL_%d' % i, 'primitives': [prim(g)]})
            center = [(lo[k] + hi[k]) / 2 for k in range(3)]
            nodes.append({'name': 'COL_%d' % i, 'mesh': len(meshes) - 1, 'translation': center})
            nodes[0]['children'].append(len(nodes) - 1)
        for pos, col in self.lights:
            nodes.append({'name': 'LUZ_' + col, 'translation': list(pos)})
            nodes[0]['children'].append(len(nodes) - 1)
        while len(bin_) % 4:
            bin_.append(0)
        doc = {'asset': {'version': '2.0', 'generator': 'unamed_game tools/props'}, 'scene': 0, 'scenes': [{'nodes': [0]}],
               'nodes': nodes, 'meshes': meshes, 'materials': materials, 'accessors': accessors, 'bufferViews': views,
               'buffers': [{'byteLength': len(bin_)}]}
        js = json.dumps(doc, separators=(',', ':')).encode()
        while len(js) % 4:
            js += b' '
        with open(path, 'wb') as f:
            f.write(struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(bin_)))
            f.write(struct.pack('<II', len(js), 0x4E4F534A))
            f.write(js)
            f.write(struct.pack('<II', len(bin_), 0x004E4942))
            f.write(bin_)
