# unamed_game

Action-RPG en tercera persona en C++17 + OpenGL 3.3 Core, sin motor. **La Hora Detenida**:
la ciudad de Iridia se quedó parada en el crepúsculo y la luz y la sombra se separaron. El
protagonista, hecho de las dos, despierta sin fuerzas en una plaza gris y va recuperando
poderes de ambos lados para volver a poner en marcha la hora. Tono adulto y sobrio, casi sin
diálogo: la historia se cuenta con escenas e imágenes (ver `docs/historia.md`).

## Historia

La biblia está en `docs/historia.md`. El capítulo 1, *La plaza parada*, se cuenta casi sin
palabras (`assets/story/capitulo1.txt`, formato en `src/game/story.h` y `src/game/cinema.cpp`):

- **Planos de cámara** (`!plano`) con bandas negras, encadenados con `!pausa`.
- **Ecos**: siluetas de luz que repiten un recuerdo (una mujer arreglando una farola mientras
  un chico le sujeta la escalera, alguien que busca por la calle, una hija que se despide de
  su madre, un chico que no se atreve a cruzar el puente).
- **Tutoriales mínimos** (`!tutorial`): una tecla y una palabra, hasta que se hace la acción.
- **Poderes que vuelven** (`!poder`): se despierta sin fuerzas ni arma; al coger una hoja
  vuelven la fuerza y el combate; los nodos devuelven la absorción y la luz; liberar a la
  que espera, la Supernova; y al final Ilse apaga el puente entero y despierta la sombra.
- **La chispa** no habla: se adelanta hacia el objetivo y espera sobre él.

Para escribir escenas: `./build/game --escena eco_alba` salta la intro y la reproduce.

## Música y sonido

Todo está sintetizado por código (`src/audio/synth.cpp`), sin ficheros de audio en el
repositorio. Hay una pista por escenario y otra de jefe (cada una con su tonalidad, modo, tempo e
instrumentos, y melodías generadas por frases a partir de la armonía); cada pista son cuatro capas
sincronizadas cuyo volumen decide el juego: el **colchón** siempre, la **melodía** cuando
vuelve el color, el **pulso** (bajo y percusión) en combate y el **brillo** (arpegio) cuando
la ciudad ya brilla. Se genera la primera vez en segundo plano (~2 s) y se guarda en
`~/.cache/unamed_game/`. Los 39 efectos se generan al arrancar: golpes en capas (chasquido de
impacto, cuerpo grave, crujido y timbre metálico, con saturación), guardia, explosiones,
enemigos que gruñen al preparar el ataque y se deshacen al morir, sin campanitas.

**La voz de la protagonista** son grabaciones de voz real (`assets/audio/voces/`, «Female RPG
Voice Starter Pack» de Cici Fyre, CC0): grita al golpear, en los remates y las
transformaciones, al saltar, al esquivar y al recibir daño, con varias tomas al azar. **Vela**
(luz) tiene la voz dulce y aguda; **Hollín** (sombra), la madura y grave. Se pueden añadir o
cambiar tomas sin tocar código (nombres en `assets/audio/voces/LEEME.md`). Si no hay
grabaciones, hay una voz sintetizada por formantes de respaldo, con la risa de Hollín.

La salida usa PulseAudio/PipeWire cargado con `dlopen` en tiempo de ejecución: no hace falta
ninguna cabecera para compilar y, si no está, el juego va en silencio.

## Compilar y ejecutar

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j4
./build/game                     # --calidad baja|media|alta  --resolucion 1920x1080  --debug

# Debug: símbolos, UBSan, aserciones de libstdc++ y mensajes de KHR_debug del driver
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug && cmake --build build-debug -j4

