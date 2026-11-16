# Reparto: ficha de NPC y enemigos

Seis modelos rediseñados con el mismo lenguaje visual que Vela y Hollín. **Compatibles con tu motor sin tocar código.**

## Compatibilidad con unamed_game (verificado)
- **Esqueleto intacto:** los nodos, los nombres y el orden de los huesos y las `inverseBindMatrices` son idénticos byte a byte a tus `.glb` originales. Solo cambia la malla.
- **Mismo formato de malla:** atributos `POSITION`, `NORMAL`, `COLOR_0` (float VEC4, lineal), `JOINTS_0` (u8) y `WEIGHTS_0` (u8 normalizado), con índices uint32.
- **Skinning rígido:** cada vértice va al 100 % a un hueso, como en los originales.
- **Materiales:** `Cloth` (blanco; el color va por vértice) y `Glow_RRGGBB`, con `emissiveFactor`. Ahora los brillos están unificados por bando:
  - **Luz:** `Glow_FFD27A` (cálido, el de la espada y el farol de Vela) y `Glow_FFF4D6` (blanco al rojo).
  - **Sombra:** `Glow_A35CFF` (vacío profundo, el del martillo de Hollín) y `Glow_C98BFF` (vacío vivo).
- Las superficies abiertas (faldas, jirones, mechones) llevan **las dos caras** en la malla, así que se ven bien aunque el motor haga *backface culling*.
- Ilse usa 15 de sus 19 huesos: las piernas (`hip`/`knee`) quedan bajo el vestido, que va con `pelvis` como en tu versión. Los zapatos sí van con `ankle` y `toe`.

## Reglas de estilo comunes
| | Sombra | Luz |
|---|---|---|
| Base | Tinta #221E2B–#2E2838 | Blanco roto #EDE8DF, porcelana #E7E1D3 |
| Metal | Hierro viejo #1C1A22, cadenas | Oro #D9A441 / #A8752A, jaulas de farola |
| Detalle | Cristal de sombra #2A1E3D, bordes en picos | Vendas y crema #D8CFBD, ribetes dorados |
| Brillo | Grietas y núcleos violeta | Grietas y bombillas cálidas |

**Motivo común:** las farolas. Vela las enciende, Hollín las rompe, el Coloso se las ha tragado, el Vigía *es* una y el Deslumbrado se ha convertido en bombilla.

## Personajes
### Ilse (NPC, entre los dos bandos)
- Mismo cuerpo y cara anime que Vela.
- **Heterocromía:** el ojo izquierdo es ámbar (luz) y el derecho, violeta (sombra). Mirada serena con los párpados algo caídos.
- Corte hime blanco: flequillo recto, mechones laterales y melena hasta la cintura.
- Vestido ciruela #2A2433 con sobrefalda abierta, bordado dorado #C9A35A, corsé y estola de piel.
- Horquilla con una vela encendida (`Glow_FFD27A`) y pendientes de cristal violeta.

### Coloso (sombra, pesado)
- Torso con costillas de poste de farola y un núcleo de vacío (esfera negra, anillo violeta y grietas radiales).
- Hombreras de cristal y una farola rota incrustada en el hombro derecho.
- Cabeza hundida con cuernos de cristal, ojos-rendija y dientes de cristal.
- Cadenas enrolladas en los antebrazos y nudillos de cristal.
- Taparrabos violeta con el bajo en picos.

### Lobo de sombra (sombra, rápido)
- Esbelto, de hocico largo y orejas altas.
- Crin de cristal desde la nuca hasta la grupa, con una veta violeta.
- Fauces que brillan por dentro.
- Cola de humo con la punta encendida.

### Sabueso (sombra, rastreador)
- Mismo esqueleto que el Lobo, pero pesado: pecho ancho, hocico corto, orejas caídas y ojos redondos.
- Collar de pinchos con cadena rota y un farolillo apagado con agujero negro.
- Dos vetas violeta por el lomo.

### Deslumbrado (luz)
- Un farolero que miró demasiado a la luz: la cabeza es una **bombilla blanca** en una jaula dorada, con un halo detrás.
- Cuerpo de maniquí de porcelana con grietas de luz cálida.
- Abrigo de farolero hecho jirones.
- Brazos largos y vendados con garras doradas.

### Vigía (luz, centinela)
- Cabeza de farola hexagonal blanca con los cristales encendidos.
- `eye`: lente al rojo blanco con diafragma.
- `ring_a`: halo de 8 bombillas.
- `ring_b`: 12 motas de luz.
- `strip1–4`: cables colgantes con una bombillita en la punta.

## Regenerar
`fuente/` contiene `kit.js` (modelado por huesos y exportador compatible), `models.js` (los seis diseños), `build.mjs` y `joints.json` (pivotes extraídos de tus IBM). Pon tus `.glb` originales en `orig/` y ejecuta `npm i three@0.170.0 && node build.mjs`. El exportador copia el esqueleto de cada original.
