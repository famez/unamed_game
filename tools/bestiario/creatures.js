// Bestiario de tamaños: criaturas con anatomía y escala propias, en el lenguaje visual de Farolas.
// Cada una tiene su propio esqueleto (skeleton([...])) y piezas orgánicas (blobs deformados, miembros cónicos)
// en vez de cilindros iguales. Unidades: metros; Y arriba; miran a +Z.
import * as THREE from 'three';
import { Rig, P, G, V, part, glow, sphere, capsule, cyl, torus, box, cone, tube, lathe, aim, spike, jag, disc, crack, zigzag, skeleton } from './kit.js';

const PI = Math.PI;

// ---------- piezas orgánicas ----------
// miembro cónico entre dos puntos, con articulación redondeada
function limb(parent, a, b, r0, r1, paint, o = {}) {
  const d = b.clone().sub(a); const L = d.length();
  const g = new THREE.CylinderGeometry(r1, r0, L, o.seg || 14, 4); g.translate(0, L / 2, 0);
  if (o.bulge) { const p = g.attributes.position; for (let i = 0; i < p.count; i++) { const t = p.getY(i) / L; const k = 1 + o.bulge * Math.sin(PI * t); p.setX(i, p.getX(i) * k); p.setZ(i, p.getZ(i) * k); } g.computeVertexNormals(); }
  const m = part(parent, g, paint, { glow: o.glow }); m.position.copy(a); aim(m, d);
  if (o.caps !== false) { part(parent, sphere(r0, 14, 10), paint, { pos: a.toArray() }); part(parent, sphere(r1, 12, 8), paint, { pos: b.toArray() }); }
  return m;
}
// esfera deformada con ruido suave: cuerpos orgánicos, pelo, roca
function blob(parent, pos, scl, paint, o = {}) {
  const g = new THREE.SphereGeometry(1, o.w || 30, o.h || 22); const p = g.attributes.position; const s = o.seed || 1, nz = o.noise || 0, f = o.freq || 3;
  for (let i = 0; i < p.count; i++) {
    const x = p.getX(i), y = p.getY(i), z = p.getZ(i);
    let k = 1 + nz * Math.sin(x * f * 1.03 + s) * Math.sin(y * f * 0.91 + s * 2.1) * Math.sin(z * f * 1.17 + s * 3.3);
    if (o.flatBottom && y < -o.flatBottom) k *= (-o.flatBottom) / y;
    if (o.taperTop && y > 0) { const t = 1 - o.taperTop * y; p.setX(i, x * t); p.setZ(i, z * t); }
    p.setXYZ(i, p.getX(i) * k, y * k, p.getZ(i) * k);
  }
  g.computeVertexNormals();
  return part(parent, g, paint, { pos: pos, scl: scl, rot: o.rot, glow: o.glow });
}
// borla de pelo/hollín: varios blobs pequeños
function fluff(parent, center, radius, count, paint, size, seed = 1) {
  const rnd = mulberry(seed);
  for (let i = 0; i < count; i++) {
    const u = rnd() * 2 - 1, th = rnd() * 2 * PI, r = Math.sqrt(1 - u * u);
    const d = V(r * Math.cos(th), u, r * Math.sin(th));
    blob(parent, center.clone().add(d.clone().multiply(radius)).toArray(), [size * (0.7 + 0.6 * rnd()), size * (0.7 + 0.6 * rnd()), size * (0.7 + 0.6 * rnd())], paint, { noise: 0.15, seed: i, w: 10, h: 8 });
  }
}
function mulberry(a) { return () => { a |= 0; a = (a + 0x6d2b79f5) | 0; let t = Math.imul(a ^ (a >>> 15), 1 | a); t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t; return ((t ^ (t >>> 14)) >>> 0) / 4294967296; }; }
// ojo cartoon: blanco + iris + pupila + brillo, orientado hacia dir
function eye(parent, pos, dir, r, iris, o = {}) {
  const g = new THREE.Group(); g.position.copy(pos); g.quaternion.setFromUnitVectors(V(0, 0, 1), dir.clone().normalize()); parent.add(g);
  part(g, sphere(r, 16, 12), o.white || P.white, { scl: [1, o.tall || 1.1, 0.6] });
  if (o.glowIris) glow(g, disc(r * 0.68, 20), o.glowIris, { pos: [0, -r * 0.05, r * 0.6] }); else part(g, disc(r * 0.68, 20), iris, { pos: [0, -r * 0.05, r * 0.6] });
  part(g, disc(r * (o.pupil || 0.34), 16), '#140E14', { pos: [0, -r * 0.05, r * 0.61], scl: [o.slit ? 0.35 : 1, 1, 1] });
  part(g, disc(r * 0.16, 10), P.white, { pos: [r * 0.25, r * 0.25, r * 0.62] });
  return g;
}
// farolillo colgante (motivo común)
function lantern(parent, pos, s, glowHex = G.warm, metal = P.iron) {
  const g = new THREE.Group(); g.position.copy(pos); g.scale.setScalar(s); parent.add(g);
  part(g, torus(0.012, 0.003, 6, 12), metal, { pos: [0, 0.02, 0] });
  part(g, cone(0.045, 0.035, 6), metal, { pos: [0, -0.01, 0] });
  glow(g, cyl(0.03, 0.03, 0.07, 6), glowHex, { pos: [0, -0.05, 0] });
  for (let i = 0; i < 6; i++) { const a = i * PI / 3; part(g, box(0.005, 0.075, 0.005), metal, { pos: [0.034 * Math.cos(a), -0.05, 0.034 * Math.sin(a)] }); }
  part(g, cyl(0.04, 0.032, 0.012, 6), metal, { pos: [0, -0.09, 0] });
  return g;
}

