// Animaciones de Vela (y de cualquier personaje con el rig de model.js).
// Cada clip es una función t → pose. Pose = rotaciones Euler XYZ locales por hueso (radianes) + desplazamiento de cadera.
// Ciclos (correr, caminar) procedurales; acciones (saltar, combo, voltereta) por fotogramas clave con interpolación cúbica.
import * as THREE from 'three';

const PI = Math.PI, S = Math.sin, C = Math.cos;
const HIPS_Y = 0.92;

// ---- mapeo de claves cortas a los grupos del rig ----
export function boneMap(rig) {
  const b = rig.bones;
  return {
    hips: b.hips, spine: b.spine, neck: b.neck, head: b.head,
    uaL: b.armL.upper, laL: b.armL.lower, hL: b.armL.hand, uaR: b.armR.upper, laR: b.armR.lower, hR: b.armR.hand,
    ulL: b.legL.upper, llL: b.legL.lower, fL: b.legL.foot, ulR: b.legR.upper, llR: b.legR.lower, fR: b.legR.foot,
    tailB: b.tailB, tailF: b.tailF, ahoge: b.ahoge, lantern: b.lantern,
  };
}
// pose neutra (pose A) de cada hueso
const REST = { uaL: [0, 0, 0.78], uaR: [0, 0, -0.78], ulL: [0, 0, 0.035], ulR: [0, 0, -0.035] };
const rest = (k) => REST[k] || [0, 0, 0];

// ---- espada: orientación respecto a la mano según la acción ----
const SWORD = {
  atras: new THREE.Quaternion().setFromEuler(new THREE.Euler(-PI / 2 - 0.35, 0, 0)), // hoja hacia atrás (al correr)
  frente: new THREE.Quaternion().setFromEuler(new THREE.Euler(PI / 2 - 0.1, 0, 0)),  // hoja al frente (combate)
};

// ======================= CICLOS PROCEDURALES =======================
function leg(p, amp, kneeAmp, footAmp) { // p: fase de la pierna
  const thigh = -0.15 - amp * S(p);
  const knee = 0.22 + kneeAmp * Math.pow(0.5 + 0.5 * C(p + 0.6), 2);
  const foot = -footAmp * S(p) - 0.15 * Math.pow(0.5 + 0.5 * C(p + 0.6), 2);
  return [thigh, knee, foot];
}
function correr(t) {
  const T = 0.62, ph = 2 * PI * t / T, pL = ph, pR = ph + PI;
  const [tL, kL, fL] = leg(pL, 0.72, 1.55, 0.35), [tR, kR, fR] = leg(pR, 0.72, 1.55, 0.35);
  return {
    hipsPos: [0, -0.045 + 0.04 * C(2 * ph), 0], hips: [0.05, 0.16 * S(ph), 0.03 * S(ph)],
    spine: [0.28, -0.24 * S(ph), 0], neck: [-0.08, 0, 0], head: [-0.16, 0.1 * S(ph), 0],
    ulL: [tL, 0, 0.04], llL: [kL, 0, 0], fL: [fL, 0, 0], ulR: [tR, 0, -0.04], llR: [kR, 0, 0], fR: [fR, 0, 0],
    // brazos contrarios a las piernas, codos muy doblados
    uaL: [0.1 + 0.75 * S(pR), 0, 0.18], laL: [-1.35 - 0.25 * S(pR), 0, 0], hL: [0, 0, 0.2],
    uaR: [0.1 + 0.75 * S(pL), 0, -0.18], laR: [-1.2 - 0.25 * S(pL), 0, 0], hR: [0.2, 0, 0],
    tailB: [0.85 + 0.12 * S(2 * ph), 0.15 * S(ph), 0.12 * S(2 * ph + 1)], tailF: [0.55 + 0.1 * S(2 * ph), 0, 0.1 * S(ph)],
    ahoge: [0.3 * S(2 * ph + 0.5), 0, 0.15 * S(ph)], lantern: [0.5 + 0.15 * S(2 * ph), 0, 0.25 * S(ph)],
    sword: 'espalda',
  };
}
function caminar(t) {
  const T = 1.05, ph = 2 * PI * t / T, pL = ph, pR = ph + PI;
  const [tL, kL, fL] = leg(pL, 0.38, 0.55, 0.22), [tR, kR, fR] = leg(pR, 0.38, 0.55, 0.22);
  return {
    hipsPos: [0, -0.012 + 0.014 * C(2 * ph), 0], hips: [0, 0.08 * S(ph), 0.05 * S(ph)],
    spine: [0.04, -0.1 * S(ph), -0.03 * S(ph)], head: [0.02, 0.05 * S(ph) + 0.15, 0.04],
    ulL: [tL + 0.1, 0, 0.035], llL: [kL - 0.05, 0, 0], fL: [fL, 0, 0], ulR: [tR + 0.1, 0, -0.035], llR: [kR - 0.05, 0, 0], fR: [fR, 0, 0],
    uaL: [0.35 * S(pR), 0, 0.12], laL: [-0.25, 0, 0], uaR: [-0.35, 0.35, -0.42], laR: [-2.05, 0, 0], hR: [0.1, 0, 0], // espada al hombro
    tailB: [0.35 + 0.08 * S(2 * ph), 0.1 * S(ph), 0.08 * S(ph)], tailF: [0.12, 0, 0.05 * S(ph)],
    ahoge: [0.15 * S(2 * ph), 0, 0.08 * S(ph)], lantern: [0.15 * S(2 * ph), 0, 0.2 * S(ph)],
    sword: 'hombro',
  };
}