# Tests de la lógica (sin ventana, con AddressSanitizer + UBSan)
ctest --test-dir build --output-on-failure     # o ./build/logic_test para ver cada caso
```

Dependencias del sistema: `libOpenGL`/`libGLX` (glvnd + Mesa), `libX11` y `libXi`
(`sudo apt install libxi-dev`). Incluidas en el repositorio: `stb_truetype.h` y las
fuentes Orbitron y Exo 2.

Opciones: `--silencio` (sin audio), `--volumen 0..1`. De desarrollo: `--escena id` (reproduce una escena del guion), `--autoplay` (un bot combate solo, sin capturar el ratón), `--sombra` (empezar en modo sombra), `--exportar-modelos [carpeta]` (escribe a .glb los personajes que falten; `--forzar` los sustituye), `--visor fichero.glb` (visor de modelos), `--vitrina` (guion que enseña ataques, técnicas, formas y mecánicas), `--video fichero.mp4 [seg]` (graba a velocidad real con ffmpeg),
`--screenshot fichero.ppm segundos [frames]` (guarda frames y sale) y `--camyaw radianes`.

## Controles

| Tecla | Acción |
|---|---|
| W A S D | Correr (relativo a la cámara). **Shift**: correr más rápido |
| Espacio | Saltar; en el aire, **doble salto** con voltereta |
| Clic izquierdo | Atacar (combo según el arma). **Mantener**: corte cargado (lanza una onda) |
| Clic derecho (mantener) | Guardia (se puede andar mirando a la cámara). Justo antes del golpe: **bloqueo perfecto**, el enemigo queda aturdido y atacar lanza el **súper contraataque** |
| C / Ctrl | Voltereta de esquiva (invulnerable); un golpe justo después es un **contraataque** |
| Q | **Luz**: junto a una farola, **entrar en ella**; dentro, saltar a otra fuente. **Sombra**: en la oscuridad, **fundirse** con el suelo (y, empujando contra una pared oscura, **trepar** por ella hasta el tejado) |
| Portal / Z | Cambiar entre **luz y sombra** cruzando un portal (Z en los niveles sin historia) |
| G | **Transformarse** (50 de Poder, 20 s): Forma Solar en la luz, Lobo de sombra en la sombra |
| T | **Invocar compañero** (chispa aliada o el eco de Nora) |
| R | Fijar objetivo / **soltarlo** (dentro de una fuente: elegir a qué fuente saltar) · clic central: siguiente objetivo · X: soltar |
| 1-5 · 6 7 8 · Rueda + E | Técnicas de la mitad activa · atajos de objetos · menú de comandos |
| F | Interactuar: cápsulas, nodos, faroles, espejos, objetos e **interruptores de farolas** |
| Tab / I | Inventario y **taller** (pausa) |
| Esc / P | Pausa: sensibilidad, calidad gráfica, reiniciar, salir |
| F12 / º | Modo debug (esqueletos, zonas, haces y la táctica de cada Gris) |

Especiales de combate: atacar corriendo = **embestida**; atacar con la guardia =
**lanzador** (sube al enemigo); en el aire con la guardia = **picado**.

## Jugabilidad

- **Luz y sombra**: dos mitades del mismo protagonista, sin bando bueno ni malo. Cada una con
  su aspecto (luz: marfil y oro; sombra: negro y violeta), su arma (espada de luz / mazo de
  sombra, con combos distintos) y sus técnicas.
  - **Poder**: una única barra para las técnicas de las dos mitades. No se recarga sola: se
    llena con **pociones de poder** y con los **orbes** que sueltan los Grises.
  - **Luz**: junto a una farola encendida, Q **entra en ella**: dentro no te ven ni te hieren,
    saltas a otras fuentes a la vista (R elige cuál), sales con Espacio o **emboscas** con el
    clic al Gris más cercano. Entrar y saltar gastan Poder, y la farola que te hospeda se atenúa.
  - **Sombra**: la oscuridad es donde no llega la luz de ninguna farola encendida, nodo o farol
    (se ve claramente más oscura). Q en la oscuridad **funde** al protagonista con el suelo: se
    mueve rápido e invisible por la zona oscura, la luz no le deja pasar y gasta Poder. Sale con
    Q o Espacio, o con el clic en emboscada.
  - **Técnicas** (1-5), distintas en cada mitad:
    - Luz: *Lanza de luz* (persigue), *Refracción* (tres esquirlas que congelan), *Calidez*
      (cura), *Estallido solar* (bola en arco que estalla y lanza por los aires) y *Supernova*
      (con el Poder lleno).
    - Sombra: *Púas de sombra* (recorren el suelo y lanzan), *Cadenas del vacío* (inmovilizan a
      los de alrededor), *Drenar* (roba vida), *Mina oscura* (implosiona fuerte) y *Agujero negro*.
  - **Crear sombra**: las farolas se **rompen a golpes** (y ya no se arreglan) o se apagan y
    encienden con su **interruptor** (F), que suele estar apartado de ellas.
  - Se cambia de luz a sombra **cruzando un portal** (en los niveles sin historia, también con Z).
- **Enemigos de luz y de sombra** (todos fueron personas): *Deslumbrado* (figura alta y
  sobreexpuesta, dos zarpazos), *Vigía* (ojo flotante que dispara luz), *Coloso* (mole de
  sombra que aplasta) y *Sabueso* (cuadrúpedo que caza en manada y muerde por la espalda).
  Sueltan Luz o Sombra; la luz debilita a los de sombra y la oscuridad a los de luz, y la
  mitad contraria del protagonista les hace más daño.
- **Croma**: cada escenario empieza casi gris; encender nodos y faroles devuelve el color
  en una zona que crece a su alrededor; cada Gris disipado deja una salpicadura.
- **Escenarios por partes** con objetivos, combates de zona con barreras, un jefe, faroles
  como puntos de control y caídas al vacío.
- **Rompecabezas de luz**: espejos que se giran (F) para llevar un haz a su receptor;
  algunos tiñen la luz y hay receptores que solo aceptan un color.
- **Armas**: prisma básico, estelar y aurora (hoja), lanza de luz (estocadas) y
  guanteletes de luz (puñetazos), cada una con su juego de movimientos.
- **Taller**: recetas con los materiales de los Grises (armas, núcleos, afiladores que
  suben la fuerza de forma permanente, consumibles mejores).
- **Formas prismáticas** (25 s, consumen un núcleo): Aurora (rápida), Coloso (golpes con
  onda, no se inmuta) y Eco (casi sin gravedad, orbes con cada golpe).
- **Compañeros**: chispa aliada (40 de Luz) y el eco de Nora (cura y aturde).
- **IA con aprendizaje por refuerzo** (Q-learning): cada especie elige tácticas (acercarse,
  atacar, retroceder, rodear, esperar, amagar, flanquear) según lo que haces y si les estás
  mirando, aprende deprisa de lo que le funciona contra ti y lo recuerda entre partidas
  (`~/.cache/unamed_game/ia_*.q`). Atacan por turnos (como mucho dos a la vez; el resto
  rodea), aprovechan cuando les das la espalda y se apartan de tus golpes cada vez mejor. El
  jefe aprende muy deprisa durante el combate.

## Niveles

La historia está aparcada por ahora: al arrancar se elige un nivel en el selector, cada uno
para probar mecánicas, sin escenas y con la sombra ya despierta. Desde la pausa, «Elegir nivel»
vuelve al selector.

```bash
./build/game                    # selector de niveles
./build/game --nivel selva      # directamente un nivel (plaza, mercado, puente, selva, cueva, callejones,
                                # o los mapas grandes: campo, neon, cafetero, rio)
