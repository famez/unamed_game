# Ciudadanos rehechos: Abuelo Candil, mozo de la fonda y Chispa

Tres `.glb` que sustituyen a `abuelo.glb`, `cafetero.glb` y `chica_neon.glb`. Mismo esqueleto de 19 huesos, nodos e `inverseBindMatrices` idénticos byte a byte a los tuyos; mismo formato de malla (colores por vértice, un hueso por vértice) y materiales `Cloth` + `Glow_RRGGBB`.

## Qué cambia en cada uno
| | Silueta | Cabeza y cara | Ropa | Oficio | Toque de bando |
|---|---|---|---|---|---|
| **Abuelo Candil** (farolero jubilado) | Encorvado | Calvo con gorra de farolero, bigote, párpados caídos, arrugas, boca recta | Abrigo largo gris desgastado con ribete, deshilachado, medallas de latas de aceite | Bastón hecho de un trozo de poste con farolillo apagado | Grieta de luz vieja y apagada en la mejilla |
| **Mozo de la fonda** | Ancho y bajo | Rapado, gorra de visera con bombilla, sonrisa amplia, bigote | Camisa blanca con mangas remangadas, delantal de cuero hasta los pies, paño al hombro | Cafetera de cobre a la espalda con tubos y un hilo de vapor dorado; taza en la mano | Vapor cálido |
| **Chispa** (aprendiz de la Doctora Umbra) | Menuda | Dos coletas rosa que brillan en las puntas, gafas de soldar, pestañas, cejas vivas, pecas | Chaleco de cuero con bolsillos, camisa oscura, vaqueros, guante de trabajo | Herramientas en el cinturón y llave inglesa en la mano | Rótula de cristal de sombra con grietas violeta en la rodilla izquierda |

## Poses idle
`poses_idle_ciudadanos.json` trae la pose de cada uno como rotaciones locales (Euler XYZ, radianes) sobre la pose A. Son las poses de los renders: el abuelo encorvado apoyado en el bastón, el mozo con la mano en el hombro y Chispa de puntillas con el pie derecho. Aplícalas como pose de reposo y mezcla encima las animaciones de hablar.

El bastón cuelga de `wrist_r` apuntando a -Y de la muñeca. Para que se apoye en el suelo, orienta `wrist_r` hacia abajo en la pose (en el JSON hay una nota).

## El generador de ciudadanos (`citizens.js`)
Es el sistema de piezas de la biblia, ya funcionando:
- `SILHOUETTES`: ancho, alto, encorvado, menudo, redondo, normal. Escalan las piezas de cada hueso.
- `HAIR`: calvo_gorra, rapado, coletas, melena, recogido.
- `HATS`: gorra_farolero, gafas_soldar, gorra_visera.
- `TORSOS`: abrigo_largo (con opción `worn`), delantal_cuero, chaleco_herramientas.
- `PROPS`: baston_farol, cafetera_mochila, herramientas_cinturon.
- `TOUCHES`: grieta_vieja, rotula_cristal, bufanda_luz.
- `head(...)` admite expresión: `lid` (párpado caído), `browTilt`, `browH`, `browThick`, `mouth` (smile, grin, flat, o), `pupil`, más `nose`, `moustache`, `wrinkles`, `lashes`.

Un ciudadano nuevo son unas 10 líneas: silueta + colores + `head` + un pelo + un sombrero + un torso + un prop + un toque. `node build_citizens.mjs` exporta todos los que haya en `CITIZENS`.

## Los otros trece (tanda 2)
Todos generados con `citizens.js` sobre la misma plantilla de esqueleto (`chica_neon.glb`), con el armature renombrado a su nombre. Poses idle en `poses_idle_ciudadanos.json` (16 personajes).

| Archivo | Personaje | Silueta | Piezas nuevas usadas |
|---|---|---|---|
| `senora_mecha.glb` | Señora Mecha (fonda) | redondo | moño con cucharas, falda larga + delantal blanco con quemaduras doradas, olla-farola y cucharón |
| `pabilo.glb` | Pabilo (hermano de Vela) | menudo | gorra enorme, mangas dobladas, escalera plegable a la espalda, farol de juguete |
| `deshollinadora.glb` | Deshollinadora | alto | trenza, cepillo circular al hombro, manchas de hollín violeta |
| `aceitero.glb` | Aceitero | redondo | sombrero de ala, bidones de cobre con aceite brillante, embudo |
| `vidriera.glb` | Vidriera | normal | moño, gafas de soldar, caña con gota de vidrio, frasco con cristal de sombra |
| `relojero.glb` | Relojero de postes | menudo | lupa en el ojo, llaves de cuerda, reloj de bolsillo brillante |
| `pregonero.glb` | Pregonero | ancho | campana, cartel con calles tachadas, bufanda con la punta encendida |
| `nino_sombras.glb` | Niño de las sombras | menudo | gorra prestada, tirantes, farolillo de papel, tiza, un ojo violeta |
| `monja_fanal.glb` | Monja del fanal | alto | capucha-farola de cristal (sin cara), hábito con cordón dorado |
| `barquero.glb` | Barquero | encorvado | sombrero, capa encerada, pértiga con farol, brazo izquierdo de sombra |
| `guardia_apagado.glb` | Guardia apagado | ancho | armadura de hojalata abollada, casco de farola cerrado, lanza con bombilla fundida, ojos apagados |
| `trapera.glb` | Trapera | encorvado | capucha, trenza gris, mitones, saco con cristales y piezas, cristal escondido |
| `musica.glb` | Música callejera | alto | melena, sombrero con moneda, organillo de tubos de farola con motas de luz |

Piezas añadidas al generador: pelos `trenza`, `flequillo_corto`, `mono`; sombreros `capucha`, `capucha_farola`, `sombrero_ala`, `lampara_minero`, `lupa_ojo`, `casco_farola`; torsos `habito`, `falda_larga`, `capa_encerada`, `armadura_hojalata`, `chaqueton_capitan`; props `cepillo_hombro`, `bidones`, `cana_vidrio`, `llaves_cuerda`, `campana_cartel`, `farolillos_tiza`, `pertiga_farol`, `lanza_bombilla`, `saco_piezas`, `organillo`; toques `ojo_violeta`, `ojos_apagados`, `mancha_violeta`, `medio_cuerpo_sombra`.
