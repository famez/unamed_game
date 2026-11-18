import { CREATURES } from './creatures.js';
import { exportGLB } from './kit.js';
import * as THREE from 'three';
const only = process.argv.slice(2);
for (const [name, fn] of Object.entries(CREATURES)) {
  if (only.length && !only.includes(name)) continue;
  const rig = fn(); rig.root.updateMatrixWorld(true);
  const b = new THREE.Box3().setFromObject(rig.root); const s = b.getSize(new THREE.Vector3());
  const info = exportGLB(rig, 'orig/chica_neon.glb', `out/${name}.glb`);
  console.log(name.padEnd(10), 'alto', s.y.toFixed(2), 'ancho', s.x.toFixed(2), 'largo', s.z.toFixed(2), '| tris', info.tris, 'KB', Math.round(info.bytes / 1024));
}