// ======================= FOTOGRAMAS CLAVE =======================
// keys: [tiempo, pose parcial]; lo que falta se hereda de la clave anterior (o del reposo).
function keyed(keys, duration, loop) {
  const names = new Set(); keys.forEach(([, p]) => Object.keys(p).forEach((k) => names.add(k)));
  const filled = []; let prev = {};
  for (const [t, p] of keys) { const f = { ...prev, ...p }; filled.push([t, f]); prev = f; }
  // las claves que aún falten al principio: valor de reposo
  for (const [, f] of filled) for (const k of names) if (!(k in f)) f[k] = k === 'hipsPos' ? [0, 0, 0] : k === 'sword' ? 'frente' : rest(k);
  return (t) => {
    t = loop ? ((t % duration) + duration) % duration : Math.min(t, duration);
    let i = 0; while (i < filled.length - 1 && filled[i + 1][0] <= t) i++;
    const a = filled[i], b = filled[Math.min(i + 1, filled.length - 1)];
    const u = b[0] > a[0] ? (t - a[0]) / (b[0] - a[0]) : 0;
    const s = u * u * (3 - 2 * u); // suavizado (ease in-out)
    const out = {};
    for (const k of names) {
      if (k === 'sword') { out[k] = u < 0.5 ? a[1][k] : b[1][k]; continue; }
      const A = a[1][k], B = b[1][k]; out[k] = A.map((v, j) => v + (B[j] - v) * s);
    }
    return out;
  };
}

// --- reposo de combate (guardia) ---
const GUARD = { hipsPos: [0, -0.06, 0], hips: [0, 0.3, 0], spine: [0.12, -0.25, 0], head: [-0.05, -0.15, 0],
  ulL: [-0.45, 0.2, 0.18], llL: [0.6, 0, 0], fL: [-0.1, 0, 0], ulR: [0.25, 0, -0.18], llR: [0.45, 0, 0], fR: [-0.2, 0, 0],
  uaR: [-1.05, 0, -0.35], laR: [-0.9, 0, 0], hR: [0.3, 0, 0], uaL: [-0.4, 0, 0.55], laL: [-1.2, 0, 0],
  tailB: [0.3, 0, 0], tailF: [0.1, 0, 0], ahoge: [0, 0, 0], lantern: [0, 0, 0], sword: 'frente' };