// ======================= 1. MECHITA · espíritu de vela (0,38 m) =======================
export function mechita() {
  const r = new Rig('mechita', skeleton([['root', null, [0, 0, 0]], ['body', 'root', [0, 0.1, 0]], ['head', 'body', [0, 0.27, 0]],
    ['arm_l', 'body', [0.055, 0.19, 0]], ['arm_r', 'body', [-0.055, 0.19, 0]], ['leg_l', 'root', [0.03, 0.07, 0]], ['leg_r', 'root', [-0.03, 0.07, 0]]]));
  const bd = r.world('body'), hd = r.world('head');
  const WAX = '#F1E7D0', WAX2 = '#E2D3B4';
  // cuerpo de cera, más ancho abajo, con el borde superior derretido
  part(bd, lathe([[0.0, 0.07], [0.068, 0.07], [0.072, 0.09], [0.066, 0.16], [0.06, 0.22], [0.058, 0.255], [0.045, 0.262], [0.0, 0.258]], 0, 2 * PI, 28), WAX);
  for (let i = 0; i < 7; i++) { const a = i / 7 * 2 * PI + 0.9; if (Math.abs(Math.sin(a) - 1) < 0.35) continue; const L = 0.025 + 0.035 * Math.abs(Math.sin(i * 2.3)); const x = 0.06 * Math.cos(a), z = 0.06 * Math.sin(a);
    blob(bd, [x * 1.03, 0.25 - L * 0.45, z * 1.03], [0.014, L * 0.55, 0.01], WAX2, { taperTop: -0.3, rot: [0, -a + PI / 2, 0] }); part(bd, sphere(0.013, 8, 6), WAX2, { pos: [x * 1.06, 0.25 - L * 0.95, z * 1.06], scl: [1, 1.2, 0.8] }); }
  // cara: dos ojos de botón y boquita, en la cera
  for (const s of [1, -1]) { part(bd, sphere(0.011, 10, 8), '#2A1E26', { pos: [0.022 * s, 0.205, 0.056], scl: [0.9, 1.2, 0.5] }); part(bd, sphere(0.004, 6, 4), P.white, { pos: [0.025 * s, 0.21, 0.061] }); part(bd, sphere(0.008, 8, 6), '#F0B9A8', { pos: [0.036 * s, 0.19, 0.052], scl: [1.2, 0.7, 0.4] }); }
  part(bd, torus(0.008, 0.0022, 6, 12, PI), '#8A4A3A', { pos: [0, 0.188, 0.058], rot: [0, 0, PI] });
  // mecha y llama (la llama es su pelo)
  part(hd, cyl(0.004, 0.005, 0.025, 6), '#2A1E26', { pos: [0, 0.27, 0] });
  blob(hd, [0, 0.315, 0], [0.032, 0.055, 0.032], '#' + G.warm, { glow: G.warm, taperTop: 0.6, noise: 0.06 });
  blob(hd, [0, 0.305, 0.004], [0.018, 0.03, 0.018], '#' + G.hot, { glow: G.hot, taperTop: 0.5 });
  // bracitos y piececitos
  for (const s of [1, -1]) {
    const a = r.world(s > 0 ? 'arm_l' : 'arm_r'); limb(a, V(0.055 * s, 0.19, 0), V(0.09 * s, 0.14, 0.02), 0.013, 0.01, WAX); part(a, sphere(0.014, 10, 8), WAX, { pos: [0.092 * s, 0.135, 0.022] });
    const l = r.world(s > 0 ? 'leg_l' : 'leg_r'); limb(l, V(0.03 * s, 0.075, 0), V(0.032 * s, 0.025, 0.005), 0.017, 0.015, WAX2); blob(l, [0.033 * s, 0.012, 0.018], [0.022, 0.013, 0.032], WAX2, { flatBottom: 0.3 });
  }
  return r;
}

