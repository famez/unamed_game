# Tanda 3: Polilla de tinta, Luciérnaga de cristal, Brasa y Doctora Umbra

## Enjambres (esqueleto propio, ligero)
Los dos tienen su propio esqueleto mínimo, exportado con el mismo layout que tus `.glb` (`<nombre>_Armature` > `<nombre>_Mesh` + huesos) y skinning rígido. **Los huesos no tienen rotación en bind**, así que una rotación local es una rotación en ejes del mundo.

| | Huesos | Tris | Aleteo | Mecánica (biblia) |
|---|---|---|---|---|
| `polilla.glb` | `root > body > head, wing_l, wing_r` | 1.816 | `wing_l.rotZ = +a·sin(ωt)`, `wing_r` = −a; a ≈ 0,55 rad, ω ≈ 9 Hz·2π | Grupos de 8–12; mueren de un golpe; apagan una farola en 10 s si no se las espanta |
| `luciernaga.glb` | `root > core > wing_l, wing_r, tail` | 608 | a ≈ 0,4 rad, ω ≈ 14 Hz·2π | Voladoras rápidas; explotan al morir y encienden lo que tocan |

Envergadura de la polilla ≈ 0,5 m, luciérnaga ≈ 0,3 m. Colócalas con el `root` en el punto de vuelo; en el visor, cada una lleva un cabeceo suave en `body`/`core` (rotX ±0,1 rad).

## Brasa (aliada, luz): esqueleto humano de 19 huesos
- Silueta alta; abrigo blanco con las dos mangas arrancadas y el bajo deshilachado (su marca: va un paso más allá que Vela).
- **Brazo izquierdo entero de porcelana** (#E7E1D3), con juntas doradas en hombro, codo y muñeca y grietas de luz cálida. Es el accidente que la jubiló.
- Brazal de cuero con anillas en el derecho; pelo gris recogido con un alambre de filamento; grietas apagadas en las mejillas.
- Farol grande de maestra al cinto (izquierda) y gancho de farolero de 1,5 m a la espalda.
- Rol: entrenadora de combos; jefa opcional cuando la luz la consume (para esa fase: todo el cuerpo de porcelana, grietas encendidas al máximo, halo).

## Doctora Umbra (aliada, tienda): esqueleto humano de 19 huesos
- Silueta redonda y baja; moño castaño.
- Diadema de latón con **tres lentes abatibles** plegadas a un lado del ojo derecho (no tapan la cara).
- **Monóculo violeta** sobre el ojo izquierdo, con pupila rasgada: la sombra que estudia la está mirando a ella.
- Delantal de cuero con siete bolsillos y herramientas asomando: destornillador, tubo de ensayo violeta y bombilla. Guantes de cuero.
- En la mano izquierda, un aparato de latón con un cristal de sombra y manivela; llave de cuerda en la cadera.

## Poses idle
`poses_idle_tanda3.json`: Brasa de brazos relajados con el izquierdo (porcelana) hacia delante; Umbra con el aparato alzado mirando a través de la lupa.
