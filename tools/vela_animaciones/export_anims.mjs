import * as THREE from 'three';
import { GLTFExporter } from 'three/examples/jsm/exporters/GLTFExporter.js';
import { writeFileSync } from 'node:fs';
import { buildVela, buildHollin, applyAPose } from './site/model.js';
import { CLIPS, CLIPS_H, applyAnim, boneMap, resetHips } from './site/anims.js';
const WHO = process.argv[2] || 'vela'; const SET = WHO === 'hollin' ? CLIPS_H : CLIPS;
globalThis.FileReader = class { readAsArrayBuffer(b) { b.arrayBuffer().then((x) => { this.result = x; this.onloadend?.(); }); } readAsDataURL(b) { b.arrayBuffer().then((x) => { this.result = 'data:application/octet-stream;base64,' + Buffer.from(x).toString('base64'); this.onloadend?.(); }); } };

const rig = WHO === 'hollin' ? buildHollin() : buildVela(); const scene = new THREE.Scene(); scene.add(rig.root);
const M = boneMap(rig); const nodes = [...new Set([...Object.values(M), rig.bones.hips])];
const FPS = 30; const clips = [];
const sword = rig.sword;
for (const [name, c] of Object.entries(SET)) {
  const n = Math.round(c.dur * FPS) + 1; const times = new Float32Array(n);
  const q = new Map(nodes.map((g) => [g, new Float32Array(n * 4)])); const hp = new Float32Array(n * 3);
  const sp = new Float32Array(n * 3), sq = new Float32Array(n * 4);
  for (let i = 0; i < n; i++) {
    const t = Math.min(c.dur, i / FPS); times[i] = t; applyAnim(rig, name, t); rig.root.updateMatrixWorld(true);
    for (const g of nodes) g.quaternion.toArray(q.get(g), i * 4);
    rig.bones.hips.position.toArray(hp, i * 3);
    // la espada se exporta como nodo hijo de la raíz con su transformación del mundo en cada fotograma
    const P = new THREE.Vector3(), Q = new THREE.Quaternion(), Sc = new THREE.Vector3(); sword.matrixWorld.decompose(P, Q, Sc);
    P.toArray(sp, i * 3); Q.toArray(sq, i * 4);
  }
  const tracks = nodes.map((g) => new THREE.QuaternionKeyframeTrack(`${g.name}.quaternion`, times, q.get(g)));
  tracks.push(new THREE.VectorKeyframeTrack('Hips.position', times, hp));
  tracks.push(new THREE.VectorKeyframeTrack(`${sword.name}.position`, times, sp), new THREE.QuaternionKeyframeTrack(`${sword.name}.quaternion`, times, sq));
  clips.push(new THREE.AnimationClip(name, c.dur, tracks));
  console.log(name.padEnd(10), c.dur.toFixed(2), 's', n, 'fotogramas', tracks.length, 'pistas');
}
// pose de reposo: A, con la espada colgando de la raíz (la animación la coloca)
resetHips(rig); applyAPose(rig); rig.root.add(sword); rig.root.updateMatrixWorld(true);
const names = new Set(); let dup = 0; rig.root.traverse((o) => { if (names.has(o.name) && nodes.concat([sword]).includes(o)) dup++; names.add(o.name); });
const glb = await new GLTFExporter().parseAsync(rig.root, { binary: true, animations: clips });
writeFileSync(`site/${WHO}_animaciones.glb`, Buffer.from(glb)); console.log('glb', (glb.byteLength / 1024).toFixed(0), 'KB', 'nombres duplicados en huesos:', dup);
