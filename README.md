# unamed_game

Action-RPG en tercera persona en C++17 + OpenGL 3.3 Core, sin motor. **La Hora Detenida**:
la ciudad de Iridia se quedó parada en el crepúsculo y la luz y la sombra se separaron. El
protagonista, hecho de las dos, despierta sin fuerzas en una plaza gris y va recuperando
poderes de ambos lados para volver a poner en marcha la hora. Tono adulto y sobrio, casi sin
diálogo: la historia se cuenta con escenas e imágenes (ver `docs/historia.md`).

## Historia

La biblia está en `docs/historia.md`. Los guiones son ficheros de texto en `assets/story/`
(formato explicado en `src/story.h`). *El capítulo 1 está pendiente de reescribirse con el
enfoque nuevo (escenas visuales y tutoriales mínimos).*

## Música y sonido

Todo está sintetizado por código (`src/synth.cpp`), sin ficheros de audio en el
repositorio. Hay una pista por escenario y otra de jefe (cada una con su tonalidad, modo, tempo e
instrumentos, y melodías generadas por frases a partir de la armonía); cada pista son cuatro capas
sincronizadas cuyo volumen decide el juego: el **colchón** siempre, la **melodía** cuando
vuelve el color, el **pulso** (bajo y percusión) en combate y el **brillo** (arpegio) cuando
la ciudad ya brilla. Se genera la primera vez en segundo plano (~2 s) y se guarda en
`~/.cache/unamed_game/`. Los 26 efectos se generan al arrancar: golpes en capas (chasquido de
impacto, cuerpo grave, crujido y timbre metálico, con saturación), guardia, explosiones,
enemigos que gruñen al preparar el ataque y se deshacen al morir, sin campanitas.

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

Opciones: `--silencio` (sin audio), `--volumen 0..1`. De desarrollo: `--autoplay` (un bot combate solo, sin capturar el ratón),
`--screenshot fichero.ppm segundos [frames]` (guarda frames y sale) y `--camyaw radianes`.

## Controles

| Tecla | Acción |
|---|---|
| W A S D | Correr (relativo a la cámara). **Shift**: correr más rápido |
| Espacio | Saltar; en el aire, **doble salto** con voltereta |
| Clic izquierdo | Atacar (combo según el arma). **Mantener**: corte cargado (lanza una onda) |
| Clic derecho (mantener) | Guardia (se puede andar mirando a la cámara). Justo a tiempo: bloqueo perfecto |
| C / Ctrl | Voltereta de esquiva (invulnerable); un golpe justo después es un **contraataque** |
| Q | **Luz**: junto a una farola, **entrar en ella**; dentro, saltar a otra fuente. **Sombra**: desde un charco oscuro, **deslizarse** a otro |
| Mantener Q | **Luz**: absorber luz (nodos, Grises aturdidos). **Sombra**: **beber la luz** de una farola (se apaga y deja oscuridad) |
| Z | Cambiar entre **luz y sombra** (cuando la sombra ya ha despertado) |
| V | **Supernova** (luz, con la Luz llena) · **Agujero negro** (sombra, 60 de Sombra) |
| G | **Transformarse** (con un núcleo) |
| T | **Invocar compañero** (chispa aliada o el eco de Nora) |
| R | Fijar objetivo / **soltarlo** (dentro de una fuente: elegir a qué fuente saltar) · clic central: siguiente objetivo · X: soltar |
| 1 2 3 · 4 5 6 · Rueda + E | Técnicas de luz · atajos de objetos · menú de comandos |
| F | Interactuar: cápsulas, nodos, faroles, espejos, objetos; en modo luz, **encender una farola apagada** (15 de Luz) |
| Tab / I | Inventario y **taller** (pausa) |
| Esc / P | Pausa: sensibilidad, calidad gráfica, reiniciar, salir |
| F12 / º | Modo debug (esqueletos, zonas, haces y la táctica de cada Gris) |

Especiales de combate: atacar corriendo = **embestida**; atacar con la guardia =
**lanzador** (sube al enemigo); en el aire con la guardia = **picado**.

## Jugabilidad