./build/game --prueba 1234      # patio de combate; con semilla, siempre el mismo
./build/game --historia         # la historia (capítulo 1)
```

- **Patio de combate**: patio aleatorio cerrado por edificios, con farolas, nodos, dos portales y
  oscuridad entre las farolas. Grises de luz y de sombra sin fin y un Gris mayor cada 30 bajas.
  Trae todas las armas y materiales para forjar.
- **Plaza, Mercado, Puente**: los escenarios con sus combates de zona y objetivos.
- **De noche**, con oscuridad de verdad y luces propias que se pueden usar para viajar o romper:
  - **Selva de los Puentes**: plataformas en árboles gigantes unidas por puentes colgantes sobre
    un barranco, alumbradas con antorchas.
  - **Cueva de Cristal**: salas de roca unidas por túneles, racimos de cristal que brillan y un
    río subterráneo.
  - **Callejones del Farol**: calles estrechas con farolillos de papel colgados de lado a lado y
    letreros de neón en las fachadas.
- **Mapas grandes**, hechos con terreno con relieve y piezas `.glb`. El campo, el pueblo y la
  bahía están en el **atardecer detenido** de los primeros niveles: luz baja, poco color (vuelve
  al encender sus cuatro **nodos de color**) y la oscuridad lejos de las farolas. La Ciudad Neón
  es de noche y a todo color.
  - **Campo abierto** (`--nivel campo`): **salen Grises al azar** por el valle (lejos de
    la granja; como mucho cinco a la vez; los que se quedan atrás desaparecen). Valle de 400 m cerrado por montañas,
    con colinas, un río con puente de madera, un camino con faroles, una granja (casas, granero,
    pozo, huerto vallado con fardos), la loma de los molinos y un pinar.
  - **Ciudad Neón** (`--nivel neon`, de noche): manzanas de torres con ventanas encendidas,
    tiendas, letreros verticales, expendedoras, pantallas, semáforos y pasos de cebra, una vía
    elevada y, en el centro, un santuario con torii, cerezos y puestos de comida.
  - **Pueblo cafetero** (`--nivel cafetero`; inspirado en Salento y Filandia): en lo
    alto de una loma, plaza empedrada con iglesia, kiosco, bancas, palmas y yipaos; calles de
    casas de bahareque con zócalos, puertas y balcones de colores; alrededor, cafetales con
    plataneras de sombrío, un guadual en la cañada y palmas de cera en los cerros.
  - **Ciudad de la Bahía** (`--nivel rio`; al estilo de Río de Janeiro, pero en el tono del
    juego: la playa vacía con las sombrillas cerradas y las ventanas de la favela encendidas): playa en
    media luna con el paseo de ondas blancas y negras, cocoteros, quioscos, sombrillas y
    puestos de socorrista; la avenida con los edificios frente al mar y manzanas detrás; los
    arcos con el tranvía amarillo; la favela de casas de colores con su depósito de agua en el
    morro, con escaleras de azulejos; la estatua de brazos abiertos en lo alto del cerro; y en
    la bahía, el Pan de Azúcar y el morro de Urca unidos por el teleférico.
  - **Habitantes** (`assets/models/habitantes`, ver su `FICHA_HABITANTES.md`): nueve
    personajes con el esqueleto de la protagonista que pasean por su zona de cada mapa, se
    paran a descansar y miran a la protagonista si se acerca (cafeteros y recolectoras en el
    pueblo, oficinistas, chicas neón y cocineros en la ciudad, surfistas y vendedoras en la
    playa, granjeras y abuelos en el campo). Los genera `python3 tools/personajes/habitantes.py`.

  - `--vuelo` sobrevuela el mapa y `--medir` escribe los fps reales por la consola.
- En todos: **Z** cambia entre luz y sombra y **F1 / F2** hacen infinitos la vida y el Poder
  (también en la pausa). Por defecto solo la vida.

## Piezas del escenario (`assets/props`)

Árboles, rocas, casas, farolas, letreros... cada uno es un `.glb` con el origen en el centro
de la base, Y hacia arriba y en metros. El juego los lee al arrancar y los dibuja con
instancias (una llamada a la GPU por modelo, se vea cuantas veces se vea). Convenciones,
que valen igual desde Blender:

- Mallas normales: lo que se ve. El material se lee por el nombre: `Glow…` (o con emisión)
  brilla, `Metal`/`Glass`, `Stone`, `Grass`, `Water`; el resto, tela. El color es el color
  por vértice multiplicado por el color base del material.
- `COL_…`: mallas invisibles que hacen de **caja de colisión** (su caja envolvente). Sin
  ninguna, la pieza no choca (hierba, flores).
- `LUZ_…`: un vacío que es una **fuente de luz del juego** (alumbra, se puede viajar a ella,
  romper y apagar). `LUZ_RRGGBB` le da color.

Las piezas de ahora las genera `python3 tools/props/generar.py` (solo la biblioteca estándar;
`campo.py`, `ciudad.py`, `cafetero.py` y `rio.py` describen cada pieza con cajas, cilindros, esferas,
tejados y vigas). Volver a generar **sobrescribe** los ficheros: una pieza rehecha a mano
conviene guardarla con otro nombre o regenerar el resto con `--solo nombre1,nombre2`.
`./build/game --visor assets/props` enseña todas en una galería (WASD para moverse, F las
colisiones y luces, V la noche) y se recarga sola al cambiar cualquiera.

## Modo debug (F12)

No usa el render con shaders: se dibuja en alambre con un único buffer de líneas
(esqueletos con la izquierda en azul y la derecha en rojo, articulaciones, cajas, escenario,
sector del último golpe, botín como cruces) y encima de cada enemigo su estado de IA y vida.

## Render

- **La protagonista** sale de modelos `.glb` (`assets/models/vela.glb` en la luz y
  `hollin.glb` en la sombra) que el juego lee al arrancar con un lector propio de glTF
  binario (`src/render/glb.cpp`). Para cambiarlos o hacer otros basta con exportar un `.glb`
  con la jerarquía de la ficha (`assets/models/FICHA_VELA_HOLLIN.md`): grupos con nombre de
  hueso (`Hips > Spine > Neck > Head`, `Shoulder_L > UpperArm_L > LowerArm_L > Hand_L`,
  `UpperLeg_L > LowerLeg_L > Foot_L`…), el pivote en la articulación, pose A, metros y
  mirando a +Z. Cada malla se pega al hueso de su grupo; el torso y el faldón se reparten
  entre varios huesos; `Ponytail`/`Mane`, `Ahoge`, `Scarf_Tail_Front/Back` y `Lantern` se
  mueven por inercia; `Prop_Sword`/`Prop_Hammer` es el arma (agarre en el origen, hoja hacia
  +Y). Los materiales emisivos brillan, `Hair*` lleva el brillo del pelo e `Iron`/`Gold*`
  son metal. El farol brilla según la vida, y el retrato del HUD es el mismo modelo en vivo.
  Sin los `.glb`, se usa el modelo de primitivas de siempre.
- **Los demás personajes también están en `.glb`** (`assets/models/`): `deslumbrado`,
  `coloso`, `vigia`, `sabueso`, `ilse` y `lobo_de_sombra` (la transformación de la sombra; la
  Forma Solar es Vela con efectos). Estos son `.glb` estándar con **armadura** (los huesos del
  juego con sus nombres, en pose de reposo), malla con pesos de piel, color por vértice y los
  ojos y brillos como material emisivo: se abren tal cual en Blender. Para rehacer uno, basta
  con mantener los nombres de los huesos de la armadura y exportar a `.glb` con piel; los
  materiales se leen por el nombre (`Glow…` o con emisión brilla, `Metal`, `Hair…`, `Stone`,
  el resto tela). Si se borra un `.glb`, vuelve el modelo generado, y
  `./build/game --exportar-modelos` escribe los que falten desde las primitivas (con
  `--forzar`, también sustituye los que hay). Los de ahora son el reparto rediseñado
  (`assets/models/FICHA_REPARTO.md`).
- **Visor de modelos**: `./build/game --visor fichero.glb` enseña un `.glb` como lo pinta el
  juego (mismo render, contorno y brillos), animado con el motor (la protagonista y los
  humanoides: quieta, andar, correr, combo, saltar, esquivar, guardia; los enemigos: quieto,
  persigue, ataque, aturdido, aparece, muere). Reconoce el tipo por el nombre del fichero o
  por sus huesos, lista los materiales y cómo los entiende el juego, y **se recarga solo al
  volver a exportar** desde Blender (o con R); si el fichero nuevo tiene un error, lo dice y
  deja el anterior. Ratón para girar, rueda para acercar, 1-7 o Tab para la animación,
  Espacio pausa, F enseña los huesos, V la noche (para ver los brillos).
- **Cuerpos continuos**: cada personaje se define con elipsoides y cápsulas pegados al
  esqueleto; al arrancar se funden (unión suave de funciones de distancia), se convierten en
  malla (*surface nets*) con pesos de hueso y la GPU la deforma (skinning). Contorno por
  casco invertido. Los detalles nítidos o luminosos son piezas rígidas aparte.
- **Post-proceso**: la escena se dibuja en un buffer propio a resolución interna escalada;
  el alfa es la máscara de brillo y un bloom de cadena de reducciones da halo a neones,
  efectos y destellos.
- **Estilo**: sombreado de animación, materiales emisivo, agua, césped y suelo con líneas de
  luz; cielo con nubes; desaturación del escenario según el croma y salpicaduras de color.
- **Texto**: fuentes reales en un atlas SDF (nítidas a cualquier tamaño, con contorno y
  halo, tildes y ñ); la interfaz se diseña a 720 px de alto y escala con la ventana.

## Hardware objetivo y decisiones

Máquina de desarrollo: i7-7500U (2C/4T), 12 GB RAM, Intel HD 620 (Mesa) + GeForce 920M (nouveau).

- **OpenGL 3.3 Core** en vez de Vulkan: la HD 620 soporta ambos, pero Vulkan exige
  ~1000 líneas de boilerplate (instancias, swapchain, sincronización, pipelines) antes de
  pintar una línea. 3.3 es la API moderna más baja: corre en casi cualquier GPU de 2010+.
- **Usar la Intel, no la 920M**: con `nouveau` la Kepler no hace reclocking y rinde menos
  que la integrada. Mesa elige la Intel por defecto (`DRI_PRIME=1` forzaría la NVIDIA).
- **X11 + GLX directo** en vez de GLFW/SDL: cero dependencias y nada oculto. Todo lo
  específico de plataforma vive en `core/platform_x11.cpp` detrás de `core/platform.h`, así que
  cambiar a GLFW/SDL (p. ej. para Windows) es reescribir un único fichero.
- **Simulación a paso fijo de 60 Hz** desacoplada del render, **vsync** activado (o un
  limitador a 60 fps si no hay vsync) para no quemar CPU/GPU en un portátil.
- **Un único VBO dinámico** con *orphaning* y pocos draw calls por frame para toda la escena.
- **Sin motor de físicas externo**: los personajes son cuerpos verticales contra cajas
  (AABB), que es todo lo que necesita este tipo de combate. Jolt Physics (ragdolls) se
  quitó al cambiar de género: los enemigos se desvanecen en luz al morir.

Coste medido en Release (HD 620, 1920×1080, sin vsync): ~74 fps en calidad media y ~97 en
baja; ~0,5 ms de CPU por frame. La calidad alta (resolución interna completa, MSAA 8x,
sombras 4096²) es para un equipo más potente.

## Estructura

```
src/
  main.cpp                       Bucle principal, cámara, entrada
  core/
    platform.h / platform_x11.cpp  Ventana, contexto GL 3.3, teclado/ratón, reloj
    gl.h                         Cabeceras GL (sin loader: glvnd exporta todo en Linux)
    vmath.h                      Vec3/Mat4, perspectiva, lookAt (column-major, Y arriba)
  render/
    renderer.h/.cpp              Render: arranque, calidad, sombras y pasadas de la escena
    render_scene.cpp             Lotes instanciados, luces, zonas y mallas con skinning
    render_fx.cpp                Humo y fuego volumétrico a media resolución
    render_post.cpp              Buffers de escena (MSAA), bloom y composición final
    gl_util.h                    Ayudas internas del render
    shaders/                     GLSL (.vert/.frag/.glsl), metidos en el binario por CMake
    models.h/.cpp                Cuerpos continuos: primitivas → malla con skinning
    glb.h/.cpp                   Lector de glTF binario (.glb): nodos, mallas, pieles y materiales
    glb_write.h/.cpp             Escritura de personajes con esqueleto a .glb
    hero_model.h/.cpp            La protagonista desde .glb: huesos, piezas con inercia, arma
    debug_draw.h/.cpp            Lote de líneas/triángulos/puntos + shader mínimo; colores
    text.h/.cpp                  Fuentes SDF (Orbitron, Exo 2) con stb_truetype
  audio/
    synth.h/.cpp                 Síntesis de la música (capas) y los efectos; WAV
    audio.h/.cpp                 Salida (PulseAudio con dlopen) y mezclador en un hilo
    sfx.h                        Lista de efectos y eventos de sonido de la lógica
  actors/
    skeleton.h/.cpp              Esqueletos genéricos (jerarquía y cinemática directa)
    physics.h/.cpp               Cajas AABB, cuerpos con gravedad, rayos (segmento vs caja)
    character.h/.cpp             Protagonista: carrera, salto, combos, esquiva, guardia
    enemy.h/.cpp                 Especies de enemigos: esqueletos, formas, animación
    brain.h/.cpp                 Aprendizaje por refuerzo de los Grises (Q-learning)
  game/
    game.h/.cpp                  Estado del juego, arranque y bucle de actualización
    combat.cpp                   Apuntar, golpes, magias, daño, muerte y reaparición
    enemy_ai.cpp                 Decisiones, movimiento y ataques de los enemigos; proyectiles
    loot.cpp                     Botín, recoger, experiencia y niveles, fabricar, equipar
    light.cpp / shadow.cpp       La luz y la sombra: viajar por la luz, fundirse, farolas, Supernova
    levels.cpp                   Escenarios, partes, combates de zona, puertas, rompecabezas
    levels_night.cpp             Niveles de noche: selva, cueva, callejones (antorchas, cristales, farolillos)
    test_mode.cpp                Modo prueba (patio con enemigos sin fin)
    allies.cpp                   Formas prismáticas y compañeros
    game_story.cpp / cinema.cpp  Escenas del guion, órdenes y cinemáticas
    story.h/.cpp                 Formato de guion y reproductor de diálogos (UTF-8)
    items.h/.cpp                 Objetos, armas e inventario
    game_draw.cpp                Dibujado del mundo, personajes y efectos (y versión debug)
  ui/
    hud.cpp                      HUD: barras, nivel, comandos, objetivo, textos de historia
    menu.h/.cpp                  Menú de pausa
    inventory_menu.h/.cpp        Menú de inventario y taller