// ======================= 2. HOLLINETE · gato de hollín (0,5 m de largo) =======================
export function hollinete() {
  const r = new Rig('hollinete', skeleton([['hips', null, [0, 0.17, -0.1]], ['chest', 'hips', [0, 0.18, 0.08]], ['neck', 'chest', [0, 0.22, 0.14]], ['head', 'neck', [0, 0.27, 0.18]],
    ['tail1', 'hips', [0, 0.2, -0.2]], ['tail2', 'tail1', [0, 0.32, -0.3]], ['fl', 'chest', [0.05, 0.13, 0.1]], ['fr', 'chest', [-0.05, 0.13, 0.1]], ['hl', 'hips', [0.055, 0.13, -0.12]], ['hr', 'hips', [-0.055, 0.13, -0.12]]]));
  const H = r.world('hips'), C = r.world('chest'), hd = r.world('head'), t1 = r.world('tail1'), t2 = r.world('tail2');
  const SOOT = '#2A2532', SOOT2 = '#3A3446';
  blob(H, [0, 0.18, -0.08], [0.1, 0.085, 0.13], SOOT, { noise: 0.12, freq: 5, seed: 2 });
  blob(C, [0, 0.19, 0.07], [0.09, 0.09, 0.1], SOOT, { noise: 0.12, freq: 5, seed: 3 });
  fluff(C, V(0, 0.2, 0), V(0.1, 0.08, 0.15), 14, SOOT2, 0.035, 4);
  blob(C, [0, 0.15, 0.12], [0.06, 0.05, 0.05], '#4A4256', { noise: 0.1 }); // pechera más clara
  // cabeza grande y redonda (proporción de cría: cabeza enorme)
  blob(hd, [0, 0.29, 0.21], [0.095, 0.085, 0.085], SOOT, { noise: 0.08, freq: 6, seed: 5 });
  fluff(hd, V(0, 0.3, 0.2), V(0.09, 0.08, 0.06), 10, SOOT2, 0.03, 6);
  for (const s of [1, -1]) {
    spike(hd, SOOT, V(0.05 * s, 0.35, 0.19), V(0.35 * s, 1, -0.1), 0.075, 0.035, { sides: 8 });
    spike(hd, '#5A3E5E', V(0.05 * s, 0.355, 0.2), V(0.35 * s, 1, -0.1), 0.05, 0.02, { sides: 8 });
    eye(hd, V(0.038 * s, 0.295, 0.285), V(0.25 * s, 0.05, 1), 0.027, null, { glowIris: G.warm, pupil: 0.3, slit: true });
    for (const k of [-1, 1]) part(hd, tube([V(0.03 * s, 0.27, 0.29), V(0.08 * s, 0.27 + 0.01 * k, 0.29), V(0.13 * s, 0.275 + 0.025 * k, 0.27)], 0.0015, 8, false, 4), '#9A93A8');
  }
  part(hd, sphere(0.009, 8, 6), '#C98BB0', { pos: [0, 0.272, 0.297], scl: [1.2, 0.8, 0.8] });
  part(hd, torus(0.009, 0.0018, 5, 10, PI), '#140E14', { pos: [0.009, 0.258, 0.295], rot: [0, 0, PI] }); part(hd, torus(0.009, 0.0018, 5, 10, PI), '#140E14', { pos: [-0.009, 0.258, 0.295], rot: [0, 0, PI] });
  // cola enroscada con farolillo
  part(t1, tube([V(0, 0.2, -0.2), V(0, 0.24, -0.27), V(0, 0.3, -0.3)], 0.025, 12, false, 8), SOOT);
  part(t2, tube([V(0, 0.3, -0.3), V(0, 0.38, -0.31), V(0, 0.42, -0.27), V(0, 0.41, -0.22)], 0.02, 14, false, 8), SOOT);
  fluff(t2, V(0, 0.41, -0.24), V(0.02, 0.02, 0.02), 4, SOOT2, 0.022, 7);
  lantern(t2, V(0, 0.385, -0.2), 0.75);
  // patitas cortas y redondas
  for (const [nm, x, z] of [['fl', 0.05, 0.1], ['fr', -0.05, 0.1], ['hl', 0.055, -0.12], ['hr', -0.055, -0.12]]) {
    const L = r.world(nm); limb(L, V(x, 0.13, z), V(x * 1.05, 0.035, z + 0.01), 0.028, 0.022, SOOT, { bulge: 0.15 });
    blob(L, [x * 1.05, 0.022, z + 0.025], [0.03, 0.022, 0.04], SOOT2, { flatBottom: 0.4 });
  }
  return r;
}

// ======================= 3. BÚHO DE COBRE · autómata informador (0,6 m posado) =======================
export function buho() {
  const r = new Rig('buho', skeleton([['root', null, [0, 0, 0]], ['body', 'root', [0, 0.28, 0]], ['head', 'body', [0, 0.47, 0.01]], ['wing_l', 'body', [0.12, 0.4, -0.01]], ['wing_r', 'body', [-0.12, 0.4, -0.01]], ['tail', 'body', [0, 0.2, -0.1]]]));
  const R0 = r.world('root'), bd = r.world('body'), hd = r.world('head'), tl = r.world('tail');
  const CU = '#B5703C', CU2 = '#8A5230', BR = '#C9A35A', PAT = '#5E8E7E';
  // percha: brazo de una farola
  part(R0, box(0.5, 0.04, 0.05), P.iron, { pos: [0, 0.1, 0] }); part(R0, cyl(0.02, 0.02, 0.2, 10), P.iron, { pos: [0.24, 0.0, 0] }); part(R0, torus(0.04, 0.008, 6, 16, PI), P.iron, { pos: [-0.22, 0.12, 0], rot: [0, 0, PI / 2] });
  // cuerpo en forma de huevo, con remaches y ventanita al mecanismo
  blob(bd, [0, 0.3, 0], [0.13, 0.17, 0.12], CU, { taperTop: 0.15 });
  for (const y of [0.22, 0.32, 0.4]) part(bd, torus(0.128 - Math.abs(y - 0.3) * 0.25, 0.005, 6, 32), BR, { pos: [0, y, 0], rot: [PI / 2, 0, 0], scl: [1, 0.92, 1] });
  part(bd, torus(0.05, 0.008, 8, 24), BR, { pos: [0, 0.3, 0.115] }); glow(bd, disc(0.044, 20), G.warm, { pos: [0, 0.3, 0.113] });
  for (let i = 0; i < 8; i++) { const a = i / 8 * 2 * PI; part(bd, box(0.012, 0.012, 0.006), CU2, { pos: [0.026 * Math.cos(a), 0.3 + 0.026 * Math.sin(a), 0.118], rot: [0, 0, a] }); } // engranaje
  for (let i = 0; i < 6; i++) { const a = -0.8 + i * 0.32; blob(bd, [0.11 * Math.sin(a), 0.22 + 0.01 * Math.cos(i), 0.1 * Math.cos(a)], [0.03, 0.022, 0.012], PAT, { rot: [0, a, 0] }); } // pátina
  // cabeza ancha con dos lentes enormes
  blob(hd, [0, 0.5, 0.01], [0.15, 0.11, 0.12], CU, { taperTop: 0.1 });
  for (const s of [1, -1]) {
    part(hd, torus(0.06, 0.012, 8, 28), BR, { pos: [0.065 * s, 0.505, 0.105] }); part(hd, cyl(0.06, 0.06, 0.02, 24), '#2A2024', { pos: [0.065 * s, 0.505, 0.1], rot: [PI / 2, 0, 0] });
    glow(hd, disc(0.05, 24), G.warm, { pos: [0.065 * s, 0.505, 0.112] }); part(hd, torus(0.028, 0.006, 6, 18), CU2, { pos: [0.065 * s, 0.505, 0.115] }); part(hd, disc(0.018, 16), '#140E14', { pos: [0.065 * s, 0.505, 0.116] });
    spike(hd, CU2, V(0.1 * s, 0.58, 0.0), V(0.4 * s, 1, -0.2), 0.09, 0.025, { sides: 5 }); // penachos
  }
  spike(hd, '#2A2024', V(0, 0.47, 0.12), V(0, -0.6, 1), 0.05, 0.016, { sides: 4 }); // pico
  // alas de placas
  for (const s of [1, -1]) {
    const w = r.world(s > 0 ? 'wing_l' : 'wing_r');
    for (let i = 0; i < 5; i++) { const y = 0.4 - i * 0.045; part(w, box(0.03, 0.2 - i * 0.025, 0.11 - i * 0.008), i % 2 ? CU2 : CU, { pos: [(0.125 + 0.006 * i) * s, y - 0.06, -0.01 - i * 0.008], rot: [0.15, 0.1 * s, -0.12 * s] }); }
  }
  for (let i = 0; i < 3; i++) part(tl, box(0.04, 0.012, 0.12), i % 2 ? CU2 : CU, { pos: [(i - 1) * 0.04, 0.17, -0.14], rot: [-0.5, (i - 1) * 0.3, 0] });
  // garras sobre la percha
  for (const s of [1, -1]) for (const k of [-1, 0, 1]) { const m = part(bd, cone(0.008, 0.045, 5), '#2A2024', { pos: [0.05 * s + 0.015 * k, 0.13, 0.02] }); aim(m, V(0.2 * k, -0.6, 1)); }
  return r;
}