const saltar = keyed([
  [0.0, { ...GUARD, hipsPos: [0, -0.01, 0], hips: [0, 0, 0], spine: [0.05, 0, 0], ulL: [-0.1, 0, 0.04], llL: [0.2, 0, 0], ulR: [-0.1, 0, -0.04], llR: [0.2, 0, 0], uaL: [0.1, 0, 0.25], laL: [-0.3, 0, 0], uaR: [0.1, 0, -0.25], laR: [-0.6, 0, 0], fL: [0, 0, 0], fR: [0, 0, 0], head: [0, 0, 0], sword: 'espalda' }],
  [0.28, { hipsPos: [0, -0.2, 0], spine: [0.45, 0, 0], head: [-0.3, 0, 0], ulL: [-1.05, 0, 0.08], llL: [1.75, 0, 0], fL: [-0.65, 0, 0], ulR: [-1.05, 0, -0.08], llR: [1.75, 0, 0], fR: [-0.65, 0, 0], uaL: [0.9, 0, 0.3], laL: [-0.4, 0, 0], uaR: [0.9, 0, -0.3], laR: [-0.5, 0, 0], tailB: [0.1, 0, 0] }],
  [0.4, { hipsPos: [0, 0.18, 0], spine: [-0.1, 0, 0], head: [0.05, 0, 0], ulL: [0.1, 0, 0.04], llL: [0.1, 0, 0], fL: [0.5, 0, 0], ulR: [0.15, 0, -0.04], llR: [0.1, 0, 0], fR: [0.5, 0, 0], uaL: [-2.6, 0, 0.35], laL: [-0.2, 0, 0], uaR: [-2.4, 0, -0.35], laR: [-0.4, 0, 0], tailB: [-0.3, 0, 0], tailF: [-0.2, 0, 0], ahoge: [-0.4, 0, 0] }],
  [0.62, { hipsPos: [0, 0.5, 0], spine: [0.25, 0, 0], ulL: [-1.25, 0, 0.1], llL: [1.9, 0, 0], fL: [0.2, 0, 0], ulR: [-0.9, 0, -0.1], llR: [1.5, 0, 0], fR: [0.2, 0, 0], uaL: [-1.2, 0, 0.9], laL: [-0.9, 0, 0], uaR: [-1.1, 0, -0.9], laR: [-1.0, 0, 0], tailB: [0.4, 0, 0.2], tailF: [0.3, 0, 0], ahoge: [0.5, 0, 0] }],
  [0.84, { hipsPos: [0, 0.22, 0], spine: [0.1, 0, 0], ulL: [-0.4, 0, 0.06], llL: [0.5, 0, 0], fL: [0.3, 0, 0], ulR: [-0.3, 0, -0.06], llR: [0.4, 0, 0], fR: [0.3, 0, 0], uaL: [-0.4, 0, 1.1], laL: [-0.4, 0, 0], uaR: [-0.4, 0, -1.1], laR: [-0.5, 0, 0], tailB: [1.0, 0, 0], tailF: [0.6, 0, 0], ahoge: [0.9, 0, 0] }],
  [0.98, { hipsPos: [0, -0.22, 0], spine: [0.5, 0, 0], head: [-0.25, 0, 0], ulL: [-1.1, 0, 0.12], llL: [1.85, 0, 0], fL: [-0.7, 0, 0], ulR: [-1.0, 0, -0.12], llR: [1.8, 0, 0], fR: [-0.7, 0, 0], uaL: [0.3, 0, 0.8], laL: [-0.5, 0, 0], uaR: [0.3, 0, -0.8], laR: [-0.6, 0, 0], tailB: [1.1, 0, 0], ahoge: [-0.5, 0, 0] }],
  [1.3, { hipsPos: [0, -0.01, 0], spine: [0.05, 0, 0], head: [0, 0, 0], ulL: [-0.1, 0, 0.04], llL: [0.2, 0, 0], fL: [0, 0, 0], ulR: [-0.1, 0, -0.04], llR: [0.2, 0, 0], fR: [0, 0, 0], uaL: [0.1, 0, 0.25], laL: [-0.3, 0, 0], uaR: [0.1, 0, -0.25], laR: [-0.6, 0, 0], tailB: [0.3, 0, 0], tailF: [0.1, 0, 0], ahoge: [0, 0, 0] }],
  [1.6, {}],
], 1.6, true);