tests/
  logic_test.cpp                 Tests de la lógica sin ventana (combate, IA, botín, croma...)
cmake/embed_shaders.cmake        Convierte los shaders en cadenas C++ al compilar
assets/fonts/                    Orbitron y Exo 2 (licencia SIL OFL, ver OFL-*.txt)
assets/story/                    Guiones de la historia
third_party/stb/                 stb_truetype.h (dominio público)
```

### Cómo funcionan los esqueletos

Cada articulación tiene un padre, un offset fijo respecto a él (pose de reposo) y una
rotación local que pone la animación. La matriz mundo se calcula recorriendo el array en
orden (los padres siempre van antes que los hijos):

```
world[i] = world[padre] * T(offset) * Ry * Rx * Rz
```

El protagonista usa un esqueleto humanoide de 19 articulaciones; cada enemigo tiene el
suyo (orejas y cola del duende, sombrero de la seta, alas de la polilla). Las formas del
cuerpo son elipsoides pegados a una articulación (*skinning rígido*).

## Próximos pasos sugeridos

1. Capítulo 2: el puente y la luz encendida al otro lado; más especies de Grises.
2. Un jefe con varias fases y su propia variación de la música.
3. Retratos animados en los diálogos y escenas con cámara guionizada.
4. Síntesis de mejoras para el prisma con los materiales.