// ======================= 4. TOPO MINERO · tendero de túneles (0,85 m, muy ancho) =======================
export function topo() {
  const r = new Rig('topo', skeleton([['hips', null, [0, 0.3, 0]], ['spine', 'hips', [0, 0.45, 0]], ['head', 'spine', [0, 0.66, 0.03]], ['arm_l', 'spine', [0.2, 0.55, 0]], ['hand_l', 'arm_l', [0.3, 0.38, 0.1]],
    ['arm_r', 'spine', [-0.2, 0.55, 0]], ['hand_r', 'arm_r', [-0.3, 0.38, 0.1]], ['leg_l', 'hips', [0.1, 0.2, 0]], ['foot_l', 'leg_l', [0.11, 0.05, 0.02]], ['leg_r', 'hips', [-0.1, 0.2, 0]], ['foot_r', 'leg_r', [-0.11, 0.05, 0.02]]]));
  const H = r.world('hips'), S = r.world('spine'), hd = r.world('head');
  const FUR = '#4E4452', FUR2 = '#6A5E6E', PINK = '#E6A3AE', LEATHER = '#5A3B2A';
  blob(H, [0, 0.36, 0], [0.24, 0.22, 0.2], FUR, { noise: 0.05, freq: 4, flatBottom: 0.6 });
  blob(S, [0, 0.52, 0], [0.21, 0.18, 0.18], FUR, { noise: 0.05, freq: 4, taperTop: 0.2 });
  blob(S, [0, 0.42, 0.13], [0.15, 0.14, 0.08], FUR2, { noise: 0.04 });
  // peto de cuero con bolsillos y herramientas
  part(S, lathe([[0.2, 0.3], [0.22, 0.38], [0.215, 0.46], [0.19, 0.53]], -0.9, 1.8, 20), LEATHER, { scl: [1, 1, 0.95], double: true });
  for (const s of [1, -1]) part(S, tube([V(0.1 * s, 0.53, 0.15), V(0.12 * s, 0.62, 0.06), V(0.1 * s, 0.62, -0.1), V(0.12 * s, 0.45, -0.18)], 0.012, 16), LEATHER);
  part(S, box(0.1, 0.07, 0.03), '#3E2A1E', { pos: [0.06, 0.4, 0.2] }); part(S, cyl(0.006, 0.006, 0.1, 6), P.iron, { pos: [0.08, 0.46, 0.2] });
  // cabeza fundida con el cuerpo y hocico rosado largo
  blob(hd, [0, 0.66, 0.06], [0.14, 0.12, 0.15], FUR, { noise: 0.04 });
  const sn = part(hd, cyl(0.022, 0.05, 0.14, 14), PINK, { pos: [0, 0.63, 0.24], rot: [PI / 2 - 0.15, 0, 0] });
  for (let i = 0; i < 10; i++) { const a = i / 10 * 2 * PI; const m = part(hd, cone(0.008, 0.03, 5), PINK, { pos: [0.02 * Math.cos(a), 0.62 + 0.02 * Math.sin(a), 0.31] }); aim(m, V(Math.cos(a), Math.sin(a), 0.6)); } // nariz de estrella
  for (const s of [1, -1]) part(hd, sphere(0.009, 8, 6), '#140E14', { pos: [0.055 * s, 0.69, 0.18] });
  // casco de minero con lámpara
  part(hd, new THREE.SphereGeometry(0.14, 24, 12, 0, 2 * PI, 0, PI * 0.45), '#C9A35A', { pos: [0, 0.7, 0.04] }); part(hd, cyl(0.16, 0.16, 0.01, 24), '#C9A35A', { pos: [0, 0.7, 0.04] });
  part(hd, cyl(0.03, 0.035, 0.04, 14), P.iron, { pos: [0, 0.79, 0.15], rot: [PI / 2 - 0.3, 0, 0] }); glow(hd, disc(0.026, 14), G.hot, { pos: [0, 0.8, 0.172], rot: [-0.3, 0, 0] });
  // brazos cortos y manos-pala enormes con garras
  for (const s of [1, -1]) {
    const a = r.world(s > 0 ? 'arm_l' : 'arm_r'), h = r.world(s > 0 ? 'hand_l' : 'hand_r');
    limb(a, V(0.2 * s, 0.55, 0), V(0.3 * s, 0.4, 0.1), 0.065, 0.05, FUR, { bulge: 0.1 });
    blob(h, [0.33 * s, 0.34, 0.14], [0.09, 0.06, 0.11], PINK, { rot: [0.4, 0, 0.5 * s] });
    for (const k of [-1.5, -0.5, 0.5, 1.5]) { const m = part(h, cone(0.016, 0.07, 6), '#EDE3C8', { pos: [0.36 * s + 0.02 * k * s, 0.3 - Math.abs(k) * 0.01, 0.22 + 0.01 * k] }); aim(m, V(0.2 * s, -0.4, 1)); }
  }
  for (const s of [1, -1]) { const L = r.world(s > 0 ? 'leg_l' : 'leg_r'), F = r.world(s > 0 ? 'foot_l' : 'foot_r'); limb(L, V(0.1 * s, 0.22, 0), V(0.11 * s, 0.07, 0.02), 0.06, 0.05, FUR); blob(F, [0.11 * s, 0.035, 0.07], [0.07, 0.035, 0.1], PINK, { flatBottom: 0.3 }); }
  // pico a la espalda
  const pk = new THREE.Group(); pk.position.set(0.05, 0.55, -0.2); pk.rotation.z = 0.7; S.add(pk);
  part(pk, cyl(0.012, 0.012, 0.5, 8), '#8A6A3A'); part(pk, tube([V(-0.14, 0.24, 0), V(0, 0.27, 0), V(0.14, 0.24, 0)], 0.016, 12, false, 6), P.iron);
  return r;
}