// combo de tres golpes: tajo horizontal, revés y golpe descendente con salto
const combo = keyed([
  [0.0, GUARD],
  // golpe 1: preparación a la derecha → tajo hacia la izquierda
  [0.16, { hips: [0, -0.35, 0], spine: [0.1, -0.75, 0.05], head: [-0.05, 0.4, 0], uaR: [-1.25, 0.2, -1.35], laR: [-0.75, 0, 0], hR: [0.2, -1.45, -0.3], uaL: [-0.8, 0, 0.3], laL: [-1.4, 0, 0] }],
  [0.3, { hips: [0, 0.4, 0], spine: [0.18, 0.75, -0.05], head: [-0.05, -0.45, 0], uaR: [-1.4, -0.3, 0.35], laR: [-0.25, 0, 0], hR: [-0.1, -1.45, 0.3], uaL: [-0.3, 0, 1.0], laL: [-0.6, 0, 0], ulL: [-0.7, 0.2, 0.22], llL: [0.9, 0, 0] }],
  [0.46, {}],
  // golpe 2: revés hacia la derecha con paso
  [0.6, { hips: [0, -0.45, 0], spine: [0.2, -0.8, 0.05], head: [0, 0.45, 0], uaR: [-1.35, 0.4, -1.5], laR: [-0.2, 0, 0], hR: [0, 1.45, -0.3], uaL: [-0.6, 0, 0.4], laL: [-1.5, 0, 0], ulL: [0.2, 0, 0.2], llL: [0.4, 0, 0], ulR: [-0.65, -0.2, -0.2], llR: [0.85, 0, 0] }],
  [0.74, {}],
  // golpe 3: espada por encima de la cabeza → golpe al suelo
  [0.94, { hipsPos: [0, 0.12, 0], hips: [0, 0, 0], spine: [-0.25, 0, 0], head: [0.15, 0, 0], uaR: [-2.85, 0, -0.35], laR: [-1.1, 0, 0], hR: [0.4, 0, 0], uaL: [-2.6, 0, 0.35], laL: [-1.2, 0, 0], ulL: [-0.35, 0, 0.12], llL: [0.6, 0, 0], ulR: [0.1, 0, -0.12], llR: [0.5, 0, 0], tailB: [-0.2, 0, 0], ahoge: [-0.5, 0, 0] }],
  [1.1, { hipsPos: [0, -0.26, 0], spine: [0.6, 0, 0], head: [-0.4, 0, 0], uaR: [-0.95, 0, -0.15], laR: [-0.15, 0, 0], hR: [-0.3, 0, 0], uaL: [-0.8, 0, 0.15], laL: [-0.3, 0, 0], ulL: [-1.15, 0, 0.18], llL: [1.4, 0, 0], fL: [-0.25, 0, 0], ulR: [0.55, 0, -0.18], llR: [1.0, 0, 0], fR: [-0.5, 0, 0], tailB: [1.2, 0, 0], tailF: [0.8, 0, 0], ahoge: [0.8, 0, 0] }],
  [1.4, {}],
  [1.85, GUARD],
  [2.1, GUARD],
], 2.1, true);

