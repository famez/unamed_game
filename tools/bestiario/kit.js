// Kit compartido: modelado por huesos + exportador GLB compatible con el motor de unamed_game.
// Cada pieza cuelga del grupo de su hueso, colocado en la pose de bind ORIGINAL (de sus IBM).
// El exportador conserva nodos, skin e inverseBindMatrices originales y solo sustituye la malla.
import * as THREE from 'three';
import { readFileSync, writeFileSync } from 'node:fs';

export const V = (x, y, z) => new THREE.Vector3(x, y, z);

// Paleta común (misma familia que Vela / Hollín)
export const P = {
  // luz
  coat: '#EDE8DF', gold: '#D9A441', goldDark: '#A8752A', porcelain: '#E7E1D3', porcelainShade: '#CFC6B4', iron: '#2B2D36', cream: '#D8CFBD',
  // sombra
  ink: '#221E2B', inkDeep: '#16131C', inkLight: '#2E2838', crystal: '#2A1E3D', violetDark: '#5B3A8C', violet: '#9B5CFF', ironOld: '#1C1A22', scarfViolet: '#3E2163',
  // neutros
  plum: '#2A2433', plumLight: '#3A3046', fur: '#D8D3E0', furShade: '#B9B2C6', hairWhite: '#E4E0EE', skin: '#F1DDD2', embroidery: '#C9A35A',
  white: '#F4F0F7', lash: '#2A1E26', mouth: '#B8606A',
};
// Brillos (material Glow_RRGGBB): mismos tonos que Vela (luz) y Hollín (vacío)
export const G = { warm: 'FFD27A', hot: 'FFF4D6', void: 'A35CFF', voidBright: 'C98BFF' };

// ---------- esqueleto ----------
const JOINTS = JSON.parse(readFileSync(new URL('./joints.json', import.meta.url)));

export class Rig {
  constructor(name, customBind = null) {
    this.name = name; this.root = new THREE.Group(); this.groups = {};
    this.bind = customBind || JOINTS[name] || JOINTS.chica_neon; this.custom = customBind;
  }
  // grupo del hueso en su pose de bind (eje -Y local = dirección del miembro)
  j(name) {
    if (this.groups[name]) return this.groups[name];
    const b = this.bind[name]; if (!b) throw new Error('hueso desconocido ' + name);
    const g = new THREE.Group(); g.name = name; g.userData.joint = name;
    const R = b.R; const m = new THREE.Matrix4().set(R[0][0], R[0][1], R[0][2], b.pos[0], R[1][0], R[1][1], R[1][2], b.pos[1], R[2][0], R[2][1], R[2][2], b.pos[2], 0, 0, 0, 1);
    m.decompose(g.position, g.quaternion, g.scale);
    this.root.add(g); this.groups[name] = g; return g;
  }
  pos(name) { return V(...this.bind[name].pos); }
  len(a, b) { return this.pos(a).distanceTo(this.pos(b)); }
  // grupo situado en coordenadas MUNDO pero asignado al hueso indicado
  world(joint) {
    const g = new THREE.Group(); g.userData.joint = joint; g.name = joint + '_w'; this.root.add(g); return g;
  }
}

// Esqueleto propio: lista de [nombre, padre|null, [x,y,z] en mundo]. Huesos sin rotación en bind (eje local = eje mundo).
export function skeleton(list) {
  const b = {}; for (const [n, p, pos] of list) b[n] = { pos, R: [[1, 0, 0], [0, 1, 0], [0, 0, 1]], parent: p }; return b;
}

// ---------- piezas ----------
// opts: pos, rot, scl, paint (hex) | glow (HEX), double (cara doble para superficies abiertas)
export function part(parent, geo, paint, o = {}) {
  const m = new THREE.Mesh(geo, new THREE.MeshBasicMaterial());
  if (o.pos) m.position.set(...o.pos); if (o.rot) m.rotation.set(...o.rot); if (o.scl) m.scale.set(...o.scl);
  if (o.quat) m.quaternion.copy(o.quat);
  m.userData.paint = paint; m.userData.glow = o.glow || null; m.userData.double = !!o.double;
  parent.add(m); return m;
}
export const glow = (parent, geo, hex, o = {}) => part(parent, geo, '#' + hex, { ...o, glow: hex });