// ======================= 5. CARACOL MERCADER · tienda ambulante (1,5 m con la concha) =======================
export function caracol() {
  const r = new Rig('caracol', skeleton([['root', null, [0, 0, 0]], ['foot', 'root', [0, 0.06, 0]], ['neck', 'foot', [0, 0.3, 0.42]], ['head', 'neck', [0, 0.58, 0.58]],
    ['stalk_l', 'head', [0.05, 0.66, 0.6]], ['stalk_r', 'head', [-0.05, 0.66, 0.6]], ['shell', 'foot', [0, 0.55, -0.1]]]));
  const ft = r.world('foot'), nk = r.world('neck'), hd = r.world('head'), sh = r.world('shell');
  const SLUG = '#B9AECB', SLUG2 = '#968AAB', CREAM = '#EDE3C8';
  // pie largo y bajo
  blob(ft, [0, 0.07, 0.05], [0.2, 0.08, 0.62], SLUG, { flatBottom: 0.2, noise: 0.03 });
  blob(ft, [0, 0.04, 0.05], [0.24, 0.035, 0.66], SLUG2, { flatBottom: 0.1 });
  // cuello que se eleva y cabeza
  part(nk, tube([V(0, 0.12, 0.38), V(0, 0.3, 0.46), V(0, 0.48, 0.53), V(0, 0.56, 0.58)], 0.085, 20, false, 14), SLUG);
  blob(hd, [0, 0.6, 0.6], [0.1, 0.09, 0.11], SLUG);
  part(hd, torus(0.028, 0.004, 6, 14, PI), '#5A3A5E', { pos: [0, 0.57, 0.705], rot: [0, 0, PI] });
  for (const s of [1, -1]) part(hd, sphere(0.014, 8, 6), '#D69AB4', { pos: [0.05 * s, 0.585, 0.69], scl: [1.3, 0.7, 0.5] });
  // sombrero de copa pequeño (es un comerciante con estilo)
  part(hd, cyl(0.05, 0.055, 0.08, 18), '#2E2838', { pos: [0, 0.71, 0.57], rot: [-0.25, 0, 0.2] }); part(hd, cyl(0.085, 0.085, 0.008, 20), '#2E2838', { pos: [0, 0.675, 0.58], rot: [-0.25, 0, 0.2] });
  part(hd, torus(0.052, 0.006, 6, 18), P.gold, { pos: [0, 0.685, 0.578], rot: [PI / 2 - 0.25, 0, 0.2] });
  // ojos en pedúnculos con luz
  for (const s of [1, -1]) {
    const st = r.world(s > 0 ? 'stalk_l' : 'stalk_r');
    part(st, tube([V(0.04 * s, 0.64, 0.62), V(0.07 * s, 0.75, 0.66), V(0.1 * s, 0.84, 0.66)], 0.013, 12, false, 8), SLUG);
    eye(st, V(0.1 * s, 0.86, 0.67), V(0.2 * s, 0, 1), 0.03, null, { glowIris: G.warm, pupil: 0.35 });
  }
  // concha en espiral (dos caras con la espiral en dorado) + tienda encima
  blob(sh, [0, 0.5, -0.12], [0.3, 0.42, 0.42], CREAM, { noise: 0.02 });
  for (const s of [1, -1]) {
    const pts = []; for (let i = 0; i <= 80; i++) { const th = i / 80 * 4.5 * PI; const rr = 0.4 * Math.exp(-0.2 * th); const yy = rr * Math.cos(th), zz = rr * Math.sin(th); const k = Math.max(0, 1 - (yy * yy + zz * zz) / (0.42 * 0.42)); pts.push(V(s * (0.3 * Math.sqrt(k) + 0.006), 0.5 + yy * 1.0, -0.12 + zz)); }
    part(sh, tube(pts, 0.012, 160, false, 6), P.goldDark);
  }
  for (const y of [0.22, 0.8]) part(sh, torus(0.25, 0.006, 6, 30, PI), P.gold, { pos: [0, y, -0.12], rot: [0, PI / 2, 0], scl: [1, 1, 1] });
  // puestito en la concha: tejadillo, estante con frascos y guirnalda de bombillas
  part(sh, box(0.42, 0.02, 0.36), '#8A6A3A', { pos: [0, 0.93, -0.12] });
  part(sh, cone(0.32, 0.18, 4), '#8A3A3A', { pos: [0, 1.18, -0.12], rot: [0, PI / 4, 0] });
  for (const [x, z] of [[0.18, 0.05], [-0.18, 0.05], [0.18, -0.29], [-0.18, -0.29]]) part(sh, cyl(0.008, 0.008, 0.25, 6), '#5A3B2A', { pos: [x, 1.06, z] });
  const jars = ['#7FF7EA', '#C98BFF', '#FFD27A', '#9FD3C7', '#E28BB0'];
  jars.forEach((c, i) => { part(sh, cyl(0.025, 0.025, 0.06, 10), '#CFE6E8', { pos: [-0.14 + i * 0.07, 0.97, 0.0] }); glow(sh, cyl(0.02, 0.02, 0.035, 10), c.slice(1), { pos: [-0.14 + i * 0.07, 0.96, 0.0] }); part(sh, cyl(0.016, 0.016, 0.012, 8), '#5A3B2A', { pos: [-0.14 + i * 0.07, 1.006, 0.0] }); });
  const garland = []; for (let i = 0; i <= 12; i++) { const t = i / 12; garland.push(V(-0.2 + 0.4 * t, 1.08 - 0.06 * Math.sin(PI * t), 0.06)); }
  part(sh, tube(garland, 0.003, 30, false, 4), P.iron); garland.forEach((p, i) => { if (i % 2) glow(sh, sphere(0.012, 8, 6), G.warm, { pos: [p.x, p.y - 0.015, p.z] }); });
  lantern(sh, V(0.22, 1.02, 0.08), 1.2);
  return r;
}