// voltereta hacia delante (esquiva): la cadera gira una vuelta completa en X
const voltereta = keyed([
  [0.0, { ...GUARD, sword: 'espalda' }],
  [0.14, { hipsPos: [0, -0.25, 0.1], hips: [0.4, 0, 0], spine: [0.6, 0, 0], head: [0.4, 0, 0], ulL: [-1.4, 0, 0.1], llL: [1.9, 0, 0], ulR: [-1.0, 0, -0.1], llR: [1.6, 0, 0], uaL: [-1.4, 0, 0.3], laL: [-0.6, 0, 0], uaR: [-1.3, 0, -0.3], laR: [-0.8, 0, 0], sword: 'espalda' }],
  [0.32, { hipsPos: [0, -0.45, 0.35], hips: [PI * 0.65, 0, 0], spine: [0.8, 0, 0], head: [0.6, 0, 0], ulL: [-2.0, 0, 0.08], llL: [2.3, 0, 0], ulR: [-2.0, 0, -0.08], llR: [2.3, 0, 0], uaL: [-1.0, 0, 0.2], laL: [-1.8, 0, 0], uaR: [-1.0, 0, -0.2], laR: [-1.8, 0, 0], tailB: [1.5, 0, 0], tailF: [1.2, 0, 0] }],
  [0.5, { hipsPos: [0, -0.42, 0.65], hips: [PI * 1.35, 0, 0] }],
  [0.66, { hipsPos: [0, -0.22, 0.9], hips: [PI * 1.92, 0, 0], spine: [0.4, 0, 0], head: [-0.2, 0, 0], ulL: [-1.3, 0, 0.1], llL: [1.9, 0, 0], ulR: [-0.6, 0, -0.1], llR: [1.2, 0, 0], uaL: [-0.4, 0, 0.9], laL: [-0.5, 0, 0], uaR: [-0.6, 0, -0.9], laR: [-0.6, 0, 0], tailB: [0.6, 0, 0] }],
  [0.9, { ...GUARD, hipsPos: [0, -0.06, 1.0], hips: [PI * 2, 0.3, 0], sword: 'frente' }],
  [1.4, { ...GUARD, hipsPos: [0, -0.06, 1.0], hips: [PI * 2, 0.3, 0] }],
], 1.4, true);

export const CLIPS = {
  correr: { fn: correr, dur: 0.62 * 2, loop: true, label: 'Correr' },
  caminar: { fn: caminar, dur: 1.05 * 2, loop: true, label: 'Caminar' },
  saltar: { fn: saltar, dur: 1.6, loop: true, label: 'Saltar' },
  combo: { fn: combo, dur: 2.1, loop: true, label: 'Combo ×3' },
  voltereta: { fn: voltereta, dur: 1.4, loop: true, label: 'Voltereta' },
};

// ---- aplicar una pose al rig ----
const _idleSword = new WeakMap();
export function applyAnim(rig, name, t) {
  const M = boneMap(rig); const p = CLIPS[name].fn(t);
  for (const [k, g] of Object.entries(M)) { const v = p[k] || rest(k); g.rotation.set(v[0], v[1], v[2]); }
  const hp = p.hipsPos || [0, 0, 0]; rig.bones.hips.position.set(hp[0], HIPS_Y + hp[1], hp[2]);
  // espada en la mano derecha con la orientación de la acción
  const sw = rig.sword, hand = rig.bones.armR.hand, spine = rig.bones.spine;
  if (!_idleSword.has(rig)) { if (sw.parent === hand) _idleSword.set(rig, sw.quaternion.clone()); }
  if (p.sword === 'espalda') {  // envainada en diagonal a la espalda, empuñadura sobre el hombro derecho
    if (sw.parent !== spine) spine.add(sw);
    sw.position.set(-0.12, 0.42, -0.2); sw.rotation.set(0, 0, 0); sw.rotateX(-0.12); sw.rotateZ(PI + 0.55);
    return;
  }
  if (sw.parent !== hand) { hand.add(sw); sw.position.set(0, -0.055, 0.008); }
  if (p.sword === 'hombro' && _idleSword.has(rig)) sw.quaternion.copy(_idleSword.get(rig)); else sw.quaternion.copy(SWORD[p.sword === 'hombro' ? 'frente' : (p.sword || 'frente')]);
}
export function resetHips(rig) { rig.bones.hips.position.set(0, HIPS_Y, 0); }