export const sphere = (r, w = 20, h = 14) => new THREE.SphereGeometry(r, w, h);
export const capsule = (r, l, seg = 14) => new THREE.CapsuleGeometry(r, l, 5, seg);
export const cyl = (rt, rb, h, s = 16, open = false) => new THREE.CylinderGeometry(rt, rb, h, s, 1, open);
export const torus = (R, r, rs = 8, ts = 28, arc = Math.PI * 2) => new THREE.TorusGeometry(R, r, rs, ts, arc);
export const box = (x, y, z) => new THREE.BoxGeometry(x, y, z);
export function cone(r, h, s = 6) { const g = new THREE.ConeGeometry(r, h, s, 1); g.translate(0, h / 2, 0); return g; } // base en el origen
export function tube(points, r, segs = 40, closed = false, radial = 7) {
  return new THREE.TubeGeometry(new THREE.CatmullRomCurve3(points, closed), segs, r, radial, closed);
}
export function lathe(profile, phiStart = 0, phiLen = Math.PI * 2, segs = 40) {
  return new THREE.LatheGeometry(profile.map(([r, y]) => new THREE.Vector2(r, y)), segs, phiStart, phiLen);
}
// orienta una pieza para que su +Y apunte a dir
export function aim(mesh, dir) { mesh.quaternion.setFromUnitVectors(V(0, 1, 0), dir.clone().normalize()); return mesh; }
// pinchos / mechones
export function spike(parent, paint, base, dir, h, r, o = {}) {
  const m = part(parent, cone(r, h, o.sides || 6), paint, { glow: o.glow }); m.position.copy(base); return aim(m, dir);
}
// bajo dentado de una prenda hecha con lathe
export function jag(geo, nprof, segs, depth, every = 2) {
  const p = geo.attributes.position;
  for (let i = 0; i <= segs; i++) {
    const v = i * nprof + (nprof - 1);
    if (i % every) p.setY(v, p.getY(v) - depth * (0.55 + 0.45 * Math.abs(Math.sin(i * 1.7))));
  }
  geo.computeVertexNormals(); return geo;
}
// cabeza anime (misma deformación que Vela/Hollín)
export function animeHead(R = 0.115, chin = 0.46) {
  const g = new THREE.SphereGeometry(1, 40, 30); const p = g.attributes.position;
  for (let i = 0; i < p.count; i++) {
    let x = p.getX(i), y = p.getY(i), z = p.getZ(i);
    if (y < 0) { const t = -y; x *= 1 - chin * Math.pow(t, 1.5); z = z * (1 - 0.18 * t) + (z > 0 ? 0.06 * t * t : 0); y *= 1.16; } else y *= 1.02;
    if (z < 0) z *= 1.05;
    p.setXYZ(i, x * R, y * R, z * R);
  }
  g.computeVertexNormals(); return g;
}
// calcas sobre la cara: devuelve un grupo orientado a la normal de la superficie en (x, y)
export function faceDecal(parent, headGeo, x, y, lift = 0.004) {
  const probe = new THREE.Mesh(headGeo, new THREE.MeshBasicMaterial({ side: THREE.DoubleSide })); probe.updateMatrixWorld(true);
  const ray = new THREE.Raycaster(V(x, y, 1), V(0, 0, -1)); const hit = ray.intersectObject(probe)[0];
  const n = hit.face.normal.clone().normalize(); const g = new THREE.Group();
  g.position.copy(hit.point).addScaledVector(n, lift); g.quaternion.setFromUnitVectors(V(0, 0, 1), n); parent.add(g); return g;
}
export const disc = (r = 0.5, s = 28) => new THREE.CircleGeometry(r, s);
export function crack(parent, pts, r, hex) { return glow(parent, tube(pts, r, Math.max(8, pts.length * 6), false, 5), hex); }
// zigzag sobre un cilindro de radio rad (eje Y local), ángulo a0, de y0 a y1
export function zigzag(rad, a0, y0, y1, amp, n = 6) {
  const pts = []; for (let i = 0; i <= n; i++) { const t = i / n, a = a0 + (i % 2 ? amp : -amp) * (0.6 + 0.4 * Math.sin(i * 1.7)); pts.push(V(rad * Math.sin(a), y0 + (y1 - y0) * t, rad * Math.cos(a))); }
  return pts;
}

// ---------- exportador ----------
const srgbToLin = (c) => (c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4));
function linColor(hex) { const c = new THREE.Color(hex); /* THREE.Color(hex) ya convierte a lineal */ return [c.r, c.g, c.b, 1]; }