- **Luz y sombra**: dos mitades del mismo protagonista, sin bando bueno ni malo.
  - **Luz**: se absorbe junto a nodos y faroles o de un Gris aturdido. Junto a una farola
    encendida, Q **entra en ella**: dentro no te ven ni te hieren, recuperas Luz, saltas a
    otras fuentes a la vista (R elige cuál), sales con Espacio o **emboscas** con el clic
    al Gris más cercano. Técnicas (*Lanza de luz*, *Refracción*, *Calidez*) y Supernova.
  - **Sombra**: se consigue de los Vaciados y bebiendo la luz de las farolas, que se apagan
    y dejan un **charco de oscuridad**. Dentro de uno te **fundes** (corres más, solo te ven
    de muy cerca, recuperas Sombra) y te **deslizas** a otros charcos. **Agujero negro**:
    atrae a los enemigos y sus proyectiles y acaba en implosión.
  - Romper una farola para tener sombra te quita ese camino de luz (y se puede volver a
    encender en modo luz): de ahí salen los acertijos.
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

## Modos de prueba

Sin historia, con **vida, Luz y Sombra infinitas**, la sombra ya despierta y todas las armas y núcleos de forma en el inventario.
Se abren desde el menú de pausa (cada vez con un escenario nuevo) o al arrancar:

```bash
./build/game --prueba                 # Grises sin fin en un patio aleatorio
./build/game --prueba luz             # archipiélago de islas flotantes para el viaje por la luz
./build/game --prueba combate 1234    # con semilla: la misma semilla da el mismo escenario
```

- **Grises sin fin**: patio cerrado por edificios, con pilares, plataformas, árboles, cajas, farolas
  y nodos ya encendidos, todo colocado al azar. Siempre hay Grises (de 3 a 9 a la vez, más cuantos más
  disipas) y cada 30 bajas aparece un Gris mayor.
- **Viaje por la luz**: islas sobre el vacío, cada una con su baliza a menos de 35 m de otra y
  agujas con una luz arriba. El objetivo cuenta las islas alcanzadas y señala la baliza más cercana
  que falta; al caer vuelves a la última isla pisada.

## Modo debug (F12)

No usa el render con shaders: se dibuja en alambre con un único buffer de líneas
(esqueletos con la izquierda en azul y la derecha en rojo, articulaciones, cajas, escenario,
sector del último golpe, botín como cruces) y encima de cada enemigo su estado de IA y vida.

## Render

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
  específico de plataforma vive en `platform_x11.cpp` detrás de `platform.h`, así que
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
  platform.h / platform_x11.cpp  Ventana, contexto GL 3.3, teclado/ratón, reloj
  gl.h                           Cabeceras GL (sin loader: glvnd exporta todo en Linux)
  vmath.h                        Vec3/Mat4, perspectiva, lookAt (column-major, Y arriba)
  debug_draw.h/.cpp              Lote de líneas/triángulos/puntos + shader mínimo
  skeleton.h/.cpp                Esqueletos genéricos (jerarquía y cinemática directa)
  physics.h/.cpp                 Cajas AABB, cuerpos con gravedad, rayos (segmento vs caja)
  character.h/.cpp               Protagonista: carrera, salto, combos, esquiva, guardia
  enemy.h/.cpp                   Especies de enemigos: esqueletos, formas, animación
  items.h/.cpp                   Objetos, armas e inventario
  game.h/.cpp                    Escenario, oleadas, IA, combate, magias, botín, cofres
  game_story.cpp                 Escenas del guion, órdenes, Brizna y ecos de memoria
  story.h/.cpp                   Formato de guion y reproductor de diálogos (UTF-8)
  synth.h/.cpp                   Síntesis de la música (capas) y los efectos; WAV
  audio.h/.cpp                   Salida (PulseAudio con dlopen) y mezclador en un hilo
  sfx.h                          Lista de efectos y eventos de sonido de la lógica
  levels.cpp                     Escenarios, partes, combates de zona, puertas, rompecabezas
  light.cpp                      La Luz: absorber, hacerse luz, Supernova
  allies.cpp                     Formas prismáticas y compañeros
  brain.h/.cpp                   Aprendizaje por refuerzo de los Grises (Q-learning)
  game_draw.cpp                  Dibujado del mundo, efectos y HUD (y su versión debug)
  inventory_menu.h/.cpp          Menú de inventario
  text.h/.cpp                    Fuentes SDF (Orbitron, Exo 2) con stb_truetype: HUD y menús
  models.h/.cpp                  Cuerpos continuos: primitivas → malla con skinning
  menu.h/.cpp                    Menú de pausa
  renderer.h/.cpp                Render: instancing, skinning, sombras, bloom, calidad, croma
  main.cpp                       Bucle principal, cámara, entrada
tests/
  logic_test.cpp                 Tests de la lógica sin ventana (combate, IA, botín, croma...)
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
