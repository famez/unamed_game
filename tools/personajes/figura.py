"""Personajes con esqueleto (.glb con armadura) hechos por piezas, para los habitantes.

El esqueleto se copia tal cual de un .glb que ya funciona en el juego (por defecto
assets/models/ilse.glb, el mismo esqueleto que la protagonista): nodos, nombres, orden y
matrices inversas de la pose de reposo. Cada pieza va pegada al 100 % a un hueso (skinning
rígido), en la pose de reposo (pose A, mirando a +Z, la izquierda en +X, metros).
"""

import array
import json
import math
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'props'))

from kit import Piece, MATERIALS, box, cylinder, sphere, mix, shade, hexcol, to_linear, T, RX, RY, RZ, S, mul, basis_y, norm  # noqa: E402

BONES = ['pelvis', 'spine', 'chest', 'neck', 'head', 'shoulder_l', 'elbow_l', 'wrist_l', 'shoulder_r', 'elbow_r', 'wrist_r',
         'hip_l', 'knee_l', 'ankle_l', 'toe_l', 'hip_r', 'knee_r', 'ankle_r', 'toe_r']


def read_glb(path):
    b = open(path, 'rb').read()
    jl = struct.unpack_from('<I', b, 12)[0]
    js = json.loads(b[20:20 + jl])
    return js, b[20 + jl + 8:]


class Skeleton:
    """El esqueleto de referencia: posición de cada hueso en la pose de reposo."""

    def __init__(self, path):
        js, bin_ = read_glb(path)
        skin = js['skins'][0]
        acc = js['accessors'][skin['inverseBindMatrices']]
        bv = js['bufferViews'][acc['bufferView']]
        off = bv.get('byteOffset', 0) + acc.get('byteOffset', 0)
        self.ibm = array.array('f', bin_[off:off + 64 * acc['count']])
        self.joint_nodes = skin['joints']
        self.nodes = js['nodes']
        self.mesh_node = next(k for k, n in enumerate(self.nodes) if 'skin' in n)
        self.roots = js['scenes'][js.get('scene', 0)]['nodes']
        self.names = [self.nodes[j]['name'] for j in self.joint_nodes]
        self.pos = {}
        for i, name in enumerate(self.names):
            m = self.ibm[i * 16:i * 16 + 16]
            R = [[m[0], m[4], m[8]], [m[1], m[5], m[9]], [m[2], m[6], m[10]]]
            t = [m[12], m[13], m[14]]
            self.pos[name] = tuple(-(R[0][k] * t[0] + R[1][k] * t[1] + R[2][k] * t[2]) for k in range(3))

    def index(self, bone):
        return self.names.index(bone)


class Figure(Piece):
    def __init__(self, name, skel):
        super().__init__(name)
        self.skel = skel
        self.bone = 'pelvis'

    def on(self, bone):
        """Lo que se añada a continuación va pegado a este hueso."""
        self.bone = bone
        return self

    def add(self, prim, color, mat='Cloth', m=None):
        m = m or [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]
        g = self.groups.setdefault(mat, {'p': [], 'n': [], 'c': [], 'i': [], 'j': []})
        before = len(g['p']) // 3
        super().add(prim, color, mat, m)
        g['j'].extend([self.skel.index(self.bone)] * (len(g['p']) // 3 - before))
        return self

    def J(self, bone):
        return self.skel.pos[bone]

    def limb(self, a, b, r0, r1, color, mat='Cloth', squash=1.0, seg=10):
        """Segmento de `a` a `b` (posiciones), cilindro con tapas redondas."""
        d = tuple(b[k] - a[k] for k in range(3))
        ln = math.sqrt(sum(x * x for x in d))
        rot = basis_y(d)
        self.add(cylinder(r0, r1, ln, seg), color, mat, mul(T(*a), rot, S(1, 1, squash)))
        self.add(sphere(r0, r0, r0 * squash, seg, 6), color, mat, mul(T(*a), rot))
        self.add(sphere(r1, r1, r1 * squash, seg, 6), color, mat, mul(T(*b), rot))
        return self

    def write(self, path):
        """Malla con skinning + el esqueleto copiado del de referencia."""
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

        def acc(data, ctype, count, typ, extra=None, target=34962):
            a = {'bufferView': view(data, target), 'componentType': ctype, 'count': count, 'type': typ}
            a.update(extra or {})
            accessors.append(a)
            return len(accessors) - 1

        materials, prims = [], []
        for name in MATERIALS:
            g = self.groups.get(name)
            if not g or not g['i']:
                continue
            n = len(g['p']) // 3
            pos = array.array('f', g['p'])
            mm = {'min': [min(pos[0::3]), min(pos[1::3]), min(pos[2::3])], 'max': [max(pos[0::3]), max(pos[1::3]), max(pos[2::3])]}
            joints = bytes(v for j in g['j'] for v in (j, 0, 0, 0))
            weights = bytes(v for _ in g['j'] for v in (255, 0, 0, 0))
            attrs = {'POSITION': acc(pos.tobytes(), 5126, n, 'VEC3', mm), 'NORMAL': acc(array.array('f', g['n']).tobytes(), 5126, n, 'VEC3'),
                     'COLOR_0': acc(array.array('f', g['c']).tobytes(), 5126, n, 'VEC4'),
                     'JOINTS_0': acc(joints, 5121, n, 'VEC4'), 'WEIGHTS_0': acc(weights, 5121, n, 'VEC4', {'normalized': True})}
            idx = acc(array.array('I', g['i']).tobytes(), 5125, len(g['i']), 'SCALAR', target=34963)
            mat = {'name': name, 'pbrMetallicRoughness': {'baseColorFactor': [1, 1, 1, 1],
                                                          'metallicFactor': 0.7 if name in ('Metal', 'Glass') else 0.0,
                                                          'roughnessFactor': 0.35 if name in ('Metal', 'Glass') else 0.85}}
            if name == 'Glow':
                mat['emissiveFactor'] = [1, 1, 1]
            materials.append(mat)
            prims.append({'attributes': attrs, 'indices': idx, 'material': len(materials) - 1})
        ibm = acc(self.skel.ibm.tobytes(), 5126, len(self.skel.names), 'MAT4', target=0)
        nodes = json.loads(json.dumps(self.skel.nodes))
        nodes[self.skel.mesh_node] = {'name': self.name + '_Mesh', 'mesh': 0, 'skin': 0}
        armature = next(k for k, n in enumerate(nodes) if self.skel.joint_nodes[0] in n.get('children', []))
        nodes[armature]['name'] = self.name + '_Armature'
        while len(bin_) % 4:
            bin_.append(0)
        doc = {'asset': {'version': '2.0', 'generator': 'unamed_game tools/personajes'}, 'scene': 0,
               'scenes': [{'nodes': self.skel.roots}], 'nodes': nodes, 'meshes': [{'name': self.name, 'primitives': prims}],
               'skins': [{'name': self.name + '_Armature', 'inverseBindMatrices': ibm, 'joints': self.skel.joint_nodes}],
               'materials': materials, 'accessors': accessors, 'bufferViews': views, 'buffers': [{'byteLength': len(bin_)}]}
        js = json.dumps(doc, separators=(',', ':')).encode()
        while len(js) % 4:
            js += b' '
        with open(path, 'wb') as f:
            f.write(struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(bin_)))
            f.write(struct.pack('<II', len(js), 0x4E4F534A))
            f.write(js)
            f.write(struct.pack('<II', len(bin_), 0x004E4942))
            f.write(bin_)