// ======================= 6. LARGUERO · gigante farolero (3,2 m) =======================
// Usa los nombres de hueso humanos (pelvis…toe_r) con proporciones de gigante: las animaciones humanoides se pueden reaprovechar.
export function larguero() {
  const B = [['pelvis', null, [0, 1.72, 0]], ['spine', 'pelvis', [0, 1.95, 0]], ['chest', 'spine', [0, 2.3, 0.04]], ['neck', 'chest', [0, 2.62, 0.12]], ['head', 'neck', [0, 2.78, 0.2]]];
  for (const [s, x] of [['l', 1], ['r', -1]]) B.push(['shoulder_' + s, 'chest', [0.3 * x, 2.52, 0.06]], ['elbow_' + s, 'shoulder_' + s, [0.42 * x, 1.95, 0.08]], ['wrist_' + s, 'elbow_' + s, [0.5 * x, 1.38, 0.12]],
    ['hip_' + s, 'pelvis', [0.14 * x, 1.68, 0]], ['knee_' + s, 'hip_' + s, [0.16 * x, 0.9, 0.04]], ['ankle_' + s, 'knee_' + s, [0.17 * x, 0.12, 0]], ['toe_' + s, 'ankle_' + s, [0.17 * x, 0.03, 0.16]]);
  const r = new Rig('larguero', skeleton(B));
  const IRON = '#3A3644', COAT = '#C9C3BA', COAT2 = '#A9A39A';
  const pel = r.world('pelvis'), sp = r.world('spine'), ch = r.world('chest'), nk = r.world('neck'), hd = r.world('head');
  // cuerpo de poste: delgado, encorvado
  limb(sp, V(0, 1.72, 0), V(0, 2.3, 0.04), 0.13, 0.17, IRON);
  blob(ch, [0, 2.42, 0.05], [0.26, 0.2, 0.17], IRON, { taperTop: 0.15 });
  limb(nk, V(0, 2.55, 0.1), V(0, 2.75, 0.2), 0.06, 0.05, IRON);
  // abrigo largo desgastado hasta las rodillas
  const prof = [[0.2, 2.55], [0.29, 2.45], [0.27, 2.2], [0.2, 1.9], [0.24, 1.6], [0.32, 1.2], [0.38, 0.95]];
  part(sp, jag(lathe(prof, 0.5, 2 * PI - 1.0, 40), prof.length, 40, 0.12, 2), COAT, { pos: [0, 0, 0.03], scl: [1, 1, 0.7], double: true });
  for (const s of [1, -1]) part(sp, tube(prof.map(([x, y]) => V(x * 1.01 * Math.sin(0.5 * s), y, x * 1.01 * Math.cos(0.5) * 0.7 + 0.03)), 0.012, 30), P.gold);
  part(sp, torus(0.2, 0.025, 8, 28), '#3E2A1E', { pos: [0, 1.95, 0.02], rot: [PI / 2, 0, 0], scl: [1, 0.72, 1] });
  // cabeza: farola con dos «ojos» de sombra dentro del cristal
  part(hd, cyl(0.1, 0.12, 0.06, 6), IRON, { pos: [0, 2.78, 0.22] });
  glow(hd, cyl(0.15, 0.12, 0.3, 6), G.warm, { pos: [0, 2.96, 0.22], rot: [0, PI / 6, 0] });
  for (let i = 0; i < 6; i++) { const a = i * PI / 3; part(hd, box(0.02, 0.32, 0.02), IRON, { pos: [0.15 * Math.cos(a), 2.96, 0.22 + 0.15 * Math.sin(a)], rot: [0, -a, 0] }); }
  for (const s of [1, -1]) blob(hd, [0.05 * s, 2.98, 0.37], [0.03, 0.045, 0.01], '#2A1E26', { rot: [0, 0, 0.2 * s] });
  part(hd, cone(0.22, 0.14, 6), IRON, { pos: [0, 3.11, 0.22], rot: [0, PI / 6, 0] }); part(hd, sphere(0.035, 10, 8), P.goldDark, { pos: [0, 3.27, 0.22] });
  part(hd, cyl(0.24, 0.24, 0.015, 6), P.goldDark, { pos: [0, 3.11, 0.22], rot: [0, PI / 6, 0] });
  // brazos larguísimos y finos, dedos largos
  for (const [s, x] of [['l', 1], ['r', -1]]) {
    const S = r.world('shoulder_' + s), E = r.world('elbow_' + s), W = r.world('wrist_' + s);
    limb(S, V(0.3 * x, 2.52, 0.06), V(0.42 * x, 1.95, 0.08), 0.07, 0.05, COAT); part(S, sphere(0.1, 14, 10), COAT, { pos: [0.3 * x, 2.52, 0.06] });
    limb(E, V(0.42 * x, 1.95, 0.08), V(0.5 * x, 1.38, 0.12), 0.05, 0.035, IRON);
    part(E, torus(0.065, 0.012, 8, 20), P.gold, { pos: [0.48 * x, 1.48, 0.11], rot: [PI / 2, 0, 0] });
    blob(W, [0.51 * x, 1.32, 0.13], [0.04, 0.06, 0.03], IRON);
    for (const k of [-1.5, -0.5, 0.5, 1.5]) limb(W, V(0.51 * x + 0.012 * k, 1.28, 0.13 + 0.012 * k), V(0.52 * x + 0.02 * k, 1.15, 0.15 + 0.02 * k), 0.008, 0.005, IRON);
  }
  for (const [s, x] of [['l', 1], ['r', -1]]) {
    const Hh = r.world('hip_' + s), K = r.world('knee_' + s), A = r.world('ankle_' + s);
    limb(Hh, V(0.14 * x, 1.68, 0), V(0.16 * x, 0.9, 0.04), 0.07, 0.05, IRON); part(K, sphere(0.06, 12, 10), P.goldDark, { pos: [0.16 * x, 0.9, 0.04] });
    limb(K, V(0.16 * x, 0.9, 0.04), V(0.17 * x, 0.12, 0), 0.05, 0.04, IRON);
    part(A, cyl(0.08, 0.1, 0.06, 6), IRON, { pos: [0.17 * x, 0.05, 0.03] }); blob(A, [0.17 * x, 0.03, 0.1], [0.07, 0.035, 0.12], IRON, { flatBottom: 0.3 });
  }
  // pértiga de encender en la mano derecha (vertical, más alta que él)
  const w = r.world('wrist_r'); part(w, cyl(0.015, 0.015, 3.4, 8), '#5A3B2A', { pos: [-0.53, 1.6, 0.16] }); part(w, cyl(0.03, 0.02, 0.08, 8), P.gold, { pos: [-0.53, 3.32, 0.16] }); glow(w, cone(0.02, 0.06, 8), G.warm, { pos: [-0.53, 3.36, 0.16] });
  return r;
}

