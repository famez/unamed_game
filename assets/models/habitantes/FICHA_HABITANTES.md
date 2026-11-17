# Habitantes: ficha

> Los personajes del barrio de los faroleros (el paquete «Farolas») tienen su propia ficha:
> `FICHA_CIUDADANOS.md` y `FICHA_TANDA3.md`. Abuelo, cafetero y chica neón son ahora Abuelo
> Candil, el mozo de la fonda y Chispa. Esta ficha cubre los que genera
> `tools/personajes/habitantes.py`.

Nueve personajes para poblar los mapas grandes, con **el mismo esqueleto que la protagonista**
(y que Ilse): se animan con el motor (quieto, andar, correr...). Los genera
`python3 tools/personajes/habitantes.py` a partir de una ficha por personaje (piel, pelo y
peinado, ojos, ropa de arriba y de abajo, calzado y accesorios).

## Compatibilidad
- Esqueleto copiado tal cual de `assets/models/ilse.glb`: nodos, nombres (`pelvis`, `spine`,
  `chest`, `neck`, `head`, `shoulder_l/r`, `elbow_l/r`, `wrist_l/r`, `hip_l/r`, `knee_l/r`,
  `ankle_l/r`, `toe_l/r`), orden y matrices inversas de la pose de reposo.
- Pose A, mirando a +Z, la izquierda en +X, en metros (la cabeza llega a ~1,8 m).
- Skinning rígido: cada pieza va al 100 % a un hueso.
- Atributos `POSITION`, `NORMAL`, `COLOR_0` (lineal), `JOINTS_0`, `WEIGHTS_0`; materiales por
  nombre (`Cloth`, `Metal`, `Glass`, `Glow` brilla).
- Para rehacer uno con otra herramienta (Blender, otra IA): mantener el esqueleto y exportar
  a `.glb` con piel con el mismo nombre. `./build/game --visor fichero.glb` lo enseña animado.

## Reparto
| Fichero | Dónde | Quién |
|---|---|---|
| `cafetero` | Pueblo cafetero | Caficultor: sombrero aguadeño, camisa blanca, poncho al hombro, carriel, bigote, alpargatas |
| `recolectora` | Pueblo cafetero | Recolectora: trenzas, blusa blanca, falda larga roja con volante azul, sombrero de paja y canasto con granos de café |
| `oficinista` | Ciudad Neón | Oficinista: traje azul marino, corbata roja, gafas y maletín |
| `chica_neon` | Ciudad Neón | Melena bob rosa, ojos violeta, sudadera cian con capucha, pantalón corto, medias y cascos que brillan |
| `cocinero` | Ciudad Neón | Cocinero de ramen: cinta en la frente, chaqueta blanca y delantal azul |
| `surfista` | Ciudad de la Bahía | Pelo rizado, camiseta de tirantes, bañador, descalzo, tabla de surf y gafas de sol en la cabeza |
| `vendedora` | Ciudad de la Bahía | Vendedora de playa: pelo afro con una flor, vestido naranja con volante y nevera colgada |
| `granjera` | Campo | Pelirroja con trenzas y pecas, camisa roja, peto vaquero, botas, sombrero de paja y horca |
| `abuelo` | Campo | Gorra, barba blanca, chaleco verde, camisa y bastón |

## Estilo
Anime sencillo, como Vela y Hollín: cabeza algo grande, ojos grandes con iris de color y
brillo, cejas y pestañas marcadas, nariz y boca mínimas, colores planos con contorno.
