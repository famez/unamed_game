# Bestiario de tamaños: Farolas

Ocho criaturas con anatomía, escala y esqueleto propios, para romper el aspecto «todos del mismo molde». Mantienen el lenguaje común: brillo cálido para la luz (`Glow_FFD27A`/`FFF4D6`), tinta y cristal violeta para la sombra (`Glow_A35CFF`/`C98BFF`), y farolas en todas partes. Mismo formato que el resto: `Cloth` con color por vértice + `Glow_RRGGBB`, un hueso por vértice y huesos sin rotación en bind.

| Archivo | Criatura | Tamaño | Rol propuesto | Huesos |
|---|---|---|---|---|
| `mechita.glb` | **Mechita**, espíritu de vela | 0,36 m | Compañera de Vela; su llama es su pelo y su vida | root, body, head, arm_l/r, leg_l/r |
| `hollinete.glb` | **Hollinete**, gato de hollín | 0,43 m alto, 0,64 m largo | NPC ambiental de los tejados; lleva un farolillo en la cola | hips, chest, neck, head, tail1/2, fl/fr/hl/hr |
| `cangrejo.glb` | **Cangrejo de cristal** | 0,87 m de ancho | Enemigo del canal (sombra); cristales y vetas violeta | root, body, eye_l/r, claw_l/r, leg_l1–3, leg_r1–3 |
| `buho.glb` | **Búho de cobre**, autómata | 0,78 m con la percha | Informador: vigila desde las farolas; lentes encendidas | root, body, head, wing_l/r, tail |
| `topo.glb` | **Topo minero** | 0,84 m, muy ancho | Tendero de los túneles; casco con lámpara, manos-pala, pico | hips, spine, head, arm/hand_l/r, leg/foot_l/r |
| `caracol.glb` | **Caracol mercader** | 1,3 m | Tienda ambulante: puestito con frascos y guirnalda sobre la concha | root, foot, neck, head, stalk_l/r, shell |
| `larguero.glb` | **Larguero**, gigante farolero | 3,5 m (3,2 m sin la lámpara) | Gigante amable que enciende las farolas altas | Los 19 nombres humanos (pelvis…toe_r) con proporciones de gigante |
| `ballena.glb` | **Ballena de niebla** | 7,5 m de largo, flota a 4 m | Criatura del cielo; lleva farolillos colgando del vientre | body, head, jaw, fin_l/r, tail1/2 |

## Por qué ya no parecen Playmobil
- **Esqueletos distintos:** cuadrúpedo, ave posada, caracol, crustáceo de seis patas, cetáceo y un gigante con brazos hasta las rodillas.
- **Proporciones distintas:** la Mechita es casi toda cabeza, el topo es más ancho que alto y el Larguero tiene las piernas el doble de largas que el torso.
- **Formas orgánicas:** cuerpos de esferas deformadas con ruido suave, miembros que se estrechan y abultan, borlas de pelo y hollín y goterones de cera, en lugar de cilindros iguales.
- **Siluetas únicas:** cada una se reconoce en negro (orejas y cola con farol, concha con tejadillo, cabeza-farola, aletas).

## Animación sugerida (todas por rotación local de huesos)
- Mechita: `head` oscila (la llama tiembla), saltitos con `root`.
- Hollinete: `tail1`/`tail2` en vaivén lento y `head` gira a mirar al jugador.
- Búho: `head` gira ±120° a golpes secos (es un autómata); `wing_*` se abren al echar a volar.
- Topo: `hand_*` excavan en alternancia.
- Caracol: `stalk_*` se retraen y se extienden; `neck` se balancea.
- Larguero: igual que un humanoide, pero a la mitad de velocidad.
- Ballena: `tail1`/`tail2` en onda vertical lenta y `fin_*` remando; el juego mueve `body` en una trayectoria por el cielo.
- Cangrejo: patas en dos grupos alternos (1 y 3 de un lado con la 2 del otro) y `claw_*` que chasquean.

## En el juego
- `cangrejo.glb` está con los demás enemigos (`assets/models/`): es el **Cangrejo de cristal**, enemigo de sombra (`EnemyKind::Crab`).
- Las demás están aquí; el juego lee su esqueleto del propio `.glb` (`src/actors/creature.h`) y las mueve en `src/game/creatures.cpp`.
- Renders del paquete: `docs/bestiario/`.

Regenerar: `tools/bestiario/` → `npm i three@0.170.0 && node build_creatures.mjs` (necesita en `orig/` la `chica_neon.glb` de `assets/models/habitantes` y escribe en `out/`).