export function exportGLB(rig, origPath, outPath) {
  let J, skin, jointNames, ibmBytes;
  if (rig.custom) {
    // nodos: Armature(0) > Mesh(1) + huesos, con el mismo layout que los .glb del motor
    const names = Object.keys(rig.custom); const idx = {}; names.forEach((n, i) => (idx[n] = i + 2));
    const nodes = [{ name: `${rig.name}_Armature`, children: [1, ...names.filter((n) => !rig.custom[n].parent).map((n) => idx[n])] }, { name: `${rig.name}_Mesh`, mesh: 0, skin: 0 }];
    for (const n of names) {
      const b = rig.custom[n]; const pp = b.parent ? rig.custom[b.parent].pos : [0, 0, 0];
      const node = { name: n, translation: [b.pos[0] - pp[0], b.pos[1] - pp[1], b.pos[2] - pp[2]] };
      const ch = names.filter((c) => rig.custom[c].parent === n).map((c) => idx[c]); if (ch.length) node.children = ch; nodes.push(node);
    }
    J = { scenes: [{ nodes: [0] }], scene: 0, nodes, meshes: [{ name: `${rig.name}_Mesh` }] };
    skin = { name: `${rig.name}_Armature`, skeleton: 0, joints: names.map((n) => idx[n]) }; jointNames = names;
    const ibm = new Float32Array(names.length * 16);
    names.forEach((n, i) => { const p = rig.custom[n].pos; ibm.set([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -p[0], -p[1], -p[2], 1], i * 16); });
    ibmBytes = Buffer.from(ibm.buffer);
  } else {
    const buf = readFileSync(origPath);
    const jlen = buf.readUInt32LE(12); J = JSON.parse(buf.subarray(20, 20 + jlen).toString());
    const boff = 20 + jlen; const blen = buf.readUInt32LE(boff); const BIN = buf.subarray(boff + 8, boff + 8 + blen);
    skin = J.skins[0]; jointNames = skin.joints.map((i) => J.nodes[i].name);
    const ibmAcc = J.accessors[skin.inverseBindMatrices]; const ibmBV = J.bufferViews[ibmAcc.bufferView];
    ibmBytes = BIN.subarray((ibmBV.byteOffset || 0) + (ibmAcc.byteOffset || 0), (ibmBV.byteOffset || 0) + (ibmAcc.byteOffset || 0) + ibmAcc.count * 64);
  }

  rig.root.updateMatrixWorld(true);
  const buckets = new Map(); // material -> arrays
  const getB = (k) => { if (!buckets.has(k)) buckets.set(k, { pos: [], nor: [], col: [], jnt: [], idx: [] }); return buckets.get(k); };
  let tris = 0;
  for (const [jname, g] of Object.entries(rig.groups).concat(rig.root.children.filter((c) => c.name.endsWith('_w')).map((c) => [c.userData.joint, c]))) {
    const ji = jointNames.indexOf(jname); if (ji < 0) throw new Error(`hueso ${jname} no está en ${origPath}`);
    g.traverse((o) => {
      if (!o.isMesh) return;
      let geo = o.geometry.clone(); if (!geo.index) geo.setIndex([...Array(geo.attributes.position.count).keys()]);
      if (!geo.attributes.normal) geo.computeVertexNormals();
      geo.applyMatrix4(o.matrixWorld);
      const flip = o.matrixWorld.determinant() < 0;
      const key = o.userData.glow ? 'Glow_' + o.userData.glow : 'Cloth';
      const col = linColor(o.userData.paint || '#ff00ff');
      const passes = o.userData.double ? [false, true] : [false];
      for (const back of passes) {
        const b = getB(key); const base = b.pos.length / 3;
        const p = geo.attributes.position, n = geo.attributes.normal, ix = geo.index.array;
        for (let i = 0; i < p.count; i++) {
          b.pos.push(p.getX(i), p.getY(i), p.getZ(i));
          const s = back ? -1 : 1; b.nor.push(n.getX(i) * s, n.getY(i) * s, n.getZ(i) * s);
          b.col.push(...col); b.jnt.push(ji, 0, 0, 0);
        }
        const rev = flip !== back;
        for (let t = 0; t < ix.length; t += 3) {
          if (rev) b.idx.push(base + ix[t], base + ix[t + 2], base + ix[t + 1]); else b.idx.push(base + ix[t], base + ix[t + 1], base + ix[t + 2]);
        }
        tris += ix.length / 3;
      }
    });
  }
  // ---- montar JSON + binario ----
  const chunks = []; let offset = 0; const bufferViews = [], accessors = [];
  const pushBV = (typed, target) => {
    const bytes = Buffer.from(typed.buffer, typed.byteOffset, typed.byteLength); const pad = (4 - (bytes.length % 4)) % 4;
    bufferViews.push({ buffer: 0, byteOffset: offset, byteLength: bytes.length, ...(target ? { target } : {}) });
    chunks.push(bytes, Buffer.alloc(pad)); offset += bytes.length + pad; return bufferViews.length - 1;
  };
  const materials = [{ name: 'Cloth', pbrMetallicRoughness: { baseColorFactor: [1, 1, 1, 1], metallicFactor: 0, roughnessFactor: 0.8 } }];
  const prims = [];
  const keys = ['Cloth', ...[...buckets.keys()].filter((k) => k !== 'Cloth').sort()];
  for (const k of keys) {
    const b = buckets.get(k); if (!b) continue;
    let mi = 0;
    if (k !== 'Cloth') {
      const hex = k.slice(5); const c = new THREE.Color('#' + hex);
      materials.push({ name: k, pbrMetallicRoughness: { baseColorFactor: [1, 1, 1, 1], metallicFactor: 0, roughnessFactor: 0.8 }, emissiveFactor: [c.r, c.g, c.b] }); mi = materials.length - 1;
    }
    const pos = new Float32Array(b.pos), cnt = pos.length / 3;
    const mn = [Infinity, Infinity, Infinity], mx = [-Infinity, -Infinity, -Infinity];
    for (let i = 0; i < cnt; i++) for (let a = 0; a < 3; a++) { mn[a] = Math.min(mn[a], pos[i * 3 + a]); mx[a] = Math.max(mx[a], pos[i * 3 + a]); }
    const acc = (bv, ct, type, extra = {}) => { accessors.push({ bufferView: bv, componentType: ct, count: cnt, type, ...extra }); return accessors.length - 1; };
    const aP = acc(pushBV(pos, 34962), 5126, 'VEC3', { min: mn, max: mx });
    const aN = acc(pushBV(new Float32Array(b.nor), 34962), 5126, 'VEC3');
    const aC = acc(pushBV(new Float32Array(b.col), 34962), 5126, 'VEC4');
    const aJ = acc(pushBV(new Uint8Array(b.jnt), 34962), 5121, 'VEC4');
    const w = new Uint8Array(cnt * 4); for (let i = 0; i < cnt; i++) w[i * 4] = 255;
    const aW = acc(pushBV(w, 34962), 5121, 'VEC4', { normalized: true });
    const idx = new Uint32Array(b.idx); bufferViews.length; const bvI = pushBV(idx, 34963);
    accessors.push({ bufferView: bvI, componentType: 5125, count: idx.length, type: 'SCALAR' }); const aI = accessors.length - 1;
    prims.push({ attributes: { POSITION: aP, NORMAL: aN, COLOR_0: aC, JOINTS_0: aJ, WEIGHTS_0: aW }, indices: aI, material: mi });
  }
  const bvIBM = pushBV(new Uint8Array(ibmBytes.buffer, ibmBytes.byteOffset, ibmBytes.byteLength), null);
  accessors.push({ bufferView: bvIBM, componentType: 5126, count: jointNames.length, type: 'MAT4' });
  const out = {
    asset: { version: '2.0', generator: 'unamed_game' },
    scene: 0, scenes: J.scenes, nodes: J.nodes,
    meshes: [{ name: J.meshes[0].name, primitives: prims }],
    skins: [{ ...skin, inverseBindMatrices: accessors.length - 1 }],
    materials, accessors, bufferViews, buffers: [{ byteLength: offset }],
  };
  if (J.scene === undefined) delete out.scene;
  const bin = Buffer.concat(chunks);
  let js = Buffer.from(JSON.stringify(out)); const jp = (4 - (js.length % 4)) % 4; js = Buffer.concat([js, Buffer.alloc(jp, 0x20)]);
  const total = 12 + 8 + js.length + 8 + bin.length; const hdr = Buffer.alloc(12); hdr.write('glTF', 0); hdr.writeUInt32LE(2, 4); hdr.writeUInt32LE(total, 8);
  const h1 = Buffer.alloc(8); h1.writeUInt32LE(js.length, 0); h1.write('JSON', 4);
  const h2 = Buffer.alloc(8); h2.writeUInt32LE(bin.length, 0); h2.write('BIN\0', 4);
  writeFileSync(outPath, Buffer.concat([hdr, h1, js, h2, bin]));
  return { tris, materials: materials.map((m) => m.name), bytes: total };
}