// ======================= 7. BALLENA DE NIEBLA · criatura del cielo (7 m) =======================
export function ballena() {
  const r = new Rig('ballena', skeleton([['body', null, [0, 4.0, 0]], ['head', 'body', [0, 4.05, 2.0]], ['jaw', 'head', [0, 3.8, 2.4]], ['fin_l', 'body', [1.0, 3.75, 1.0]], ['fin_r', 'body', [-1.0, 3.75, 1.0]], ['tail1', 'body', [0, 4.05, -1.8]], ['tail2', 'tail1', [0, 4.15, -3.2]]]));
  const bd = r.world('body'), hd = r.world('head'), jw = r.world('jaw'), t1 = r.world('tail1'), t2 = r.world('tail2');
  const SKIN = '#7E7896', SKIN2 = '#5E5878', BELLY = '#CFC9DC';
  blob(bd, [0, 4.0, 0.2], [1.15, 1.0, 2.4], SKIN, { noise: 0.02, freq: 2 });
  blob(bd, [0, 3.62, 0.5], [0.95, 0.55, 2.1], BELLY, { noise: 0.01 });
  for (let i = -3; i <= 3; i++) part(bd, tube([V(i * 0.12, 3.12, 1.8), V(i * 0.14, 3.1, 0.6), V(i * 0.13, 3.25, -0.8)], 0.02, 20, false, 5), '#B1AAC2'); // pliegues ventrales
  const rnd = mulberry(11);
  for (let i = 0; i < 22; i++) { const z = -1.4 + rnd() * 3.2, a = 0.3 + rnd() * 1.2, s = rnd() > 0.5 ? 1 : -1; blob(bd, [s * 1.05 * Math.sin(a) * Math.sqrt(Math.max(0.1, 1 - (z / 2.6) ** 2)), 4.0 + 0.95 * Math.cos(a) * 0.9, z], [0.05 + 0.05 * rnd(), 0.04, 0.05 + 0.05 * rnd()], '#9C96AE', { noise: 0.2, w: 10, h: 8 }); } // percebes
  // cabeza y boca
  blob(hd, [0, 4.0, 2.25], [0.95, 0.85, 0.9], SKIN, { noise: 0.02 });
  blob(jw, [0, 3.6, 2.35], [0.85, 0.35, 0.85], BELLY);
  part(jw, tube([V(-0.8, 3.75, 1.9), V(-0.6, 3.72, 2.7), V(0, 3.7, 3.08), V(0.6, 3.72, 2.7), V(0.8, 3.75, 1.9)], 0.025, 30, false, 6), '#3A3446');
  for (const s of [1, -1]) eye(hd, V(0.8 * s, 3.95, 2.35), V(1 * s, 0, 0.35), 0.11, null, { glowIris: G.voidBright, pupil: 0.3, white: '#2A2533' });
  // farolillos colgando de cadenas bajo el vientre: lleva la luz por la ciudad
  for (const [x, z, L] of [[0.4, 1.4, 0.7], [-0.45, 0.8, 0.9], [0.2, 0.0, 1.1], [-0.3, -0.7, 0.8], [0.35, -1.3, 0.6]]) {
    const y0 = 3.2 + Math.abs(z) * 0.05; const pts = []; for (let k = 0; k <= 6; k++) pts.push(V(x, y0 - L * k / 6, z));
    part(bd, tube(pts, 0.008, 6, false, 4), P.iron); lantern(bd, V(x, y0 - L - 0.02, z), 2.2);
  }
  // aletas y cola
  for (const s of [1, -1]) {
    const f = r.world(s > 0 ? 'fin_l' : 'fin_r');
    const sh = new THREE.Shape(); sh.moveTo(0, 0.18); sh.quadraticCurveTo(0.8, 0.12, 1.5, -0.35); sh.quadraticCurveTo(0.9, -0.3, 0, -0.22); sh.closePath();
    const g = new THREE.ExtrudeGeometry(sh, { depth: 0.07, bevelEnabled: true, bevelThickness: 0.04, bevelSize: 0.04, bevelSegments: 2 }); g.translate(0, 0, -0.035);
    if (s < 0) g.scale(-1, 1, 1);
    const fin = part(f, g, SKIN2, { pos: [0.9 * s, 3.55, 1.1] });
    fin.rotation.set(PI / 2, 0, 0); fin.rotateZ(-0.35 * s); fin.rotateY(0.55 * s); // hacia atrás y caída hacia abajo
  }
  blob(t1, [0, 4.08, -2.4], [0.7, 0.62, 1.2], SKIN, { noise: 0.01 });
  blob(t2, [0, 4.15, -3.35], [0.32, 0.28, 0.6], SKIN);
  for (const s of [1, -1]) {
    const sh = new THREE.Shape(); sh.moveTo(0, 0.25); sh.quadraticCurveTo(0.9, 0.2, 1.6, -0.55); sh.quadraticCurveTo(1.0, -0.2, 0.15, -0.35); sh.closePath();
    const g = new THREE.ExtrudeGeometry(sh, { depth: 0.07, bevelEnabled: true, bevelThickness: 0.04, bevelSize: 0.04, bevelSegments: 2 }); g.translate(0, 0, -0.035);
    if (s < 0) g.scale(-1, 1, 1);
    // forma en el plano XY → horizontal (plano XZ), hacia atrás (-Z)
    part(t2, g, SKIN2, { pos: [0, 4.17, -3.75], rot: [PI / 2 - 0.3, 0, 0] });
  }
  return r;
}

// ======================= 8. CANGREJO DE CRISTAL · enemigo del canal (0,9 m de ancho) =======================
export function cangrejo() {
  const B = [['root', null, [0, 0, 0]], ['body', 'root', [0, 0.26, 0]], ['eye_l', 'body', [0.06, 0.36, 0.18]], ['eye_r', 'body', [-0.06, 0.36, 0.18]], ['claw_l', 'body', [0.22, 0.24, 0.16]], ['claw_r', 'body', [-0.22, 0.24, 0.16]]];
  for (let i = 0; i < 3; i++) for (const [s, x] of [['l', 1], ['r', -1]]) B.push([`leg_${s}${i + 1}`, 'body', [0.2 * x, 0.24, 0.06 - i * 0.1]]);
  const r = new Rig('cangrejo', skeleton(B));
  const bd = r.world('body'); const SHELL = '#2E2440', SHELL2 = '#3E3256';
  blob(bd, [0, 0.27, 0], [0.26, 0.11, 0.2], SHELL, { noise: 0.04, freq: 4 });
  blob(bd, [0, 0.21, 0.02], [0.22, 0.06, 0.17], SHELL2);
  // cristales de sombra sobre el caparazón y vetas violeta
  const rnd = mulberry(5);
  for (let i = 0; i < 9; i++) { const a = rnd() * 2 * PI, d = 0.05 + rnd() * 0.12; spike(bd, P.crystal, V(d * Math.cos(a), 0.34 - d * 0.25, d * Math.sin(a) * 0.8), V(Math.cos(a) * 0.4, 1, Math.sin(a) * 0.3), 0.08 + rnd() * 0.08, 0.025, { sides: 5 }); }
  for (const s of [1, -1]) crack(bd, [V(0.05 * s, 0.37, 0.12), V(0.12 * s, 0.35, 0.05), V(0.18 * s, 0.32, -0.04), V(0.22 * s, 0.28, -0.1)], 0.004, G.void);
  // ojos en pedúnculos
  for (const s of [1, -1]) { const e = r.world(s > 0 ? 'eye_l' : 'eye_r'); limb(e, V(0.06 * s, 0.32, 0.17), V(0.07 * s, 0.42, 0.2), 0.012, 0.01, SHELL2); glow(e, sphere(0.022, 10, 8), G.voidBright, { pos: [0.07 * s, 0.44, 0.205] }); }
  // pinzas: brazo + pinza grande de cristal
  for (const s of [1, -1]) {
    const c = r.world(s > 0 ? 'claw_l' : 'claw_r');
    limb(c, V(0.22 * s, 0.24, 0.16), V(0.32 * s, 0.26, 0.3), 0.03, 0.03, SHELL2);
    blob(c, [0.36 * s, 0.27, 0.38], [0.075, 0.06, 0.1], SHELL, { noise: 0.05 });
    const up = spike(c, P.crystal, V(0.37 * s, 0.3, 0.44), V(0.05 * s, 0.25, 1), 0.13, 0.03, { sides: 5 });
    const lo = spike(c, P.crystal, V(0.36 * s, 0.24, 0.44), V(0.0, -0.1, 1), 0.1, 0.025, { sides: 5 });
    glow(c, sphere(0.015, 8, 6), G.voidBright, { pos: [0.36 * s, 0.27, 0.47] });
  }
  // seis patas articuladas (un hueso por pata)
  for (let i = 0; i < 3; i++) for (const [s, x] of [['l', 1], ['r', -1]]) {
    const L = r.world(`leg_${s}${i + 1}`); const z = 0.06 - i * 0.1;
    const knee = V(0.32 * x, 0.3, z * 1.15), foot = V(0.38 * x, 0.0, z * 1.35);
    limb(L, V(0.2 * x, 0.22, z), knee, 0.032, 0.026, SHELL2, { bulge: 0.12 }); limb(L, knee, foot, 0.026, 0.01, SHELL); spike(L, P.crystal, knee.clone(), V(0.3 * x, 1, 0), 0.04, 0.014, { sides: 4 });
  }
  return r;
}

export const CREATURES = { mechita, hollinete, buho, topo, caracol, larguero, ballena, cangrejo };
